# drag.exe — CLI Windows Drag-and-Drop Utility

Initiate a **native Windows drag-and-drop operation directly from the terminal**.

```powershell
PS C:\Stuff> drag report.pdf
# → cursor becomes a drag payload; drop onto Discord, browser, Explorer, etc.
Drop completed
```

---

## Requirements

| Tool | Version |
|------|---------|
| Windows | 10 / 11 (x64 recommended) |
| Visual Studio | 2019 or 2022 (Desktop C++ workload) |
| CMake | 3.16 or later |

> **MinGW/MSYS2** works too if you have `cmake` and a MinGW-w64 toolchain, but
> MSVC is recommended for best shell-API compatibility.

---

## Build Instructions

### Option A — Visual Studio Developer Command Prompt (recommended)

```cmd
git clone <repo>   OR   unzip the source archive
cd drag

:: Create and enter build directory
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

:: Output:
::   build\Release\drag.exe
```

To install to `C:\Tools\bin` (edit path as desired):

```cmd
cmake --install build --config Release --prefix C:\Tools
```

### Option B — Ninja (faster incremental builds)

```cmd
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

:: Output:
::   build\drag.exe
```

### Option C — CMake Preset (VS 2022 + Ninja)

```cmd
cmake --preset release   # requires CMakePresets.json if you add one
cmake --build --preset release
```

---

## Usage

```powershell
drag <files>
```

### Examples

```powershell
# Single file
drag image.png

# Multiple files
drag file1.txt file2.txt

# Wildcard
drag *.jpg

# Absolute path
drag C:\Users\me\Desktop\report.docx

# Quoted path with spaces
drag "my document.pdf"

# Relative path
drag .\subfolder\photo.jpg
```

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | Drop accepted by target |
| `1` | Invalid arguments / no files specified |
| `2` | One or more files not found |
| `3` | Drag cancelled (Escape pressed or released over non-target) |
| `4` | COM / internal error |

---

## Architecture

```
drag/
├── CMakeLists.txt          Build system (MSVC, Ninja, MinGW)
└── src/
    ├── main.cpp            Entry point, COM init, DoDragDrop orchestration
    ├── DropSource.cpp/.h   IDropSource — controls the drag loop
    ├── DataObject.cpp/.h   IDataObject — exposes files via CF_HDROP
    ├── DragImage.cpp/.h    IDragSourceHelper — attaches shell drag image
    └── Utils.cpp/.h        Path resolution, wildcard expansion, helpers
```

### Component responsibilities

#### `main.cpp`
- Parses arguments via `wmain(argc, argv)`
- Calls `Utils::ResolvePaths()` to canonicalise paths and expand wildcards
- Initialises COM with `OleInitialize()` (STA thread apartment)
- Creates a **hidden message-only HWND** (required by `IDragSourceHelper` and `DoDragDrop`)
- Constructs `CDataObject` and `CDropSource`
- Calls `DragImage::AttachShellImage()` (best-effort)
- Uses `SendInput()` to synthesise a mouse-left-down event so `DoDragDrop` sees `MK_LBUTTON` set from the very start
- Calls `DoDragDrop()` — **this call blocks until the user drops or cancels**
- Prints result and returns appropriate exit code

#### `DropSource.cpp` — `IDropSource`
- `QueryContinueDrag()`: returns `DRAGDROP_S_CANCEL` on Escape; `DRAGDROP_S_DROP` when LMB is released; `S_OK` otherwise
- `GiveFeedback()`: returns `DRAGDROP_S_USEDEFAULTCURSORS` → Windows manages cursor icons identically to Explorer

#### `DataObject.cpp` — `IDataObject`
- `GetData(CF_HDROP, TYMED_HGLOBAL)`: builds a `DROPFILES` HGLOBAL with all file paths (UTF-16, double-NUL terminated)
- `QueryGetData()`: advertises CF_HDROP/HGLOBAL support
- `EnumFormatEtc()`: exposes a minimal `IEnumFORMATETC` so well-behaved targets can discover the format

#### `DragImage.cpp`
- CoCreates `CLSID_DragDropHelper` (the shell drag image manager)
- Queries `IDragSourceHelper2` and enables `DSH_ALLOWDROPDESCRIPTIONTEXT` for rich drop descriptions
- Retrieves the shell file icon via `SHGetFileInfoW`
- Renders a 32bpp DIB bitmap: icon + filename (or "+ N files") with 50% alpha for the translucent Explorer look
- Calls `IDragSourceHelper::InitializeFromBitmap()` to attach the image to the data object

#### `Utils.cpp`
- `ResolvePaths()`: expands wildcards with `FindFirstFileW`/`FindNextFileW`, canonicalises paths with `GetFullPathNameW`, verifies existence with `GetFileAttributesW`, deduplicates results
- Error/usage printing helpers

---

## How it works end-to-end

```
CLI user types: drag *.png
        │
        ▼
wmain parses args → ResolvePaths expands *.png → [a.png, b.png, c.png]
        │
        ▼
OleInitialize()  (COM STA)
        │
        ▼
CreateHelperWindow()  (hidden HWND_MESSAGE)
        │
        ▼
new CDataObject([a.png, b.png, c.png])
   └── CF_HDROP HGLOBAL built on demand when target calls GetData()
        │
        ▼
new CDropSource()
        │
        ▼
DragImage::AttachShellImage()
   └── CLSID_DragDropHelper → shell icon + label bitmap → stored on data object
        │
        ▼
SendInput(MOUSEEVENTF_LEFTDOWN)   ← synthesise the "button held" state
        │
        ▼
DoDragDrop(pDataObj, pDropSrc,
           COPY|MOVE|LINK, &dwEffect)
   ┌─────────────────────────────────────┐
   │  Internal drag loop (OLE manages)   │
   │  ┌──────────────────────────────┐   │
   │  │ On each mouse event:          │   │
   │  │   QueryContinueDrag()         │   │
   │  │     Escape? → CANCEL          │   │
   │  │     LMB up? → DROP            │   │
   │  │     else   → S_OK             │   │
   │  │   GiveFeedback()              │   │
   │  │     → USE_DEFAULT_CURSORS     │   │
   │  └──────────────────────────────┘   │
   └─────────────────────────────────────┘
        │
        ▼
DRAGDROP_S_DROP   → "Drop completed"  exit 0
DRAGDROP_S_CANCEL → "Drag canceled"   exit 3
```

---

## Compatibility notes

- **Chrome / Chromium-based browsers** (upload inputs, Gmail attach, etc.): ✅ CF_HDROP is fully supported
- **Discord**: ✅ accepts CF_HDROP file drops
- **Slack**: ✅ same
- **Windows Explorer**: ✅ native
- **VS Code**: ✅ uses Electron, which delegates to Chromium file drop handling
- **Any Win32 app registering as a drop target**: ✅ standard OLE protocol

---

## Troubleshooting

**"Error: File not found: *.png" even though files exist**
→ The shell has already expanded the glob before passing to `wmain`. Pass the
  literal pattern in quotes: `drag "*.png"` or use PowerShell's `-LiteralPath`.
  In PowerShell, globs are expanded by PowerShell itself before reaching the
  executable; you may need `drag (Get-Item *.png).FullName`.

**Drag starts but cursor shows "no-drop" everywhere**
→ Run from an interactive desktop session (not SSH/headless). Drag-and-drop
  requires a visible interactive window station.

**Drag image doesn't appear**
→ Shell image creation is best-effort; the drag still works. May happen in
  restricted environments or with unusual file types.
