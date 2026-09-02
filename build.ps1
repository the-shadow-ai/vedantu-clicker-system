<#
.SYNOPSIS
    Build script for Vedantu Clicker System.

.DESCRIPTION
    Sets up vcpkg (if missing), creates .env (if missing), configures CMake
    with the vcpkg toolchain, and builds the Release binary.

.PARAMETER VcpkgRoot
    Path to your vcpkg installation. Defaults to C:\vcpkg.
    Set the VCPKG_ROOT environment variable to override without editing this file.

.PARAMETER SunvoteSdkDir
    Path to the SunVote WSapp SDK directory (only needed if you need to
    re-copy SDK binaries into sdk_embed/). Leave empty to skip.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -VcpkgRoot "D:\tools\vcpkg"
#>
param(
    [string]$VcpkgRoot    = $env:VCPKG_ROOT ?? "C:\vcpkg",
    [string]$SunvoteSdkDir = $env:SUNVOTE_SDK_DIR ?? ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot

# ── Step 1: vcpkg ─────────────────────────────────────────────────────────────
if (-not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Host "[1/4] Cloning vcpkg to $VcpkgRoot..." -ForegroundColor Cyan
    git clone https://github.com/microsoft/vcpkg $VcpkgRoot
    & "$VcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
} else {
    Write-Host "[1/4] vcpkg found at $VcpkgRoot" -ForegroundColor Green
}

# ── Step 2: .env ──────────────────────────────────────────────────────────────
if (-not (Test-Path "$ProjectRoot\.env")) {
    Write-Host "[2/4] Creating .env from .env.example..." -ForegroundColor Cyan
    Copy-Item "$ProjectRoot\.env.example" "$ProjectRoot\.env"
    Write-Host "      ACTION REQUIRED: Edit .env with your real API credentials before running." -ForegroundColor Yellow
} else {
    Write-Host "[2/4] .env already exists" -ForegroundColor Green
}

# ── Step 3: CMake configure ───────────────────────────────────────────────────
Write-Host "[3/4] Configuring CMake..." -ForegroundColor Cyan

$cmakeArgs = @(
    "-B", "build",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-A", "x64"
)

if ($SunvoteSdkDir -ne "") {
    Write-Host "      SunVote SDK dir: $SunvoteSdkDir" -ForegroundColor Cyan
    $cmakeArgs += "-DSUNVOTE_SDK_DIR=$SunvoteSdkDir"
}

cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed. Check output above." -ForegroundColor Red
    exit 1
}

# ── Step 4: Build ─────────────────────────────────────────────────────────────
Write-Host "[4/4] Building Release..." -ForegroundColor Cyan
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed. Check the error output above." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "BUILD SUCCESSFUL" -ForegroundColor Green
Write-Host "  Executable : build\Release\VedantuClickerSystem.exe" -ForegroundColor White
Write-Host "  AppRelease : AppRelease\VedantuClickerSystem.exe" -ForegroundColor White
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Edit .env with your SESSION_SECRET and API credentials"
Write-Host "  2. Connect the SunVote USB dongle"
Write-Host "  3. Run build\Release\VedantuClickerSystem.exe"
