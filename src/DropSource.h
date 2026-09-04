#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <atomic>

// ----------------------------------------------------------------------------
// CDropSource — keyboard-driven IDropSource
//
// Uses external volatile flags (set by hotkey handler in main.cpp)
// to decide when to drop or cancel. Supports both keyboard triggers
// and standard mouse release.
// ----------------------------------------------------------------------------
class CDropSource : public IDropSource
{
public:
    // shouldDrop/shouldCancel are set by the hotkey handler in main.cpp
    CDropSource(volatile bool* shouldDrop, volatile bool* shouldCancel);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override;
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) override;

private:
    volatile bool* m_shouldDrop;
    volatile bool* m_shouldCancel;
    bool m_lbuttonDown = false;
    std::atomic<ULONG> m_refCount{1};
};
