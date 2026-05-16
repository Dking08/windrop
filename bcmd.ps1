cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix C:\Tools

Remove-Item "C:\tools\drag" -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue
Remove-Item "$HOME\.local\bin\drag.exe" -Force -Confirm:$false -ErrorAction SilentlyContinue

Move-Item "C:\tools\bin" "C:\tools\drag" -Force
Copy-Item "C:\tools\drag\drag.exe" "$HOME\.local\bin\" -Force
