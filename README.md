# drag.exe — CLI Windows Drag-and-Drop Utility

> **The `dragon` / `blobdrop` alternative for Windows.**  
> Initiate a **native Windows OLE drag-and-drop operation directly from your terminal**.

```powershell
PS C:\Stuff> drag report.pdf
# → A sleek floating card appears at your cursor
# → Click and drag directly into Discord, Chrome, VS Code, Explorer, Slack, etc.
Drop completed (Copy).
```

---

## Features

- 🎴 **Floating Drag Card**: Summons a lightweight, dark-acrylic floating card at your cursor displaying the file's native 32x32 shell icon and filename.
- 🚀 **100% Native Windows OLE**: Initiates standard Windows OLE `DoDragDrop` directly on mouse interaction with 0 terminal interference or text-selection conflicts.
- 📦 **Multi-Format Shell Payload**:
  - `CF_HDROP` (Native file lists for Explorer, 7-Zip, Discord, Slack)
  - `CFSTR_PREFERREDDROPEFFECT` (`DROPEFFECT_COPY | DROPEFFECT_MOVE`)
  - `CF_UNICODETEXT` & `CF_TEXT` (Newline-separated file paths for text editors & terminals)
  - `CFSTR_SHELLURL` (`file:///` URLs for web browsers & web apps)
- 🖼️ **Shell Drag Thumbnail**: Automatically renders a translucent shell thumbnail preview using `IDragSourceHelper2`.
- ⚡ **Instant Auto-Exit**: Closes and returns to the command line immediately upon drop completion or cancellation.
- ⌨️ **Keyboard Controls**: Press `Escape` or Right-Click anywhere to dismiss; press `F8`, `Enter`, or `Space` to initiate drag.
- 🖥️ **Per-Monitor V2 DPI Aware**: Sharp rendering on 4K / high-DPI displays.

---

## Quick Start / Build Instructions

### Requirements
- Windows 10 / 11 (x64)
- Visual Studio 2019 or 2022 (C++ Desktop workload)
- CMake 3.16+

### Build & Install (PowerShell)

You can build and install directly using the included PowerShell script:

```powershell
.\bcmd.ps1
```

Or build manually via CMake:

```powershell
# Configure & build Release binary
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Output binary located at:
#   build\Release\drag.exe
```

---

## Usage

```powershell
drag <files...>
```

### Examples

```powershell
# Drag a single file
drag photo.png

# Drag multiple files
drag report.pdf notes.txt data.csv

# Wildcards
drag *.jpg

# Paths with spaces
drag "C:\My Documents\Quarterly Report.xlsx"

# Relative paths
drag .\dist\bundle.js
```

### Keyboard & Mouse Controls

| Action | Control |
| :--- | :--- |
| **Start Drag & Drop** | Click & drag the floating card with Left Mouse Button |
| **Alternate Drag Trigger** | Press `F8`, `Enter`, or `Space` while card is focused |
| **Cancel & Exit** | Press `Escape` or Right-Click the card |

### Exit Codes

| Code | Meaning |
| :---: | :--- |
| `0` | Drop completed successfully (`EXIT_OK`) |
| `1` | Invalid arguments / no files specified (`EXIT_BAD_ARGS`) |
| `2` | One or more files not found (`EXIT_FILE_MISSING`) |
| `3` | Drag cancelled (`EXIT_CANCELLED`) |
| `4` | COM / internal initialization error (`EXIT_COM_ERROR`) |

---

## Architecture & How It Works

```
drag/
├── CMakeLists.txt          # Modern C++17 build configuration (/W4 /WX strict)
├── bcmd.ps1                # Build & deployment script
└── src/
    ├── main.cpp            # Entry point, Win32 GUI card, DoDragDrop orchestration
    ├── DropSource.cpp/.h   # IDropSource — OLE drag loop state & feedback
    ├── DataObject.cpp/.h   # IDataObject — multi-format shell clipboard provider
    ├── DragImage.cpp/.h    # IDragSourceHelper — shell drag preview renderer
    └── Utils.cpp/.h        # Path canonicalization & wildcard resolution
```

### Execution Flow

```
1. CLI invocation: drag *.png
   │
   ├─► Utils::ResolvePaths expands globs and verifies file existence
   ├─► OleInitialize() initializes COM Single-Threaded Apartment (STA)
   ├─► CDataObject builds multi-format payload (HDROP, text, URL, effects)
   ├─► Extract high-res 32x32 shell icon via SHGetFileInfoW
   │
2. Floating Card Window (DragPayloadCardWnd)
   │
   ├─► Positioned at cursor on the active display monitor
   ├─► Renders dark acrylic card (Segoe UI typography, icon, accent border)
   │
3. User Clicks & Drags
   │
   ├─► Card hides (ShowWindow(SW_HIDE))
   ├─► DoDragDrop() takes over with native Windows OLE mouse capture
   ├─► IDragSourceHelper renders translucent drag thumbnail
   │
4. Target Application Interaction
   │
   ├─► Target window receives DragEnter / DragOver with full MK_LBUTTON state
   ├─► Target displays visual drop indicator ("Drop here" / "+ Copy" / "Upload")
   │
5. Drop Completion
   │
   ├─► User releases mouse button over target
   ├─► Target executes IDropTarget::Drop() and ingests payload
   ├─► DoDragDrop() returns DRAGDROP_S_DROP
   └─► drag.exe prints "Drop completed (Copy)." and exits (code 0)
```

---

## Target Compatibility

| Target Application | Supported Payload | Status |
| :--- | :--- | :---: |
| **Discord / Slack / Teams** | `CF_HDROP` / `CFSTR_SHELLURL` | ✅ Full Support |
| **Chrome / Edge / Firefox** | File upload inputs, Gmail, web apps | ✅ Full Support |
| **VS Code / Cursor / IDEs** | Editor split / file explorer drop | ✅ Full Support |
| **Windows File Explorer** | Copy / Move file operations | ✅ Full Support |
| **Text Editors / Notepad** | `CF_UNICODETEXT` (file paths) | ✅ Full Support |
| **Adobe Suite / Photoshop / GIMP** | Image drag ingestion | ✅ Full Support |

---

## Troubleshooting

- **Globs / Wildcards in PowerShell**:
  In PowerShell, `drag *.png` might be expanded before reaching `drag.exe`. If you encounter path parsing issues with complex expressions, wrap in quotes: `drag "*.png"`.

- **Headless / SSH Sessions**:
  Windows OLE drag-and-drop requires an active, interactive desktop window station. It cannot run across headless SSH sessions without a desktop display.
