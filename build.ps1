# build.ps1 — Configure + build OpenWarband on Windows
# Requires: CMake >= 3.20, a C++ compiler, git
# With Visual Studio installed: cmake auto-detects the MSVC toolchain.
# With MinGW:  cmake -B build -G "MinGW Makefiles"

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# --- check prerequisites ---
foreach ($tool in @("cmake", "git")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "'$tool' not found. Install it and make sure it is on PATH."
        exit 1
    }
}

# --- pick a generator ---
# Prefer MSVC if available (cl on PATH), otherwise fall back to MinGW GCC.
$genArgs = @()
if (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host ">>> Using Visual Studio / MSVC toolchain" -ForegroundColor Cyan
} elseif (Get-Command gcc -ErrorAction SilentlyContinue) {
    Write-Host ">>> Using MinGW (GCC) toolchain" -ForegroundColor Cyan
    $genArgs = @("-G", "MinGW Makefiles")
} else {
    Write-Error "No C++ compiler found. Install Visual Studio Build Tools or MSYS2/MinGW."
    exit 1
}

# --- configure ---
Write-Host ">>> Configuring..." -ForegroundColor Cyan
# Always reconfigure — stale CMakeCache from WSL or a different generator
# causes cryptic build errors.
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -B build @genArgs -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) { Write-Error "cmake configure failed"; exit 1 }

# --- build ---
Write-Host ">>> Building Release..." -ForegroundColor Cyan
cmake --build build --config Release

if ($LASTEXITCODE -ne 0) { Write-Error "cmake build failed"; exit 1 }

# --- locate exe ---
$exe = "build\Release\openwarband.exe"
if (-not (Test-Path $exe)) {
    # try single-config generator (MinGW / Makefiles)
    $exe = "build\openwarband.exe"
}

if (Test-Path $exe) {
    Write-Host ">>> Build succeeded: $exe" -ForegroundColor Green
    $run = Read-Host "Run the game now? (y/n)"
    if ($run -eq "y") { Start-Process $exe }
} else {
    Write-Error "Executable not found after build."
    exit 1
}
