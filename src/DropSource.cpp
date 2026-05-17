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

    // Drop flag is checked FIRST because we use PostThreadMessage with
    // VK_ESCAPE to trigger this call — fEscapePressed will be TRUE even
    // when the user pressed F8 to drop. The flag distinguishes them.
    if (m_shouldDrop && *m_shouldDrop)
        return DRAGDROP_S_DROP;

    // Cancel: either via the injected VK_ESCAPE (with g_shouldCancel set)
    // or via a real Escape press that somehow reached DoDragDrop.
    if ((m_shouldCancel && *m_shouldCancel) || fEscapePressed)
        return DRAGDROP_S_CANCEL;

    // Keep drag alive.
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDropSource::GiveFeedback(DWORD)
{
    return DRAGDROP_S_USEDEFAULTCURSORS;
}
