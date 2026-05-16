#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <atomic>

// ----------------------------------------------------------------------------
// CDataObject
//
// Implements IDataObject exposing a list of file paths via CF_HDROP.
//
// CF_HDROP is the clipboard format understood by virtually every Windows
// application that accepts file drops (Explorer, Chrome, Discord, Slack,
// VS Code, etc.).  The format stores a DROPFILES header followed by a
// double-null-terminated list of NUL-separated wide strings.
//
// SetData() is also implemented so that IDragSourceHelper can attach
// drag image formats, and drop targets can store auxiliary formats
// (PerformedDropEffect, TargetCLSID, etc.) during the drag loop.
// ----------------------------------------------------------------------------
class CDataObject : public IDataObject
{
public:
    // files must be absolute, fully-qualified paths (use Utils::ResolvePaths)
    explicit CDataObject(const std::vector<std::wstring>& files);
    ~CDataObject();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDataObject – implemented
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override;
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override;
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) override;
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnum) override;

    // IDataObject – stubs (drag source does not need these)
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*)                            override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pOut) override { if(pOut) pOut->ptd=nullptr; return DATA_S_SAMEFORMATETC; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*)              override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)                                               override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)                                   override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    // Build the HGLOBAL containing the DROPFILES structure for our file list.
    // Returns NULL on allocation failure.
    HGLOBAL BuildHDrop() const;

    // Storage for formats set via SetData (drag image, drop descriptions, etc.)
    struct StoredMedium
    {
        FORMATETC fmt;
        STGMEDIUM med;
    };
    std::vector<StoredMedium> m_storedData;

    std::vector<std::wstring> m_files;
    std::atomic<ULONG>        m_refCount{1};
};
