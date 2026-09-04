// windrop.exe — CLI Windows Drag-and-Drop Utility
//
// Usage:  windrop <file1> [file2 ...]
// Flow:   - Option A (Keyboard): Hover over target and press [F8] to drag → press [F8] or left-click to drop!
//         - Option B (Mouse): Grab & drag the floating card directly to drop!
//         - Dismiss: Press [Esc] or right-click on card anytime.

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

static const wchar_t* kCardClassName = L"WindropPayloadCardWnd";

#define WM_START_F8_DRAG (WM_APP + 1)
#define WM_CLAIM_HOTKEY  (WM_APP + 2)

static constexpr int kHotkeyF8  = 1;
static constexpr int kHotkeyEsc = 2;

#define WM_HOTKEY_CMD_REGISTER   (WM_USER + 10)
#define WM_HOTKEY_CMD_UNREGISTER (WM_USER + 11)

struct WindropPayloadState
{
    std::vector<std::wstring> files;
    CDataObject*              pDataObj    = nullptr;
    CDropSource*              pDropSrc    = nullptr;
    HICON                     hIcon       = nullptr;
    std::wstring              displayTitle;
    std::wstring              displaySubtitle;
    int                       exitCode    = EXIT_CANCELLED;
    HWND                      hwndCard    = nullptr;
};

static WindropPayloadState* g_pState         = nullptr;
static DWORD                g_mainThreadId   = 0;
static DWORD                g_hotkeyThreadId = 0;
static HANDLE               g_hHotkeyThread  = nullptr;
static volatile bool        g_isDragging     = false;
static volatile bool        g_shouldDrop     = false;
static volatile bool        g_shouldCancel   = false;
static bool                 g_verbose        = false;

// ── Helpers to find topmost Windrop window across processes ──
struct TopWindropFinder
{
    HWND topHwnd = nullptr;
};

static BOOL CALLBACK EnumWindropWindowsProc(HWND hwnd, LPARAM lParam)
{
    wchar_t clsName[64] = {};
    if (GetClassNameW(hwnd, clsName, 64) && wcscmp(clsName, kCardClassName) == 0)
    {
        if (IsWindowVisible(hwnd))
        {
            auto* pFinder = reinterpret_cast<TopWindropFinder*>(lParam);
            if (!pFinder->topHwnd)
                pFinder->topHwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static HWND GetTopmostWindropWindow()
{
    TopWindropFinder finder;
    EnumWindows(EnumWindropWindowsProc, reinterpret_cast<LPARAM>(&finder));
    return finder.topHwnd;
}

static void WakeupDragLoop()
{
    if (g_mainThreadId)
        PostThreadMessageW(g_mainThreadId, WM_KEYDOWN, VK_ESCAPE, 0);

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[0].mi.dx = 1;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[1].mi.dx = -1;
    SendInput(2, inputs, sizeof(INPUT));
}

// ── Dedicated background thread for global hotkeys ──
// Runs its own message pump so hotkeys are never blocked by DoDragDrop's internal loop
static DWORD WINAPI HotkeyThreadProc(LPVOID)
{
    g_hotkeyThreadId = GetCurrentThreadId();

    auto doRegister = []() {
        RegisterHotKey(nullptr, kHotkeyF8, MOD_NOREPEAT, VK_F8);
        RegisterHotKey(nullptr, kHotkeyEsc, MOD_NOREPEAT, VK_ESCAPE);
    };

    auto doUnregister = []() {
        UnregisterHotKey(nullptr, kHotkeyF8);
        UnregisterHotKey(nullptr, kHotkeyEsc);
    };

    doRegister();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            if (msg.wParam == kHotkeyF8)
            {
                if (!g_isDragging)
                {
                    // Phase 1: User pressed F8 to engage drag at current cursor position
                    HWND targetWnd = GetTopmostWindropWindow();
                    if (targetWnd)
                    {
                        if (g_pState && targetWnd != g_pState->hwndCard)
                        {
                            doUnregister();
                            PostMessageW(targetWnd, WM_CLAIM_HOTKEY, 0, 0);
                        }
                        PostMessageW(targetWnd, WM_START_F8_DRAG, 0, 0);
                    }
                }
                else
                {
                    // Phase 2: User pressed F8 to drop!
                    g_shouldDrop = true;
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    WakeupDragLoop();
                }
            }
            else if (msg.wParam == kHotkeyEsc)
            {
                if (!g_isDragging)
                {
                    HWND targetWnd = GetTopmostWindropWindow();
                    if (targetWnd)
                    {
                        PostMessageW(targetWnd, WM_CLOSE, 0, 0);
                    }
                }
                else
                {
                    g_shouldCancel = true;
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    WakeupDragLoop();
                }
            }
        }
        else if (msg.message == WM_HOTKEY_CMD_REGISTER)
        {
            doRegister();
        }
        else if (msg.message == WM_HOTKEY_CMD_UNREGISTER)
        {
            doUnregister();
        }
        else if (msg.message == WM_QUIT)
        {
            break;
        }
    }

    doUnregister();
    return 0;
}

static void StartHotkeyThread()
{
    if (!g_hHotkeyThread)
    {
        g_hHotkeyThread = CreateThread(nullptr, 0, HotkeyThreadProc, nullptr, 0, &g_hotkeyThreadId);
    }
}

static void StopHotkeyThread()
{
    if (g_hHotkeyThread)
    {
        if (g_hotkeyThreadId)
            PostThreadMessageW(g_hotkeyThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_hHotkeyThread, 1000);
        CloseHandle(g_hHotkeyThread);
        g_hHotkeyThread = nullptr;
        g_hotkeyThreadId = 0;
    }
}

static void ClaimHotkeys()
{
    if (g_hotkeyThreadId)
        PostThreadMessageW(g_hotkeyThreadId, WM_HOTKEY_CMD_REGISTER, 0, 0);
}

// ── Core OLE Drag Execution ──
static void ExecuteOLEDrag(HWND hwnd)
{
    if (!g_pState || !g_pState->pDataObj || !g_pState->pDropSrc)
        return;

    ShowWindow(hwnd, SW_HIDE);
    g_isDragging   = true;
    g_shouldDrop   = false;
    g_shouldCancel = false;

    if (g_verbose)
        fwprintf(stderr, L"Dragging... hover over target window and press [F8] or left-click to drop ([Esc] to cancel).\n");
    else
        fwprintf(stderr, L"Dragging...\n");
    fflush(stderr);

    DWORD dwEffect = 0;
    DWORD dwOK     = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

    HRESULT hr = DoDragDrop(g_pState->pDataObj, g_pState->pDropSrc, dwOK, &dwEffect);

    g_isDragging = false;

    // Release synthesized mouse button if any
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

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

// ── Window Procedure for the visual Card ──
static LRESULT CALLBACK WindropCardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_START_F8_DRAG)
    {
        // 1. Move card directly under cursor so synthesized mouse down hits OUR window
        POINT pt = {};
        GetCursorPos(&pt);
        SetWindowPos(hwnd, HWND_TOPMOST, pt.x - 20, pt.y - 20, 40, 40, SWP_SHOWWINDOW);

        // 2. Synthesize left mouse down to initialize genuine MK_LBUTTON OLE drag state
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        return 0;
    }

    if (msg == WM_CLAIM_HOTKEY)
    {
        ClaimHotkeys();
        return 0;
    }

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

        // Dark Fluent acrylic style background
        HBRUSH bgBrush   = CreateSolidBrush(RGB(32, 33, 36));
        HPEN   borderPen = CreatePen(PS_SOLID, 2, RGB(0, 162, 237)); // Glowing Cyan Accent
        HBRUSH oldBrush  = static_cast<HBRUSH>(SelectObject(memDC, bgBrush));
        HPEN   oldPen    = static_cast<HPEN>(SelectObject(memDC, borderPen));

        RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 16, 16);

        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(bgBrush);
        DeleteObject(borderPen);

        // Draw 32x32 Shell Icon
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
            // Title (filename or summary)
            HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, hFontTitle));
            SetTextColor(memDC, RGB(248, 249, 250));
            RECT rcTitle = { 56, 14, rc.right - 14, 34 };
            DrawTextW(memDC, g_pState->displayTitle.c_str(), -1, &rcTitle,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            // Subtitle
            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, RGB(154, 160, 166));
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
        ExecuteOLEDrag(hwnd);
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
        if (wp == VK_F8)
        {
            PostMessageW(hwnd, WM_START_F8_DRAG, 0, 0);
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

    case WM_CLOSE:
    {
        wprintf(L"Drag canceled.\n");
        if (g_pState) g_pState->exitCode = EXIT_CANCELLED;
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
    {
        StopHotkeyThread();
        HWND nextWnd = GetTopmostWindropWindow();
        if (nextWnd && nextWnd != hwnd)
        {
            PostMessageW(nextWnd, WM_CLAIM_HOTKEY, 0, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Entry point ──
int wmain(int argc, wchar_t* argv[])
{
    std::vector<std::wstring> rawArgs;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"--version" || arg == L"-v" || arg == L"-V")
        {
            Utils::PrintVersion();
            return EXIT_OK;
        }
        if (arg == L"--help" || arg == L"-h" || arg == L"-?" || arg == L"/?")
        {
            Utils::PrintUsage(stdout);
            return EXIT_OK;
        }
        if (arg == L"--verbose")
        {
            g_verbose = true;
            continue;
        }
        rawArgs.emplace_back(arg);
    }

    if (rawArgs.empty()) { Utils::PrintUsage(stderr); return EXIT_BAD_ARGS; }

    g_mainThreadId = GetCurrentThreadId();

    // Enable Per-Monitor V2 DPI awareness
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    auto pfnSetDpi = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (pfnSetDpi)
        pfnSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::vector<std::wstring> missing;
    std::vector<std::wstring> files = Utils::ResolvePaths(rawArgs, missing);

    for (const auto& m : missing)
        Utils::PrintError(L"File not found: " + m);
    if (!missing.empty()) return EXIT_FILE_MISSING;
    if (files.empty())    { Utils::PrintUsage(stderr); return EXIT_BAD_ARGS; }

    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr))
    {
        Utils::PrintError(L"OleInitialize failed");
        return EXIT_COM_ERROR;
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Register Window Class for the card
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WindropCardWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kCardClassName;
    RegisterClassExW(&wc);

    WindropPayloadState state;
    state.files = files;
    state.pDataObj = new (std::nothrow) CDataObject(files);
    state.pDropSrc = new (std::nothrow) CDropSource(&g_shouldDrop, &g_shouldCancel);

    if (!state.pDataObj || !state.pDropSrc)
    {
        Utils::PrintError(L"Out of memory allocating COM objects");
        if (state.pDropSrc) state.pDropSrc->Release();
        if (state.pDataObj) state.pDataObj->Release();
        UnregisterClassW(kCardClassName, hInst);
        OleUninitialize();
        return EXIT_COM_ERROR;
    }

    // Extract Shell Icon
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
    state.displaySubtitle = L"Drag or [F8] Engage \x2022 Right-Click Exit";

    g_pState = &state;

    // Count existing Windrop windows to cascade position
    struct CascadeCount { int count = 0; } cascade;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        wchar_t cls[64] = {};
        if (GetClassNameW(h, cls, 64) && wcscmp(cls, kCardClassName) == 0)
            reinterpret_cast<CascadeCount*>(lp)->count++;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&cascade));

    // Positioning
    POINT pt;
    GetCursorPos(&pt);

    const int kWidth  = 280;
    const int kHeight = 70;
    int offset = (cascade.count % 6) * 30;
    int posX = pt.x - 20 + offset;
    int posY = pt.y - 20 + offset;

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
        kCardClassName, L"windrop",
        WS_POPUP,
        posX, posY, kWidth, kHeight,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd)
    {
        Utils::PrintError(L"Failed to create windrop payload window");
        state.pDropSrc->Release();
        state.pDataObj->Release();
        if (state.hIcon) DestroyIcon(state.hIcon);
        UnregisterClassW(kCardClassName, hInst);
        OleUninitialize();
        return EXIT_COM_ERROR;
    }

    state.hwndCard = hwnd;

    // Apply rounded window region
    HRGN hRgn = CreateRoundRectRgn(0, 0, kWidth + 1, kHeight + 1, 16, 16);
    SetWindowRgn(hwnd, hRgn, TRUE);

    // Attach Shell Drag Image to card for direct mouse drags
    DragImage::AttachShellImage(hwnd, files, state.pDataObj);

    // Start dedicated hotkey thread for F8 and Escape (safe alternative to low-level keyboard hooks)
    StartHotkeyThread();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    if (g_verbose)
    {
        fwprintf(stderr, L"\nwindrop ready\n\n");
        fwprintf(stderr, L"Controls:\n");
        fwprintf(stderr, L"  - Mouse:    Drag the floating card directly into any window\n");
        fwprintf(stderr, L"  - Keyboard: Hover over target window and press [F8] to drag -> press [F8] or left-click to drop\n");
        fwprintf(stderr, L"  - Dismiss:  Press [Esc] or right-click on card\n\n");
    }

    fwprintf(stderr, L"Payload (%zu file%s):\n", files.size(), files.size() == 1 ? L"" : L"s");
    for (const auto& f : files)
        fwprintf(stderr, L"  - %s\n", f.c_str());
    fwprintf(stderr, L"\n");
    fflush(stderr);

    // Message pump
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopHotkeyThread();

    if (state.hIcon)
        DestroyIcon(state.hIcon);

    state.pDropSrc->Release();
    state.pDataObj->Release();
    UnregisterClassW(kCardClassName, hInst);
    OleUninitialize();

    return state.exitCode;
}
