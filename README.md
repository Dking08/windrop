# windrop.exe — CLI Windows Drag-and-Drop Utility

> **The `dragon` / `blobdrop` alternative for Windows.**  
> Initiate a **native Windows OLE drag-and-drop operation directly from your terminal**.

```powershell
PS C:\Stuff> windrop report.pdf
# → Native OLE drag is initiated with your cursor
# → Hover over Discord, Chrome, VS Code, Explorer, Slack, etc.
# → Press [F8] (or Left Click) to drop!
Drop completed (Copy).
```

---

## Features

- 🚀 **100% Native Windows OLE**: Standard Windows OLE `DoDragDrop` protocol with live `DragEnter`, `DragOver`, and `Drop` target negotiation.
- 🎯 **F8 Quick-Drop**: Move your mouse cursor anywhere on your screen and press **`F8`** to drop into whatever window is under the cursor.
- 🖱️ **Physical Click-to-Drop**: Releasing Left Mouse Button over any drop target also completes the drop.
- 📦 **Multi-Format Shell Payload**:
  - `CF_HDROP` (Native file lists for Explorer, 7-Zip, Discord, Slack)
  - `CFSTR_PREFERREDDROPEFFECT` (`DROPEFFECT_COPY | DROPEFFECT_MOVE`)
  - `CF_UNICODETEXT` & `CF_TEXT` (Newline-separated file paths for text editors & terminals)
  - `CFSTR_SHELLURL` (`file:///` URLs for web browsers & web apps)
- 🖼️ **Shell Drag Thumbnail**: Automatically renders a translucent shell thumbnail preview using `IDragSourceHelper2`.
- ⚡ **Instant Auto-Exit**: Closes and returns to the command line immediately upon drop completion or cancellation.
- ⌨️ **Keyboard Controls**: Press **`Escape`** anywhere to cancel; press **`F8`** to drop.
- 🖥️ **Per-Monitor V2 DPI Aware**: Crisp rendering on 4K / high-DPI displays.

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
#   build\Release\windrop.exe
```

---

## Usage

```powershell
windrop <files...>
```

### Examples

```powershell
# Drag a single file
windrop photo.png

# Drag multiple files
windrop report.pdf notes.txt data.csv

# Wildcards
windrop *.jpg

# Paths with spaces
windrop "C:\My Documents\Quarterly Report.xlsx"

# Relative paths
windrop .\dist\bundle.js
```

### Controls

| Action | Control |
| :--- | :--- |
| **Drop payload into target** | Hover cursor over target and press **`F8`** (or Left Click) |
| **Cancel & Exit** | Press **`Escape`** |

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
windrop/
├── CMakeLists.txt          # Modern C++17 build configuration (/W4 /WX strict)
├── bcmd.ps1                # Build & deployment script
└── src/
    ├── main.cpp            # Entry point, OLE message pump, DoDragDrop orchestration
    ├── DropSource.cpp/.h   # IDropSource — OLE drag loop state & feedback
    ├── DataObject.cpp/.h   # IDataObject — multi-format shell clipboard provider
    ├── DragImage.cpp/.h    # IDragSourceHelper — shell drag preview renderer
    └── Utils.cpp/.h        # Path canonicalization & wildcard resolution
```

### Execution Flow

```
1. CLI invocation: windrop *.png
   │
   ├─► Utils::ResolvePaths expands globs and verifies file existence
   ├─► OleInitialize() initializes COM Single-Threaded Apartment (STA)
   ├─► CDataObject builds multi-format payload (HDROP, text, URL, effects)
   ├─► CDropSource initialized with atomic drop/cancel flags
   │
2. DoDragDrop Active OLE Loop
   │
   ├─► Transparent click-through helper window manages OLE desktop capture
   ├─► Background jiggle thread pulses micro-moves to keep DragOver active
   ├─► Shell thumbnail follows cursor
   │
3. Target Window Negotiation
   │
   ├─► Target window under cursor receives DragEnter and DragOver
   ├─► Target application shows visual drop cues ("Drop here" / "+ Copy" / "Upload")
   │
4. Drop Trigger (F8 or Left Click)
   │
   ├─► Low-level keyboard hook catches F8 → sets shouldDrop = true
   ├─► QueryContinueDrag returns DRAGDROP_S_DROP
   ├─► Target executes IDropTarget::Drop() and ingests files
   └─► windrop.exe prints "Drop completed (Copy)." and exits (code 0)
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
  In PowerShell, `windrop *.png` might be expanded before reaching `windrop.exe`. If you encounter path parsing issues with complex expressions, wrap in quotes: `windrop "*.png"`.

- **Headless / SSH Sessions**:
  Windows OLE drag-and-drop requires an active, interactive desktop window station. It cannot run across headless SSH sessions without a desktop display.
