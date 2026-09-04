# windrop Architecture

Technical overview of the `windrop` codebase and Windows OLE drag-and-drop orchestration.

---

## File Structure

```
windrop/
├── CMakeLists.txt          # Modern C++17 build configuration (/W4 /WX strict)
└── src/
    ├── main.cpp            # Entry point, UI card window, OLE DoDragDrop loop
    ├── DropSource.cpp/.h   # IDropSource implementation & drop query state
    ├── DataObject.cpp/.h   # IDataObject multi-format shell clipboard provider
    ├── DragImage.cpp/.h    # IDragSourceHelper shell thumbnail preview renderer
    └── Utils.cpp/.h        # Wildcard expansion and path resolution
```

---

## Technical Workflow

```
1. CLI Invocation: windrop *.png
   │
   ├─► Utils::ResolvePaths expands globs and verifies file existence
   ├─► OleInitialize() initializes COM Single-Threaded Apartment (STA)
   ├─► CDataObject builds multi-format payload (HDROP, text, URL, effects)
   ├─► CDropSource initializes drop/cancel volatile flags
   ├─► HotkeyThread starts and registers F8/Esc via RegisterHotKey(MOD_NOREPEAT)
   └─► Creates top-level floating card widget with 32x32 shell icon
   │
2. Drag Execution
   │
   ├─► Mouse Mode: WM_LBUTTONDOWN initiates DoDragDrop natively
   └─► Keyboard Mode: [F8] snaps card to cursor and initiates synchronized
       WM_LBUTTONDOWN to provide genuine MK_LBUTTON OLE mouse capture
   │
3. Target Window Negotiation
   │
   ├─► Target application receives DragEnter and DragOver
   ├─► Target displays visual drop cue ("Drop files here" / "+ Copy" / "Upload")
   │
4. Drop Commitment
   │
   ├─► 2nd [F8], Left-Click, or Mouse Release triggers DRAGDROP_S_DROP
   ├─► Target window executes IDropTarget::Drop() and ingests payload
   └─► windrop.exe outputs "Drop completed (Copy)." and exits with code 0
```

---

## Hotkey Architecture (Keylogger-Free & Safe)

Earlier versions relied on low-level keyboard hooks (`SetWindowsHookExW(WH_KEYBOARD_LL)`), which heuristic antivirus scanners (including Windows Defender) often falsely flag as keylogger behavior.

Starting with **v2.1.0**, `windrop` uses official Win32 `RegisterHotKey`:
- **Dedicated Worker Thread (`HotkeyThread`)**: `DoDragDrop` runs an internal modal message loop that selectively filters out `WM_HOTKEY` (`0x0312`). To avoid message starvation during active drag, hotkeys are registered and pumped on a dedicated worker thread with an uninhibited message queue.
- **`MOD_NOREPEAT` Protection**: Prevents OS autorepeat while a key is physically held down, ensuring clean 2-step transitions (1st `F8` engage &rarr; 2nd `F8` drop).
- **Multi-Card Cascade Hand-off**: When multiple cards are staged, `WM_CLAIM_HOTKEY` dynamically re-routes hotkey ownership to the topmost card.

---

## Reproducible Builds

`windrop` is configured with deterministic MSVC flags in `CMakeLists.txt`:
- `/experimental:deterministic`: Emits reproducible object files.
- `/Brepro`: Emits deterministic PE timestamps and checksums.
- `/PDBALTPATH:%_PDB%`: Prevents leaking local user paths into debug headers.
- `/INCREMENTAL:NO`: Guarantees bit-for-bit identical binary hashes across clean builds.
