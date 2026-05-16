#include "DropSource.h"

CDropSource::CDropSource(volatile bool* shouldDrop, volatile bool* shouldCancel)
    : m_shouldDrop(shouldDrop), m_shouldCancel(shouldCancel) {}

HRESULT STDMETHODCALLTYPE CDropSource::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropSource)
    { *ppv = static_cast<IDropSource*>(this); AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CDropSource::AddRef()  { return ++m_refCount; }
ULONG STDMETHODCALLTYPE CDropSource::Release() { ULONG r = --m_refCount; if (!r) delete this; return r; }

HRESULT STDMETHODCALLTYPE CDropSource::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState)
{
    (void)grfKeyState;

    if (fEscapePressed || (m_shouldCancel && *m_shouldCancel))
        return DRAGDROP_S_CANCEL;

    if (m_shouldDrop && *m_shouldDrop)
        return DRAGDROP_S_DROP;

    // Keep drag alive — we do NOT check MK_LBUTTON because there is no
    // physical mouse button held. The drag continues until F8 or Escape.
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDropSource::GiveFeedback(DWORD)
{
    return DRAGDROP_S_USEDEFAULTCURSORS;
}
