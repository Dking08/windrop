#include "DataObject.h"
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helper: duplicate an HGLOBAL block
// ---------------------------------------------------------------------------
static HGLOBAL DuplicateHGlobal(HGLOBAL hSrc)
{
    if (!hSrc) return nullptr;
    SIZE_T cb = GlobalSize(hSrc);
    void*  pSrc = GlobalLock(hSrc);
    if (!pSrc) return nullptr;

    HGLOBAL hDst = GlobalAlloc(GHND | GMEM_SHARE, cb);
    if (hDst)
    {
        void* pDst = GlobalLock(hDst);
        if (pDst) { memcpy(pDst, pSrc, cb); GlobalUnlock(hDst); }
        else      { GlobalFree(hDst); hDst = nullptr; }
    }
    GlobalUnlock(hSrc);
    return hDst;
}

// ---------------------------------------------------------------------------
// Simple IEnumFORMATETC implementation so callers can discover our formats.
// Enumerates CF_HDROP plus any formats stored via SetData().
// ---------------------------------------------------------------------------
class CEnumFormatEtc : public IEnumFORMATETC
{
public:
    CEnumFormatEtc(const std::vector<FORMATETC>& fmts)
        : m_fmts(fmts), m_index(0), m_refCount(1)
    {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumFORMATETC))
        { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG c=--m_refCount; if(!c) delete this; return c; }

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override
    {
        if (!rgelt) return E_POINTER;
        ULONG fetched = 0;
        while (fetched < celt && m_index < m_fmts.size())
        {
            rgelt[fetched] = m_fmts[m_index];
            ++m_index;
            ++fetched;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return fetched == celt ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override { m_index += celt; return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset()          override { m_index = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** ppEnum) override
    {
        if (!ppEnum) return E_POINTER;
        auto* p = new (std::nothrow) CEnumFormatEtc(m_fmts);
        if (!p) return E_OUTOFMEMORY;
        p->m_index = m_index;
        *ppEnum = p;
        return S_OK;
    }

private:
    std::vector<FORMATETC> m_fmts;
    size_t                 m_index;
    std::atomic<ULONG>     m_refCount;
};

// ---------------------------------------------------------------------------
// CDataObject
// ---------------------------------------------------------------------------
CDataObject::CDataObject(const std::vector<std::wstring>& files)
    : m_files(files), m_refCount(1)
{}

CDataObject::~CDataObject()
{
    // Release all stored media
    for (auto& sm : m_storedData)
        ReleaseStgMedium(&sm.med);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject))
    {
        *ppvObject = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CDataObject::AddRef()  { return ++m_refCount; }
ULONG STDMETHODCALLTYPE CDataObject::Release()
{
    ULONG c = --m_refCount;
    if (!c) delete this;
    return c;
}

// ---------------------------------------------------------------------------
// BuildHDrop
//
// Layout of HGLOBAL for CF_HDROP:
//
//   [DROPFILES header]
//   [wchar_t* path1 \0]
//   [wchar_t* path2 \0]
//   ...
//   [wchar_t* pathN \0]
//   [\0]               ← extra terminator (double-null)
//
// The DROPFILES.pFiles field is the byte offset from the start of the
// allocation to the first path character.
// DROPFILES.fWide = TRUE means the path list is Unicode (UTF-16).
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildHDrop() const
{
    // Calculate the total number of wchar_t characters needed for all paths
    // (each path is NUL-terminated, plus one final NUL).
    size_t pathChars = 1; // final double-NUL
    for (const auto& f : m_files)
        pathChars += f.size() + 1;

    const size_t headerSize = sizeof(DROPFILES);
    const size_t totalBytes = headerSize + pathChars * sizeof(wchar_t);

    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, totalBytes);
    if (!hGlobal) return nullptr;

    auto* pDrop = static_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!pDrop) { GlobalFree(hGlobal); return nullptr; }

    pDrop->pFiles = static_cast<DWORD>(headerSize); // offset to first path
    pDrop->pt     = {0, 0};
    pDrop->fNC    = FALSE;
    pDrop->fWide  = TRUE;  // UTF-16 paths

    // Write paths after the header
    wchar_t* dest = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(pDrop) + headerSize);

    for (const auto& f : m_files)
    {
        std::wmemcpy(dest, f.c_str(), f.size());
        dest += f.size();
        *dest++ = L'\0';
    }
    *dest = L'\0'; // final terminator

    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// IDataObject::GetData
//
// The drop target calls this to retrieve the actual file data.
// First checks stored data (from SetData), then handles CF_HDROP natively.
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::GetData(FORMATETC* pfe, STGMEDIUM* pmed)
{
    if (!pfe || !pmed) return E_POINTER;

    // Check stored data first (drag images, drop descriptions, etc.)
    for (const auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            (sm.fmt.tymed & pfe->tymed) &&
            sm.fmt.dwAspect == pfe->dwAspect)
        {
            // Duplicate the stored medium for the caller. Handing back the
            // exact same handle/pointer we still own (without duplicating
            // or AddRef'ing it) would mean both our destructor's
            // ReleaseStgMedium and the caller's eventual release free the
            // same object — a double-free / use-after-free. tymed
            // determines the correct way to make the copy safe.
            pmed->tymed = sm.med.tymed;
            pmed->pUnkForRelease = nullptr;

            switch (sm.med.tymed)
            {
            case TYMED_HGLOBAL:
                pmed->hGlobal = DuplicateHGlobal(sm.med.hGlobal);
                return pmed->hGlobal ? S_OK : E_OUTOFMEMORY;

            case TYMED_ISTREAM:
                pmed->pstm = sm.med.pstm;
                if (pmed->pstm) pmed->pstm->AddRef();
                return S_OK;

            case TYMED_ISTORAGE:
                pmed->pstg = sm.med.pstg;
                if (pmed->pstg) pmed->pstg->AddRef();
                return S_OK;

            default:
                // Raw GDI/metafile handles (TYMED_GDI, TYMED_MFPICT,
                // TYMED_ENHMF) aren't refcounted and can't be safely
                // duplicated here. Only share them as-is if the medium
                // came with its own pUnkForRelease (the caller relies on
                // that, not on the raw handle, to manage lifetime);
                // otherwise refuse rather than risk an unsafe alias.
                if (sm.med.pUnkForRelease)
                {
                    *pmed = sm.med;
                    pmed->pUnkForRelease->AddRef();
                    return S_OK;
                }
                return DATA_E_FORMATETC;
            }
        }
    }

    // Native CF_HDROP handling
    if (pfe->cfFormat != CF_HDROP)      return DATA_E_FORMATETC;
    if (!(pfe->tymed & TYMED_HGLOBAL))  return DATA_E_FORMATETC;
    if (pfe->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;

    HGLOBAL hDrop = BuildHDrop();
    if (!hDrop) return E_OUTOFMEMORY;

    pmed->tymed          = TYMED_HGLOBAL;
    pmed->hGlobal        = hDrop;
    pmed->pUnkForRelease = nullptr; // caller frees via ReleaseStgMedium
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDataObject::QueryGetData
//
// Returns S_OK if we can provide the requested format.
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::QueryGetData(FORMATETC* pfe)
{
    if (!pfe) return E_POINTER;

    // Check stored formats
    for (const auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            (sm.fmt.tymed & pfe->tymed) &&
            sm.fmt.dwAspect == pfe->dwAspect)
            return S_OK;
    }

    // Native CF_HDROP
    if (pfe->cfFormat != CF_HDROP)    return DATA_E_FORMATETC;
    if (!(pfe->tymed & TYMED_HGLOBAL)) return DATA_E_FORMATETC;
    if (pfe->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDataObject::SetData
//
// Accepts data from the shell drag image helper and drop targets.
// If fRelease is TRUE, we take ownership of the medium.
// If fRelease is FALSE, we duplicate the HGLOBAL.
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::SetData(FORMATETC* pfe, STGMEDIUM* pmed, BOOL fRelease)
{
    if (!pfe || !pmed) return E_POINTER;

    // Replace existing entry for the same format, or add new
    for (auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            sm.fmt.dwAspect == pfe->dwAspect)
        {
            ReleaseStgMedium(&sm.med);
            sm.fmt = *pfe;

            if (fRelease)
            {
                sm.med = *pmed;
            }
            else
            {
                sm.med = {};
                sm.med.tymed = pmed->tymed;
                if (pmed->tymed == TYMED_HGLOBAL)
                    sm.med.hGlobal = DuplicateHGlobal(pmed->hGlobal);
                sm.med.pUnkForRelease = nullptr;
            }
            return S_OK;
        }
    }

    // New entry
    StoredMedium entry = {};
    entry.fmt = *pfe;

    if (fRelease)
    {
        entry.med = *pmed;
    }
    else
    {
        entry.med = {};
        entry.med.tymed = pmed->tymed;
        if (pmed->tymed == TYMED_HGLOBAL)
            entry.med.hGlobal = DuplicateHGlobal(pmed->hGlobal);
        entry.med.pUnkForRelease = nullptr;
    }

    m_storedData.push_back(entry);
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDataObject::EnumFormatEtc
//
// Direction DATADIR_GET: return an enumerator for our supported formats.
// Direction DATADIR_SET: we don't accept data so return E_NOTIMPL.
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnum)
{
    if (!ppEnum) return E_POINTER;
    if (dwDirection != DATADIR_GET) return E_NOTIMPL;

    // Build a list of all available formats
    std::vector<FORMATETC> fmts;

    // Native CF_HDROP
    FORMATETC fmtHDrop = {};
    fmtHDrop.cfFormat = CF_HDROP;
    fmtHDrop.ptd      = nullptr;
    fmtHDrop.dwAspect = DVASPECT_CONTENT;
    fmtHDrop.lindex   = -1;
    fmtHDrop.tymed    = TYMED_HGLOBAL;
    fmts.push_back(fmtHDrop);

    // All stored formats
    for (const auto& sm : m_storedData)
        fmts.push_back(sm.fmt);

    auto* p = new (std::nothrow) CEnumFormatEtc(fmts);
    if (!p) return E_OUTOFMEMORY;
    *ppEnum = p;
    return S_OK;
}
