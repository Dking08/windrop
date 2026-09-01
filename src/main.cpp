// drag.exe — CLI-initiated Windows drag-and-drop
//
// Usage:  drag <file1> [file2 ...]
// Flow:   drag starts immediately → move cursor → F8 (or Left Click) → drop

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
#include <objbase.h>
#include <shlobj.h>
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

// ── Shared state ──
static volatile bool g_isDragging   = false;
static volatile bool g_shouldDrop   = false;
static volatile bool g_shouldCancel = false;
static volatile bool g_workerStop   = false;
static DWORD         g_mainThreadId = 0;

static void TriggerDrop()
{
    g_shouldDrop = true;
    if (g_mainThreadId)
        PostThreadMessageW(g_mainThreadId, WM_KEYDOWN, VK_ESCAPE, 0);

    // Stir mouse queue
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[0].mi.dx = 1;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[1].mi.dx = -1;
    SendInput(2, inputs, sizeof(INPUT));
}

static void TriggerCancel()
{
    g_shouldCancel = true;
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

// ── Background monitor thread ──
//
// 1. Sends micro relative mouse moves to keep drop targets (Explorer, Chrome,
//    Electron apps) continuously primed with DragOver events even when motionless.
// 2. Polls physical key and mouse states as a fail-safe fallback in case Windows
//    silently unhooks the low-level hook due to timeouts.
static DWORD WINAPI DragMonitorThreadProc(LPVOID)
{
    bool toggle = false;
    bool lbuttonSeen = false;
    int tick = 0;
    while (!g_workerStop && g_isDragging)
    {
        // High-frequency polling (every 15ms)
        if ((GetAsyncKeyState(VK_F8) & 0x8000) || (GetAsyncKeyState(VK_F8) & 0x0001) ||
            (GetAsyncKeyState(VK_RETURN) & 0x8000))
        {
            TriggerDrop();
        }
        else if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) || (GetAsyncKeyState(VK_ESCAPE) & 0x0001))
        {
            TriggerCancel();
        }

        // Track left mouse button release
        bool lbtnDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (lbtnDown)
        {
            lbuttonSeen = true;
        }
        else if (lbuttonSeen)
        {
            lbuttonSeen = false;
            TriggerDrop();
        }

        // Send mouse jiggle every ~90ms (every 6 ticks)
        if (++tick % 6 == 0)
        {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            input.mi.dx = toggle ? 1 : -1;
            input.mi.dy = 0;
            SendInput(1, &input, sizeof(INPUT));
            toggle = !toggle;
        }

        Sleep(15);
    }
    return 0;
}

// ── Low-level keyboard hook ──
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
    {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (g_isDragging)
        {
            if (kb->vkCode == VK_F8 || kb->vkCode == VK_RETURN)
            {
                TriggerDrop();
                return 1; // swallow F8 / Enter
            }
            if (kb->vkCode == VK_ESCAPE)
            {
                TriggerCancel();
                return 1; // swallow Escape
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── Helper window for OLE mouse capture ──
static const wchar_t* kWindowClassName = L"DragUtilityHelperWnd";

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT; // Mouse hit-tests pass straight through to target app beneath
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND CreateHelperWindow(HINSTANCE hInst)
{
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kWindowClassName, L"",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, hInst, nullptr);

    if (hwnd)
    {
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA); // Completely invisible
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    return hwnd;
}

// ── Entry point ──
int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) { Utils::PrintUsage(); return EXIT_BAD_ARGS; }

    // Enable Per-Monitor V2 DPI awareness if available
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    auto pfnSetDpi = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
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
    HWND hwndHelper = CreateHelperWindow(hInst);

    // Wait for any prior F8 keypress to be physically released
    while (GetAsyncKeyState(VK_F8) & 0x8000) Sleep(10);

    g_mainThreadId = GetCurrentThreadId();
    g_shouldDrop   = false;
    g_shouldCancel = false;
    g_workerStop   = false;
    g_isDragging   = true;

    // Install LL keyboard hook
    HHOOK hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                     hInst, 0);
    if (!hHook)
    {
        fwprintf(stderr, L"Warning: SetWindowsHookEx failed (%lu), using polling fallback.\n", GetLastError());
    }

    // Start background monitor / jiggle thread
    HANDLE hThread = CreateThread(nullptr, 0, DragMonitorThreadProc, nullptr, 0, nullptr);

    auto* pDataObj = new (std::nothrow) CDataObject(files);
    auto* pDropSrc = new (std::nothrow) CDropSource(&g_shouldDrop, &g_shouldCancel);

    int exitCode = EXIT_COM_ERROR;

    if (pDataObj && pDropSrc)
    {
        DragImage::AttachShellImage(nullptr, files, pDataObj);

        fwprintf(stderr, L"Dragging %zu file(s)... Move cursor over target and press F8 (or Left Click) to drop.\n", files.size());
        for (const auto& f : files)
            fwprintf(stderr, L"  %s\n", f.c_str());
        fwprintf(stderr, L"Press Escape to cancel.\n\n");
        fflush(stderr);

        DWORD dwEffect = 0;
        DWORD dwOK = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

        hr = DoDragDrop(pDataObj, pDropSrc, dwOK, &dwEffect);

        if (hr == DRAGDROP_S_DROP)
        {
            if (dwEffect == DROPEFFECT_NONE)
            {
                wprintf(L"Drop rejected by target.\n");
                exitCode = EXIT_CANCELLED;
            }
            else if (dwEffect & DROPEFFECT_MOVE)
            {
                wprintf(L"Drop completed (Move).\n");
                exitCode = EXIT_OK;
            }
            else if (dwEffect & DROPEFFECT_COPY)
            {
                wprintf(L"Drop completed (Copy).\n");
                exitCode = EXIT_OK;
            }
            else
            {
                wprintf(L"Drop completed.\n");
                exitCode = EXIT_OK;
            }
        }
        else if (hr == DRAGDROP_S_CANCEL)
        {
            wprintf(L"Drag canceled.\n");
            exitCode = EXIT_CANCELLED;
        }
        else
        {
            fwprintf(stderr, L"DoDragDrop failed (0x%08lX)\n", (unsigned long)hr);
            exitCode = EXIT_COM_ERROR;
        }
    }
    else
    {
        Utils::PrintError(L"Out of memory allocating COM objects");
    }

    g_isDragging = false;
    g_workerStop = true;

    if (hThread)
    {
        WaitForSingleObject(hThread, 1000);
        CloseHandle(hThread);
    }

    if (pDropSrc) pDropSrc->Release();
    if (pDataObj) pDataObj->Release();

    if (hHook)
        UnhookWindowsHookEx(hHook);

    if (hwndHelper)
        DestroyWindow(hwndHelper);

    UnregisterClassW(kWindowClassName, hInst);

    OleUninitialize();
    return exitCode;
}
