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
   ├─► CDropSource initializes atomic drop/cancel flags
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
