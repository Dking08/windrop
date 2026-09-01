// drag.exe — CLI-initiated Windows drag-and-drop utility (dragon/blobdrop for Windows)
//
// Usage:  drag <file1> [file2 ...]
// Flow:   A sleek floating drag-card appears at cursor → Click & drag to any app → drop!

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <ole2.h>
#include <cstdio>
#include <string>
#include <vector>

#include "DropSource.h"
#include "DataObject.h"
#include "DragImage.h"
#include "Utils.h"

static constexpr int EXIT_OK           = 0;
static constexpr int EXIT_BAD_ARGS     = 1;
static constexpr int EXIT_FILE_MISSING = 2;
static constexpr int EXIT_CANCELLED    = 3;
static constexpr int EXIT_COM_ERROR    = 4;

static const wchar_t* kWindowClassName = L"DragPayloadCardWnd";

struct DragPayloadState
{
    std::vector<std::wstring> files;
    CDataObject*              pDataObj = nullptr;
    CDropSource*              pDropSrc = nullptr;
    HICON                     hIcon    = nullptr;
    std::wstring              displayTitle;
    std::wstring              displaySubtitle;
    int                       exitCode = EXIT_CANCELLED;
};

static DragPayloadState* g_pState = nullptr;

static void PerformDrag(HWND hwnd)
{
    if (!g_pState || !g_pState->pDataObj || !g_pState->pDropSrc)
        return;

    ShowWindow(hwnd, SW_HIDE);

    DWORD dwEffect = 0;
    DWORD dwOK     = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

    HRESULT hr = DoDragDrop(g_pState->pDataObj, g_pState->pDropSrc, dwOK, &dwEffect);

    if (hr == DRAGDROP_S_DROP)
    {
        if (dwEffect & DROPEFFECT_MOVE)
        {
            wprintf(L"Drop completed (Move).\n");
            g_pState->exitCode = EXIT_OK;
        }
        else if (dwEffect & (DROPEFFECT_COPY | DROPEFFECT_LINK))
        {
            wprintf(L"Drop completed (Copy).\n");
            g_pState->exitCode = EXIT_OK;
        }
        else
        {
            wprintf(L"Drop completed.\n");
            g_pState->exitCode = EXIT_OK;
        }
    }
    else
    {
        wprintf(L"Drag canceled.\n");
        g_pState->exitCode = EXIT_CANCELLED;
    }

    DestroyWindow(hwnd);
}

static LRESULT CALLBACK DragCardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Double buffer to avoid any flicker
        HDC memDC      = CreateCompatibleDC(hdc);
        HBITMAP memBm  = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBm  = static_cast<HBITMAP>(SelectObject(memDC, memBm));

        // Dark modern background (Windows 11 dark acrylic style)
        HBRUSH bgBrush   = CreateSolidBrush(RGB(32, 33, 36));
        HPEN borderPen   = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
        HBRUSH oldBrush  = static_cast<HBRUSH>(SelectObject(memDC, bgBrush));
        HPEN oldPen      = static_cast<HPEN>(SelectObject(memDC, borderPen));

        RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 16, 16);

        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(bgBrush);
        DeleteObject(borderPen);

        // Draw 32x32 Shell File Icon
        if (g_pState && g_pState->hIcon)
        {
            DrawIconEx(memDC, 14, (rc.bottom - 32) / 2, g_pState->hIcon, 32, 32, 0, nullptr, DI_NORMAL);
        }

        // Draw Typography
        SetBkMode(memDC, TRANSPARENT);

        HFONT hFontTitle = CreateFontW(
            -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HFONT hFontSub = CreateFontW(
            -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        if (g_pState)
        {
            // Title (file name or multi-file summary)
            HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, hFontTitle));
            SetTextColor(memDC, RGB(245, 245, 245));
            RECT rcTitle = { 56, 14, rc.right - 14, 34 };
            DrawTextW(memDC, g_pState->displayTitle.c_str(), -1, &rcTitle,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            // Subtitle
            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, RGB(160, 160, 160));
            RECT rcSub = { 56, 36, rc.right - 14, 54 };
            DrawTextW(memDC, g_pState->displaySubtitle.c_str(), -1, &rcSub,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            SelectObject(memDC, oldFont);
        }

        DeleteObject(hFontTitle);
        DeleteObject(hFontSub);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBm);
        DeleteObject(memBm);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        PerformDrag(hwnd);
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (wp == VK_ESCAPE)
        {
            wprintf(L"Drag canceled.\n");
            if (g_pState) g_pState->exitCode = EXIT_CANCELLED;
            DestroyWindow(hwnd);
            return 0;
        }
        if (wp == VK_F8 || wp == VK_RETURN || wp == VK_SPACE)
        {
            PerformDrag(hwnd);
            return 0;
        }
        break;
    }

    case WM_RBUTTONUP:
    {
        wprintf(L"Drag canceled.\n");
        if (g_pState) g_pState->exitCode = EXIT_CANCELLED;
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Entry point ──
int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) { Utils::PrintUsage(); return EXIT_BAD_ARGS; }

    // Enable Per-Monitor V2 DPI awareness
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    auto pfnSetDpi = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (pfnSetDpi)
        pfnSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::vector<std::wstring> rawArgs;
    for (int i = 1; i < argc; ++i)
        rawArgs.emplace_back(argv[i]);

    std::vector<std::wstring> missing;
    std::vector<std::wstring> files = Utils::ResolvePaths(rawArgs, missing);

    for (const auto& m : missing)
        Utils::PrintError(L"File not found: " + m);
    if (!missing.empty()) return EXIT_FILE_MISSING;
    if (files.empty())    { Utils::PrintUsage(); return EXIT_BAD_ARGS; }

    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr))
    {
        Utils::PrintError(L"OleInitialize failed");
        return EXIT_COM_ERROR;
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Register Window Class
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DragCardWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    DragPayloadState state;
    state.files = files;
    state.pDataObj = new (std::nothrow) CDataObject(files);
    state.pDropSrc = new (std::nothrow) CDropSource(nullptr, nullptr);

    if (!state.pDataObj || !state.pDropSrc)
    {
        Utils::PrintError(L"Out of memory allocating COM objects");
        if (state.pDropSrc) state.pDropSrc->Release();
        if (state.pDataObj) state.pDataObj->Release();
        UnregisterClassW(kWindowClassName, hInst);
        OleUninitialize();
        return EXIT_COM_ERROR;
    }

    // Extract File Icon
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(files[0].c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
        state.hIcon = sfi.hIcon;

    // Build Titles
    if (files.size() == 1)
    {
        size_t lastSlash = files[0].find_last_of(L"\\/");
        state.displayTitle = (lastSlash != std::wstring::npos) ? files[0].substr(lastSlash + 1) : files[0];
    }
    else
    {
        state.displayTitle = std::to_wstring(files.size()) + L" files";
    }
    state.displaySubtitle = L"Drag me to drop \x2022 [Esc] Exit";

    g_pState = &state;

    // Window Dimensions and Positioning near mouse
    POINT pt;
    GetCursorPos(&pt);

    const int kWidth  = 280;
    const int kHeight = 70;
    int posX = pt.x - 20;
    int posY = pt.y - 20;

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi))
    {
        if (posX + kWidth > mi.rcWork.right)   posX = mi.rcWork.right - kWidth - 10;
        if (posX < mi.rcWork.left)             posX = mi.rcWork.left + 10;
        if (posY + kHeight > mi.rcWork.bottom) posY = mi.rcWork.bottom - kHeight - 10;
        if (posY < mi.rcWork.top)              posY = mi.rcWork.top + 10;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName, L"drag",
        WS_POPUP,
        posX, posY, kWidth, kHeight,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd)
    {
        Utils::PrintError(L"Failed to create drag payload window");
        state.pDropSrc->Release();
        state.pDataObj->Release();
        if (state.hIcon) DestroyIcon(state.hIcon);
        UnregisterClassW(kWindowClassName, hInst);
        OleUninitialize();
        return EXIT_COM_ERROR;
    }

    // Apply rounded window region
    HRGN hRgn = CreateRoundRectRgn(0, 0, kWidth + 1, kHeight + 1, 16, 16);
    SetWindowRgn(hwnd, hRgn, TRUE);

    // Attach Shell Drag Image
    DragImage::AttachShellImage(hwnd, files, state.pDataObj);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    fwprintf(stderr, L"Drag payload ready. Drag card to target application or press Esc to cancel.\n");
    for (const auto& f : files)
        fwprintf(stderr, L"  %s\n", f.c_str());
    fflush(stderr);

    // Standard Win32 message pump
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (state.hIcon)
        DestroyIcon(state.hIcon);

    state.pDropSrc->Release();
    state.pDataObj->Release();
    UnregisterClassW(kWindowClassName, hInst);
    OleUninitialize();

    return state.exitCode;
}
