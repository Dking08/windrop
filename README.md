# windrop

CLI Windows Drag-and-Drop Utility (dragon / blobdrop alternative for Windows).

`windrop` allows you to initiate native Windows OLE drag-and-drop operations directly from your terminal, command line, or scripts.

```powershell
PS C:\Stuff> windrop report.pdf
# Move cursor over target window (Discord, Chrome, VS Code, Explorer, Slack, etc.)
# Press [F8] to drag -> press [F8] to drop!
Drop completed (Copy).
```

---

## Features

- **100% Native Windows OLE**: Implements standard Windows OLE `DoDragDrop` protocol with live `DragEnter`, `DragOver`, and `Drop` target negotiation.
- **2-Step Keyboard Drag (`F8`)**: Hover your cursor over any target window, press **`F8`** to engage drag (illuminating drop cues), and press **`F8`** again to drop.
- **Floating Drag Card (Mouse Drag)**: Grab and drag the floating acrylic widget card directly into any target application.
- **Multi-Widget Staging**: Spawning multiple `windrop` processes automatically cascades independent cards (`+30px` offset) across your screen.
- **Multi-Format Shell Payload**:
  - `CF_HDROP`: Native shell file lists for Explorer, 7-Zip, Discord, Slack.
  - `CFSTR_PREFERREDDROPEFFECT`: Explicit `DROPEFFECT_COPY` / `DROPEFFECT_MOVE` negotiation.
  - `CF_UNICODETEXT` & `CF_TEXT`: Newline-separated file paths for text editors, IDEs, and terminals.
  - `CFSTR_SHELLURL`: `file:///` URLs for web browsers and Electron web applications.
- **Shell Drag Image**: Translucent shell drag preview rendered via `IDragSourceHelper2`.
- **Per-Monitor V2 DPI Aware**: Native crisp rendering across high-DPI and mixed-monitor setups.
- **Fast Dismissal**: Right-Click or press **`Escape`** anywhere on your keyboard to cancel.

---

## Usage

```powershell
windrop <files...>
```

### Examples

```powershell
# Single file
windrop photo.png

# Multiple files
windrop report.pdf notes.txt data.csv

# Wildcards
windrop *.jpg

# Paths with spaces
windrop "C:\My Documents\Quarterly Report.xlsx"

# Relative paths
windrop .\dist\bundle.js
```
---

## Build Instructions

### Requirements
- Windows 10 / 11 (x64)
- Visual Studio 2019 or 2022 (Desktop development with C++)
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

### Controls

| Action | Key / Interaction |
| :--- | :--- |
| **Engage Drag (Keyboard)** | Hover cursor over destination window and press **`F8`** |
| **Commit Drop (Keyboard)** | Press **`F8`** again over the destination window |
| **Mouse Drag** | Grab and drag the floating card directly into the target window |
| **Cancel / Dismiss** | Press **`Escape`** or **Right-Click** the card |

### Exit Codes

| Exit Code | Constant | Meaning |
| :---: | :--- | :--- |
| `0` | `EXIT_OK` | Drop completed successfully |
| `1` | `EXIT_BAD_ARGS` | Invalid arguments or no files specified |
| `2` | `EXIT_FILE_MISSING` | One or more specified files were not found |
| `3` | `EXIT_CANCELLED` | Drag cancelled by user (Escape / Right-Click) |
| `4` | `EXIT_COM_ERROR` | Internal OLE / COM initialization error |

---

## Target Application Compatibility

| Target Application | Supported Payload | Status |
| :--- | :--- | :---: |
| Discord, Slack, Microsoft Teams | `CF_HDROP`, `CFSTR_SHELLURL` | Full Support |
| Chrome, Edge, Firefox, Brave | HTML5 file drop, Gmail, web uploads | Full Support |
| VS Code, Cursor, Visual Studio | Editor split view, file explorer drop | Full Support |
| Windows File Explorer | File copy and move operations | Full Support |
| Notepad, Sublime Text, Text Editors | `CF_UNICODETEXT` (file paths) | Full Support |
| Photoshop, Illustrator, GIMP | Image canvas ingestion | Full Support |

---

## Troubleshooting

- **PowerShell Wildcard Expansion**:
  In PowerShell, `windrop *.png` may be pre-expanded by the shell. To let `windrop` handle path canonicalization directly, quote expressions: `windrop "*.png"`.

- **Interactive Session Requirement**:
  Windows OLE drag-and-drop requires an active interactive desktop window station (`WinSta0`). It cannot execute across headless SSH sessions without an attached display.

---

## License

MIT License. See `LICENSE` for details.
