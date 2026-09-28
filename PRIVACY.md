# Windrop Privacy Policy

**Effective date:** September 28, 2026
**Applies to:** Windrop 2.1.0 and later releases (`windrop.exe`), unless a newer version of this policy says otherwise.
**Developer:** Dastageer Siddiqui ([@Dking08](https://github.com/Dking08))

Windrop is a small, local, command-line utility for Windows. You give it one or more files on the command line, it shows a floating card, and you drag that card (or press `F8`) onto another application. This policy explains exactly what information Windrop handles while doing that.

## Summary

- Windrop **does not collect, store, or transmit** any personal or identifying information.
- Windrop has **no telemetry, analytics, crash reporting, accounts, or auto-update**.
- Windrop has **no network communication**. It does not open network connections, and no networking libraries are linked into it.
- Windrop **does not read the contents of your files**.
- Windrop **keeps nothing after it exits**. It writes no files, logs, settings, or registry entries.
- Windrop **does expose the paths of the files you choose** to the application you drop them onto. That is the tool's purpose, and it only happens when you perform the drop.

## Information Windrop handles

Windrop handles the following information locally, in memory, only while it is running:

| Information | Why it is handled |
| :--- | :--- |
| File paths and file names you pass as command-line arguments (wildcards such as `*.jpg` are expanded locally) | To build the drag-and-drop payload and to show the file name (or the number of files) on the card |
| Whether each path exists and is a regular file | To reject missing paths and directories before starting |
| The shell icon of the first file | To display an icon on the card and in the drag image. The icon is requested from the Windows shell, which may inspect the file's metadata or type to choose it. Windrop itself does not open or read the file |
| Mouse cursor position and monitor geometry | To place the card near your cursor and keep it on-screen |
| Presses of the `F8` and `Esc` keys | Registered as system-wide hotkeys to start, complete, or cancel a drag from the keyboard |

Windrop does **not** record, log, or inspect any other keystrokes. It uses the standard Windows `RegisterHotKey` API, which notifies Windrop only when one of the two registered keys is pressed. It does not use low-level keyboard hooks. While a Windrop card is open, `F8` and `Esc` are claimed by Windrop and are not delivered to other applications. Windrop also generates small synthetic mouse events (a mouse-button press or release, and a one-pixel mouse nudge) so that Windows treats a keyboard-initiated drag like a normal mouse drag.

## What is exposed to the drop target

During a drag, Windrop offers a standard Windows OLE data object. The application you drag onto (the "drop target") may request any of the formats below. Data is provided only in response to the target's request during a drag-and-drop operation that you initiated.

| Format | What it contains |
| :--- | :--- |
| `CF_HDROP` | The full path of every selected file (the standard shell file list) |
| `Preferred DropEffect` | A flag indicating that both copy and move are acceptable |
| `CF_UNICODETEXT` | The full path of every selected file, one per line |
| `CF_TEXT` | The same list of paths as ANSI text |
| `UniformResourceLocatorW` (`CFSTR_SHELLURL`) | A `file:///` URL for the **first** selected file only |
| `FileNameW` (`CFSTR_FILENAMEW`) | The full path of the **first** selected file only |
| Drag image and drop-description data | A small preview image (the file icon and file name, or "N files") and descriptive text, exchanged with the Windows drag-and-drop helper |

Windrop does **not** provide file contents through the data object. It offers no file-contents, file-descriptor, or stream formats. It provides paths, and the drop target opens the files itself using its own permissions.

Windrop does not write to the Windows clipboard. These formats exist only on the temporary drag-and-drop data object, which is discarded when the drag ends or Windrop exits.

## What the drop target does with your files

Once you drop onto another application, that application receives the paths above and may read the files, copy or move them, upload them, or otherwise process them. For example, dropping onto a chat application or a browser upload field can cause that application to send the file over the internet. That behavior belongs to the receiving application and is governed by its own privacy policy. Windrop cannot control it, observe it, or see the result. Only drop files onto applications you trust with them.

## File contents

Windrop never opens, reads, parses, copies, modifies, moves, or deletes your files. Any copy or move is performed by the drop target, not by Windrop. The only file-system operations Windrop performs are resolving paths and expanding wildcards (looking up names and attributes) and, through the Windows shell, retrieving the file's icon.

## Retention

Windrop retains nothing. All information described in this policy lives only in the memory of the running process and is released when the process exits. Windrop creates no configuration files, logs, caches, temporary files, or registry entries.

Windrop prints status messages to your terminal, including the list of file paths in the payload (and extra instructions when you use `--verbose`). This output goes only to the console window you launched it from. If you redirect that output to a file or capture it with a terminal or logging tool, the paths will be stored wherever you send them.

## Network communication and telemetry

Windrop performs no network communication and no telemetry. It does not contact any server, check for updates, send usage statistics, or report crashes. Its only dependencies are Windows system libraries for drag-and-drop, the shell, and window drawing (`ole32`, `oleaut32`, `shell32`, `uuid`, `gdi32`, `user32`), and the statically linked C++ runtime. No socket, HTTP, or other networking code is present in the source.

Multiple Windrop cards on the same computer coordinate using ordinary local Windows window messages, for example to decide which card receives the `F8` hotkey. This communication stays on your machine.

## Third parties

Windrop shares no information with the developer or any third party. It does not include third-party SDKs or services. Downloading Windrop from GitHub or through the Windows Package Manager is subject to the privacy practices of those services, which are separate from Windrop.

## Children

Windrop collects no personal information from anyone, including children.

## Verify it yourself

Windrop is open source under the [MIT License](LICENSE), and the entire program is a few small source files in [`src/`](src). You can review the code, and reproduce the released binary using the deterministic build settings in `CMakeLists.txt`, to confirm the statements in this policy.

## Changes to this policy

If a future version of Windrop changes how it handles information, this policy will be updated in this repository before that version is released. Previous versions can be viewed in the repository's commit history. The effective date at the top shows when it last changed.

## Contact

Questions about this policy can be filed as an [issue](https://github.com/Dking08/windrop/issues). To report a security problem privately, see [SECURITY.md](SECURITY.md).
