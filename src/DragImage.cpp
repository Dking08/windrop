#include "DragImage.h"
#include <shellapi.h>
#include <shlobj.h>
#include <comdef.h>
#include <algorithm>
#include <cstdio>

// Older SDK headers may not define IDragSourceHelper2 – guard against that.
#ifndef CLSID_DragDropHelper
// {4657278A-411B-11d2-839A-00C04FD918D0}
DEFINE_GUID(CLSID_DragDropHelper,
    0x4657278A, 0x411B, 0x11d2,
    0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0);
#endif

namespace DragImage
{

// ---------------------------------------------------------------------------
// AttachShellImage
//
// Strategy:
//  1. CoCreate the shell DragDropHelper (CLSID_DragDropHelper).
//  2. Build a SHDRAGIMAGE from the first file's shell icon so the drag
//     payload has the familiar Explorer look (file icon + name).
//  3. Call IDragSourceHelper::InitializeFromWindow(), which fills in the
//     internal drag image and stores it on pDataObj (the data object gains an
//     extra format understood by the system drag renderer).
//
// The POINT passed to InitializeFromWindow is the cursor offset within the
// drag image bitmap. We centre it so the icon sits under the cursor tip.
// ---------------------------------------------------------------------------
bool AttachShellImage(HWND hwnd,
                      const std::vector<std::wstring>& files,
                      IDataObject* pDataObj)
{
    (void)hwnd; // reserved for future use; not needed by IDragSourceHelper
    if (!pDataObj || files.empty()) return false;

    // -----------------------------------------------------------------------
    // 1. Create the DragDropHelper COM object
    // -----------------------------------------------------------------------
    IDragSourceHelper* pHelper = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_DragDropHelper,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IDragSourceHelper,
        reinterpret_cast<void**>(&pHelper));

    if (FAILED(hr) || !pHelper) return false;

    // -----------------------------------------------------------------------
    // 2. Try IDragSourceHelper2 to enable drop descriptions (Win Vista+).
    //    This gives richer tooltips ("Copy to X", etc.) just like Explorer.
    // -----------------------------------------------------------------------
    IDragSourceHelper2* pHelper2 = nullptr;
    if (SUCCEEDED(pHelper->QueryInterface(IID_IDragSourceHelper2,
                                          reinterpret_cast<void**>(&pHelper2))))
    {
        // DSH_ALLOWDROPDESCRIPTIONTEXT lets the drop target supply text.
        pHelper2->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);
        pHelper2->Release();
    }

    // -----------------------------------------------------------------------
    // 3. Build a SHDRAGIMAGE using the shell icon of the first file.
    //    If we cannot retrieve a shell icon we use a plain 32×32 blank
    //    bitmap so InitializeFromBitmap still has something to work with.
    // -----------------------------------------------------------------------
    SHDRAGIMAGE sdi = {};

    // Attempt to get the shell file icon (large, 32×32)
    SHFILEINFOW sfi = {};
    DWORD_PTR   iconOk = SHGetFileInfoW(files[0].c_str(), 0, &sfi, sizeof(sfi),
                                         SHGFI_ICON | SHGFI_LARGEICON);

    const int ICON_SIZE = 32;
    const int PADDING   = 4;

    // Label text: filename (or "+N files" for multiple)
    wchar_t label[MAX_PATH + 16] = {};
    if (files.size() == 1)
    {
        const wchar_t* name = wcsrchr(files[0].c_str(), L'\\');
        wcscpy_s(label, name ? name + 1 : files[0].c_str());
    }
    else
    {
        swprintf_s(label, L"%zu files", files.size());
    }

    // Measure label text width using a temporary DC
    HDC hdcScreen = GetDC(nullptr);
    SIZE textSize = {};
    if (hdcScreen)
    {
        GetTextExtentPoint32W(hdcScreen, label, static_cast<int>(wcslen(label)), &textSize);
        ReleaseDC(nullptr, hdcScreen);
    }
    if (textSize.cx == 0) textSize.cx = 100;
    if (textSize.cy == 0) textSize.cy = 16;

    int bmpWidth  = PADDING + ICON_SIZE + PADDING + textSize.cx + PADDING;
    int bmpHeight = PADDING + std::max(ICON_SIZE, (int)textSize.cy) + PADDING;

    // Create a 32bpp DIB for the drag image
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = bmpWidth;
    bmi.bmiHeader.biHeight      = -bmpHeight; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HDC   hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);

    if (!hBmp || !hdcMem)
    {
        if (hdcMem) DeleteDC(hdcMem);
        if (iconOk && sfi.hIcon) DestroyIcon(sfi.hIcon);
        pHelper->Release();
        return false;
    }

    SelectObject(hdcMem, hBmp);

    // Fill with a semi-transparent white background (alpha = 0x80)
    // GDI 32bpp DIBs: pixel = BGRA, but GDI ignores alpha. We set it manually
    // after drawing by walking pvBits.  For now fill with light grey.
    HBRUSH hBrush = CreateSolidBrush(RGB(220, 220, 220));
    RECT   rc     = {0, 0, bmpWidth, bmpHeight};
    FillRect(hdcMem, &rc, hBrush);
    DeleteObject(hBrush);

    // Draw the shell icon into the bitmap
    if (iconOk && sfi.hIcon)
    {
        DrawIconEx(hdcMem, PADDING, PADDING, sfi.hIcon,
                   ICON_SIZE, ICON_SIZE, 0, nullptr, DI_NORMAL);
        DestroyIcon(sfi.hIcon);
    }

    // Draw the label text
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(0, 0, 0));
    int textX = PADDING + ICON_SIZE + PADDING;
    int textY = PADDING + (std::max(ICON_SIZE, (int)textSize.cy) - (int)textSize.cy) / 2;
    TextOutW(hdcMem, textX, textY, label, static_cast<int>(wcslen(label)));

    // Apply 50% alpha to every pixel so the image is translucent
    auto* pixels = static_cast<DWORD*>(pvBits);
    for (int i = 0; i < bmpWidth * bmpHeight; ++i)
    {
        DWORD& px = pixels[i];
        BYTE r = GetRValue(px), g = GetGValue(px), b = GetBValue(px);
        px = (0x80u << 24) | (r << 16) | (g << 8) | b;
    }

    // -----------------------------------------------------------------------
    // 4. Fill SHDRAGIMAGE and call InitializeFromBitmap
    // -----------------------------------------------------------------------
    sdi.sizeDragImage.cx = bmpWidth;
    sdi.sizeDragImage.cy = bmpHeight;
    sdi.ptOffset.x       = PADDING;      // hot-spot near top-left
    sdi.ptOffset.y       = PADDING;
    sdi.hbmpDragImage    = hBmp;
    sdi.crColorKey       = CLR_NONE;     // no colour-key transparency (we use alpha)

    hr = pHelper->InitializeFromBitmap(&sdi, pDataObj);

    // The helper takes ownership of hBmp on success; on failure we must free it.
    if (FAILED(hr))
        DeleteObject(hBmp);

    DeleteDC(hdcMem);
    pHelper->Release();

    return SUCCEEDED(hr);
}

} // namespace DragImage
