param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $cmake) { throw 'CMake is not installed or is not on PATH. Install Visual Studio 2026 with Desktop development with C++ and CMake tools.' }
    & cmake.exe -S . -B build -G 'Visual Studio 18 2026' -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake.exe --build build --config $Configuration -- /m:1 /nr:false
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    & ctest.exe --test-dir build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed.' }
    $dll = Join-Path $root ("build\$Configuration\JishsVirtualTV.dll")
    if (-not (Test-Path $dll)) { throw "DLL not found at $dll" }
    Write-Host "Build succeeded: $dll" -ForegroundColor Green
    Write-Host 'Enable the DLL through a compatible ReSkate DLL loader; press F8 in-game.'
} finally {
    Pop-Location
}
