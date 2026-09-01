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
    // Drop flag is checked FIRST because we use PostThreadMessage with
    // VK_ESCAPE to trigger this call — fEscapePressed will be TRUE even
    // when the user pressed F8 to drop. The flag distinguishes them.
    if (m_shouldDrop && *m_shouldDrop)
        return DRAGDROP_S_DROP;

    // Cancel: either via the injected VK_ESCAPE (with g_shouldCancel set)
    // or via a real Escape press.
    if ((m_shouldCancel && *m_shouldCancel) || fEscapePressed || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
        return DRAGDROP_S_CANCEL;

    if (grfKeyState & MK_LBUTTON)
    {
        m_lbuttonDown = true;
    }
    else if (m_lbuttonDown)
    {
        // Left button was pressed during drag and is now released -> complete drop
        return DRAGDROP_S_DROP;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CDropSource::GiveFeedback(DWORD)
{
    return DRAGDROP_S_USEDEFAULTCURSORS;
}
