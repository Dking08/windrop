// drag.exe — CLI-initiated Windows drag-and-drop
//
// Usage:  drag <file1> [file2 ...]
// Flow:   F8 → drag starts → move cursor → F8 → drop

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
enum class TriggerState { Waiting, Go, Abort };
static volatile TriggerState g_trigger      = TriggerState::Waiting;
static volatile bool         g_isDragging   = false;
static volatile bool         g_shouldDrop   = false;
static volatile bool         g_shouldCancel = false;
static DWORD                 g_mainThreadId = 0;

// Forward declaration — DoDragDrop is called from inside WndProc
static IDataObject* g_pDataObj = nullptr;
static IDropSource* g_pDropSrc = nullptr;
static HRESULT      g_dragResult = E_FAIL;
static DWORD        g_dwEffect   = 0;

// ── LL keyboard hook ──
//
// KEY INSIGHT from ReactOS/Wine DoDragDrop source code:
//
// DoDragDrop internally runs:
//   while (!done && GetMessageW(&msg, 0, 0, 0)) {
//       if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) {
//           if (msg.wParam == VK_ESCAPE) escPressed = TRUE;
//           OLEDD_TrackStateChange();  // ← calls QueryContinueDrag
//       } else {
//           DispatchMessageW(&msg);    // handles WM_MOUSEMOVE etc
//       }
//   }
//
// QueryContinueDrag is ONLY called when a keyboard message arrives in
// the thread's message queue. Mouse moves do NOT trigger it.
//
// Our LL hook fires BEFORE the message reaches any queue. If we return 1
// (swallow), the WM_KEYDOWN never enters our thread's queue, and
// DoDragDrop never calls QueryContinueDrag.
//
// FIX: When F8 is pressed during drag, we:
//   1. Set g_shouldDrop = true
//   2. PostThreadMessage(WM_KEYDOWN, VK_ESCAPE) to our own thread
//      This injects a keyboard message into the queue that DoDragDrop's
//      GetMessageW will pick up, triggering QueryContinueDrag.
//   3. QueryContinueDrag checks g_shouldDrop FIRST → returns DRAGDROP_S_DROP
//
// For Escape: same pattern but sets g_shouldCancel instead.
//
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
    {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (!g_isDragging)
        {
            // Phase 1: waiting for user to press F8 to start
            if (g_trigger == TriggerState::Waiting)
            {
                if (kb->vkCode == VK_F8)
                { g_trigger = TriggerState::Go; PostQuitMessage(0); return 1; }
                if (kb->vkCode == VK_ESCAPE)
                { g_trigger = TriggerState::Abort; PostQuitMessage(0); return 1; }
            }
            // Between phases: just swallow F8 silently
            if (kb->vkCode == VK_F8) return 1;
        }
        else
        {
            // Phase 2: drag is active
            if (kb->vkCode == VK_F8)
            {
                g_shouldDrop = true;
                // Inject a WM_KEYDOWN into our thread's message queue.
                // DoDragDrop's GetMessageW will pick this up and call
                // QueryContinueDrag, which checks g_shouldDrop.
                PostThreadMessageW(g_mainThreadId, WM_KEYDOWN, VK_ESCAPE, 0);
                return 1; // swallow the real F8
            }
            if (kb->vkCode == VK_ESCAPE)
            {
                g_shouldCancel = true;
                // Same trick: inject WM_KEYDOWN so DoDragDrop calls
                // QueryContinueDrag where we check g_shouldCancel.
                PostThreadMessageW(g_mainThreadId, WM_KEYDOWN, VK_ESCAPE, 0);
                return 1; // swallow the real Escape
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── Custom message to trigger the drag from WndProc context ──
#define WM_START_DRAG (WM_APP + 1)

static const wchar_t* kWindowClassName = L"DragUtilityHelperWnd";

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_START_DRAG)
    {
        DWORD dwOK = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
        g_dragResult = DoDragDrop(g_pDataObj, g_pDropSrc, dwOK, &g_dwEffect);
        PostQuitMessage(0);
        return 0;
    }
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
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        kWindowClassName, L"",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, hInst, nullptr);

    if (hwnd)
        SetLayeredWindowAttributes(hwnd, 0, 1, LWA_ALPHA);
    return hwnd;
}

// ── Entry point ──
int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) { Utils::PrintUsage(); return EXIT_BAD_ARGS; }

    g_mainThreadId = GetCurrentThreadId();

    std::vector<std::wstring> rawArgs;
    for (int i = 1; i < argc; ++i)
        rawArgs.emplace_back(argv[i]);

    std::vector<std::wstring> missing;
    std::vector<std::wstring> files = Utils::ResolvePaths(rawArgs, missing);

    for (const auto& m : missing)
        Utils::PrintError(L"File not found: " + m);
    if (!missing.empty()) return EXIT_FILE_MISSING;
    if (files.empty())    { Utils::PrintUsage(); return EXIT_BAD_ARGS; }

    fwprintf(stderr, L"Ready to drag %zu file(s):\n", files.size());
    for (const auto& f : files)
        fwprintf(stderr, L"  %s\n", f.c_str());
    fwprintf(stderr, L"\n");

    // ── Install LL keyboard hook (stays for entire lifetime) ──
    HHOOK hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                     GetModuleHandleW(nullptr), 0);
    if (!hHook)
    {
        fwprintf(stderr, L"ERROR: SetWindowsHookEx failed (%lu)\n", GetLastError());
        return EXIT_COM_ERROR;
    }

    // ── Phase 1: wait for F8 ──
    fwprintf(stderr, L"Hover cursor over the drop target, then press F8 to drag.\n");
    fwprintf(stderr, L"Press Escape to abort.\n");
    fflush(stderr);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    { TranslateMessage(&msg); DispatchMessageW(&msg); }

    if (g_trigger != TriggerState::Go)
    {
        UnhookWindowsHookEx(hHook);
        fwprintf(stderr, L"Aborted.\n");
        return EXIT_CANCELLED;
    }

    // Immediately enter dragging phase to close the state machine gap
    g_isDragging = true;

    // Wait for F8 to be physically released (debounce)
    while (GetAsyncKeyState(VK_F8) & 0x8000) Sleep(10);
    Sleep(100);

    // Reset flags — any F8 presses during debounce are discarded
    g_shouldDrop   = false;
    g_shouldCancel = false;

    // Drain stale messages
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {}

    // ── Phase 2: OLE drag ──
    fwprintf(stderr, L"\nDragging... move cursor to target and press F8 to drop.\n");
    fwprintf(stderr, L"Press Escape to cancel.\n");
    fflush(stderr);

    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr))
    {
        UnhookWindowsHookEx(hHook);
        Utils::PrintError(L"OleInitialize failed");
        return EXIT_COM_ERROR;
    }

    int exitCode = EXIT_COM_ERROR;
    {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        HWND hwnd = CreateHelperWindow(hInst);
        if (!hwnd) { OleUninitialize(); UnhookWindowsHookEx(hHook); return EXIT_COM_ERROR; }

        // Position at cursor and show
        POINT pt = {};
        GetCursorPos(&pt);
        SetWindowPos(hwnd, HWND_TOPMOST, pt.x, pt.y, 1, 1,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        g_pDataObj = new (std::nothrow) CDataObject(files);
        g_pDropSrc = new (std::nothrow) CDropSource(&g_shouldDrop, &g_shouldCancel);
        if (!g_pDataObj || !g_pDropSrc)
        {
            if (g_pDataObj) g_pDataObj->Release();
            if (g_pDropSrc) g_pDropSrc->Release();
            DestroyWindow(hwnd); OleUninitialize(); UnhookWindowsHookEx(hHook);
            return EXIT_COM_ERROR;
        }

        DragImage::AttachShellImage(hwnd, files, g_pDataObj);

        // Trigger DoDragDrop via a posted message so it runs inside
        // DispatchMessage on our thread — the proper context.
        PostMessageW(hwnd, WM_START_DRAG, 0, 0);

        // Run message loop — DoDragDrop blocks inside WM_START_DRAG.
        // Our LL hook fires and uses PostThreadMessage to inject
        // WM_KEYDOWN into this thread's queue, which DoDragDrop's
        // internal GetMessageW picks up.
        while (GetMessageW(&msg, nullptr, 0, 0))
        { TranslateMessage(&msg); DispatchMessageW(&msg); }

        g_isDragging = false;

        hr = g_dragResult;
        if      (hr == DRAGDROP_S_DROP)   { wprintf(L"Drop completed.\n"); exitCode = EXIT_OK; }
        else if (hr == DRAGDROP_S_CANCEL) { wprintf(L"Drag canceled.\n");  exitCode = EXIT_CANCELLED; }
        else { fwprintf(stderr, L"DoDragDrop=0x%08lX\n", (unsigned long)hr); exitCode = EXIT_COM_ERROR; }

        g_pDropSrc->Release();
        g_pDataObj->Release();
        DestroyWindow(hwnd);
        UnregisterClassW(kWindowClassName, hInst);
    }

    OleUninitialize();
    UnhookWindowsHookEx(hHook);
    return exitCode;
}
