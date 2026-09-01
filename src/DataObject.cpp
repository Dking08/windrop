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
// Registered shell clipboard formats
// ---------------------------------------------------------------------------
static const UINT g_cfPreferredDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
static const UINT g_cfShellUrlW           = RegisterClipboardFormatW(CFSTR_SHELLURL);
static const UINT g_cfFileNameW           = RegisterClipboardFormatW(CFSTR_FILENAMEW);
static const UINT g_cfFileNameA           = RegisterClipboardFormatW(CFSTR_FILENAMEA);

// ---------------------------------------------------------------------------
// BuildHDrop
//
// Layout of HGLOBAL for CF_HDROP:
//   [DROPFILES header]
//   [wchar_t* path1 \0]
//   [wchar_t* path2 \0]
//   ...
//   [wchar_t* pathN \0]
//   [\0]               ← extra terminator (double-null)
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildHDrop() const
{
    size_t pathChars = 1; // final double-NUL
    for (const auto& f : m_files)
        pathChars += f.size() + 1;

    const size_t headerSize = sizeof(DROPFILES);
    const size_t totalBytes = headerSize + pathChars * sizeof(wchar_t);

    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, totalBytes);
    if (!hGlobal) return nullptr;

    auto* pDrop = static_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!pDrop) { GlobalFree(hGlobal); return nullptr; }

    pDrop->pFiles = static_cast<DWORD>(headerSize);
    pDrop->pt     = {0, 0};
    pDrop->fNC    = FALSE;
    pDrop->fWide  = TRUE;

    wchar_t* dest = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(pDrop) + headerSize);

    for (const auto& f : m_files)
    {
        std::wmemcpy(dest, f.c_str(), f.size());
        dest += f.size();
        *dest++ = L'\0';
    }
    *dest = L'\0';

    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildPreferredDropEffect (CFSTR_PREFERREDDROPEFFECT)
// Advertises that we support both Copy and Move operations.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildPreferredDropEffect() const
{
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, sizeof(DWORD));
    if (!hGlobal) return nullptr;

    auto* pData = static_cast<DWORD*>(GlobalLock(hGlobal));
    if (!pData) { GlobalFree(hGlobal); return nullptr; }

    *pData = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildUnicodeText (CF_UNICODETEXT)
// Formatted paths separated by newlines for text boxes, search bars, editors.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildUnicodeText() const
{
    if (m_files.empty()) return nullptr;

    std::wstring text;
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        if (i > 0) text += L"\r\n";
        text += m_files[i];
    }

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, bytes);
    if (!hGlobal) return nullptr;

    void* p = GlobalLock(hGlobal);
    if (!p) { GlobalFree(hGlobal); return nullptr; }

    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildAnsiText (CF_TEXT)
// ANSI fallback string.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildAnsiText() const
{
    if (m_files.empty()) return nullptr;

    std::string text;
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        if (i > 0) text += "\r\n";
        int len = WideCharToMultiByte(CP_ACP, 0, m_files[i].c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 1)
        {
            std::string s(len - 1, '\0');
            WideCharToMultiByte(CP_ACP, 0, m_files[i].c_str(), -1, &s[0], len, nullptr, nullptr);
            text += s;
        }
    }

    size_t bytes = text.size() + 1;
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, bytes);
    if (!hGlobal) return nullptr;

    void* p = GlobalLock(hGlobal);
    if (!p) { GlobalFree(hGlobal); return nullptr; }

    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildShellUrl (UniformResourceLocatorW)
// file:/// URL for browsers, Electron apps, and URL drops.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildShellUrl() const
{
    if (m_files.empty()) return nullptr;

    std::wstring url = L"file:///";
    for (wchar_t ch : m_files[0])
    {
        if (ch == L'\\') url += L'/';
        else url += ch;
    }

    size_t bytes = (url.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, bytes);
    if (!hGlobal) return nullptr;

    void* p = GlobalLock(hGlobal);
    if (!p) { GlobalFree(hGlobal); return nullptr; }

    memcpy(p, url.c_str(), bytes);
    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildFileNameW (FileNameW)
// Single filename string for older drop targets.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildFileNameW() const
{
    if (m_files.empty()) return nullptr;

    size_t bytes = (m_files[0].size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, bytes);
    if (!hGlobal) return nullptr;

    void* p = GlobalLock(hGlobal);
    if (!p) { GlobalFree(hGlobal); return nullptr; }

    memcpy(p, m_files[0].c_str(), bytes);
    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// IDataObject::GetData
//
// First checks stored data (drag image, drop descriptions, etc.),
// then handles native formats (CF_HDROP, PreferredDropEffect, UnicodeText, etc.)
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::GetData(FORMATETC* pfe, STGMEDIUM* pmed)
{
    if (!pfe || !pmed) return E_POINTER;

    // Check stored data first (drag images, drop descriptions, etc.)
    for (const auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            (sm.fmt.tymed & pfe->tymed) &&
            (sm.fmt.dwAspect == pfe->dwAspect || pfe->dwAspect == DVASPECT_CONTENT))
        {
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

    if (!(pfe->tymed & TYMED_HGLOBAL)) return DATA_E_FORMATETC;

    HGLOBAL hData = nullptr;
    if (pfe->cfFormat == CF_HDROP)
    {
        hData = BuildHDrop();
    }
    else if (g_cfPreferredDropEffect && pfe->cfFormat == g_cfPreferredDropEffect)
    {
        hData = BuildPreferredDropEffect();
    }
    else if (pfe->cfFormat == CF_UNICODETEXT)
    {
        hData = BuildUnicodeText();
    }
    else if (pfe->cfFormat == CF_TEXT)
    {
        hData = BuildAnsiText();
    }
    else if (g_cfShellUrlW && pfe->cfFormat == g_cfShellUrlW)
    {
        hData = BuildShellUrl();
    }
    else if (g_cfFileNameW && pfe->cfFormat == g_cfFileNameW)
    {
        hData = BuildFileNameW();
    }
    else
    {
        return DATA_E_FORMATETC;
    }

    if (!hData) return E_OUTOFMEMORY;

    pmed->tymed          = TYMED_HGLOBAL;
    pmed->hGlobal        = hData;
    pmed->pUnkForRelease = nullptr;
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDataObject::QueryGetData
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::QueryGetData(FORMATETC* pfe)
{
    if (!pfe) return E_POINTER;

    for (const auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            (sm.fmt.tymed & pfe->tymed) &&
            (sm.fmt.dwAspect == pfe->dwAspect || pfe->dwAspect == DVASPECT_CONTENT))
            return S_OK;
    }

    if (!(pfe->tymed & TYMED_HGLOBAL)) return DATA_E_FORMATETC;

    if (pfe->cfFormat == CF_HDROP ||
        (g_cfPreferredDropEffect && pfe->cfFormat == g_cfPreferredDropEffect) ||
        pfe->cfFormat == CF_UNICODETEXT ||
        pfe->cfFormat == CF_TEXT ||
        (g_cfShellUrlW && pfe->cfFormat == g_cfShellUrlW) ||
        (g_cfFileNameW && pfe->cfFormat == g_cfFileNameW))
    {
        return S_OK;
    }

    return DATA_E_FORMATETC;
}

// ---------------------------------------------------------------------------
// IDataObject::SetData
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::SetData(FORMATETC* pfe, STGMEDIUM* pmed, BOOL fRelease)
{
    if (!pfe || !pmed) return E_POINTER;

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
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnum)
{
    if (!ppEnum) return E_POINTER;
    if (dwDirection != DATADIR_GET) return E_NOTIMPL;

    std::vector<FORMATETC> fmts;

    auto addFormat = [&](CLIPFORMAT cf) {
        if (!cf) return;
        FORMATETC fe = {};
        fe.cfFormat = cf;
        fe.ptd      = nullptr;
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex   = -1;
        fe.tymed    = TYMED_HGLOBAL;
        fmts.push_back(fe);
    };

    addFormat(CF_HDROP);
    if (g_cfPreferredDropEffect)
        addFormat(static_cast<CLIPFORMAT>(g_cfPreferredDropEffect));
    addFormat(CF_UNICODETEXT);
    addFormat(CF_TEXT);
    if (g_cfShellUrlW)
        addFormat(static_cast<CLIPFORMAT>(g_cfShellUrlW));
    if (g_cfFileNameW)
        addFormat(static_cast<CLIPFORMAT>(g_cfFileNameW));

    for (const auto& sm : m_storedData)
        fmts.push_back(sm.fmt);

    auto* p = new (std::nothrow) CEnumFormatEtc(fmts);
    if (!p) return E_OUTOFMEMORY;
    *ppEnum = p;
    return S_OK;
}
