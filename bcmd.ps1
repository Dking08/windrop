cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix C:\Tools

Remove-Item "C:\tools\windrop" -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue
Remove-Item "$HOME\.local\bin\windrop.exe" -Force -Confirm:$false -ErrorAction SilentlyContinue
Remove-Item "$HOME\.local\bin\drag.exe" -Force -Confirm:$false -ErrorAction SilentlyContinue

Move-Item "C:\tools\bin" "C:\tools\windrop" -Force
Copy-Item "C:\tools\windrop\windrop.exe" "$HOME\.local\bin\windrop.exe" -Force
Copy-Item "C:\tools\windrop\windrop.exe" "$HOME\.local\bin\drag.exe" -Force
Copy-Item "build\Release\windrop.exe" "windrop.exe" -Force
Copy-Item "build\Release\windrop.exe" "drag.exe" -Force

