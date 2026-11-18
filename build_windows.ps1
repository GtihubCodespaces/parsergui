# Builds the whole project with Visual Studio 2022, runs the tests and lays out a
# runnable dist\ directory. The .sln opens the same sources in the IDE.
param(
    [string]$Configuration = "Release",
    [switch]$NoGui
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$gui = if ($NoGui) { "OFF" } else { "ON" }
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    -DPARSERGUI_BUILD_GUI=$gui -DPARSERGUI_BUILD_TESTS=ON
cmake --build build --config $Configuration --parallel 4
ctest --test-dir build -C $Configuration --output-on-failure

Remove-Item -Recurse -Force dist -ErrorAction SilentlyContinue
cmake --install build --config $Configuration --prefix dist
Copy-Item providers, skills, assets -Destination dist -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "`nReady: $root\dist\bin\parsergui.exe"
