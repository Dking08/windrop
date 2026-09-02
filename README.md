# windrop

A lightweight CLI drag-and-drop utility for Windows (`dragon` / `blobdrop` alternative).

**100% Native Win32 / OLE** &bull; **Single Standalone Binary** &bull; **Peak Working Set < 3MB** &bull; **Zero Background Bloat / Zero Services**

https://github.com/user-attachments/assets/36948fa9-7e54-4ef4-8b24-8652923e40c3

`windrop` lets you initiate native Windows drag-and-drop operations directly from your terminal or scripts without running background daemons or heavy electron wrappers.

```powershell
PS C:\Stuff> windrop report.pdf
# Move cursor over Discord, Chrome, VS Code, Explorer, Slack, etc.
# Press [F8] to drag -> press [F8] to drop!
Drop completed (Copy).
```

---

## How to Use

### 1. The Command
```powershell
windrop <files...>
```

```powershell
# Single file
windrop photo.png

# Multiple files
windrop report.pdf notes.txt data.csv

# Wildcards & spaces
windrop *.jpg "C:\My Documents\report.xlsx"
```

### 2. Controls & Actions

| Action | How to Trigger |
| :--- | :--- |
| **Keyboard Drag & Drop** | Hover cursor over target window and press **`F8`** to drag &rarr; press **`F8`** again to drop |
| **Mouse Drag & Drop** | Grab and drag the floating card widget directly into any window |
| **Dismiss / Cancel** | Press **`Escape`** or **Right-Click** the card anytime |

---

## Features

- **True Standalone Binary**: Pure C++17 compiled against native Windows APIs (`ole32`, `shell32`, `user32`). Starts instantly, exits immediately after drop, uses 0 background resources.
- **2-Step Keyboard Drag (`F8`)**: Hover over any destination window, press **`F8`** to engage drag (target lights up with visual drop cues), and press **`F8`** again to drop.
- **Floating Acrylic Card**: Sleek dark card with 32x32 native shell file icon and thumbnail preview for direct mouse drags.
- **Multi-Widget Staging**: Run `windrop` multiple times from your CLI &mdash; cards automatically cascade (`+30px` offset) across your desktop.
- **Multi-Format Shell Payload**:
  - `CF_HDROP`: Native shell file lists for File Explorer, 7-Zip, Discord, Slack.
  - `CFSTR_PREFERREDDROPEFFECT`: Explicit copy / move negotiation.
  - `CF_UNICODETEXT` & `CF_TEXT`: Newline-separated file paths for text editors, IDEs, and terminals.
  - `CFSTR_SHELLURL`: `file:///` URLs for web browsers and Electron web applications.
- **Per-Monitor V2 DPI Aware**: Crisp, sharp rendering on 4K and high-DPI displays.

---

## Target Compatibility

| Application | Supported Drops | Status |
| :--- | :--- | :---: |
| **Discord / Slack / Teams** | File uploads, channel attachment sharing | Full Support |
| **Chrome / Edge / Firefox / Brave** | File upload inputs, Gmail, web applications | Full Support |
| **VS Code / Cursor / Visual Studio** | Editor split view, file explorer drop | Full Support |
| **Windows File Explorer** | File copy and move operations | Full Support |
| **Notepad / Sublime / Text Editors** | File paths pasting | Full Support |
| **Photoshop / Illustrator / GIMP** | Direct image canvas drop | Full Support |

---

## Build Instructions

### Requirements
- Windows 10 / 11 (x64)
- Visual Studio 2019 or 2022 (C++ Desktop workload)
- CMake 3.16+

### Build via PowerShell (2 lines)

```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The compiled binary will be located at:
```
build/Release/windrop.exe
```

---

## Technical Details

For internal architecture, COM data object implementation, and state machine diagrams, see **[Architecture.md](Architecture.md)**.

---

## License

[MIT License](LICENSE). Copyright &copy; 2026 Dastageer Siddiqui.
