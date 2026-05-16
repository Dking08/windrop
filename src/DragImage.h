#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <objidl.h>
#include <vector>
#include <string>

// ----------------------------------------------------------------------------
// DragImage
//
// Wraps IDragSourceHelper (and its v2 extension IDragSourceHelper2).
//
// IDragSourceHelper lets us attach the same translucent shell drag image that
// Explorer uses, making the drag operation feel completely native.
//
// InitializeFromWindow() uses a hidden helper window as the source HWND;
// this is the recommended way to get a drag image when dragging from a
// non-visual source such as a CLI application.
//
// If shell image creation fails (e.g. running headless) we fall back
// gracefully and the drag continues without an image – DoDragDrop still works.
// ----------------------------------------------------------------------------
namespace DragImage
{
    // Attach a native shell drag image to pDataObj.
    // hwnd    - helper window (can be a minimal hidden HWND)
    // files   - list of fully-qualified paths being dragged
    // pDataObj- the IDataObject we pass to DoDragDrop
    //
    // Returns true on success.  On failure, pDataObj is unchanged and the
    // caller should still proceed with DoDragDrop.
    bool AttachShellImage(HWND hwnd,
                          const std::vector<std::wstring>& files,
                          IDataObject* pDataObj);
}
