<#
.SYNOPSIS
    First-time dependency setup for Vedantu Clicker System.

.DESCRIPTION
    Installs vcpkg (if not present), bootstraps it, copies .env from
    .env.example, and runs CMake configure + Release build.

    Designed to work on any developer machine without editing this file.
    Override defaults via environment variables or script parameters.

.PARAMETER VcpkgRoot
    Where to install/find vcpkg. Default: C:\vcpkg
    Override: set the VCPKG_ROOT environment variable.

.PARAMETER SunvoteSdkDir
    Path to the SunVote WSapp SDK folder (needed only when re-staging
    SDK binaries into sdk_embed/). Leave blank if sdk_embed/ already has files.
    Override: set the SUNVOTE_SDK_DIR environment variable.

.EXAMPLE
    # Standard first-time setup
    .\setup_deps.ps1

    # With a custom vcpkg location
    .\setup_deps.ps1 -VcpkgRoot "D:\tools\vcpkg"

    # Also stage SDK binaries
    .\setup_deps.ps1 -SunvoteSdkDir "C:\SunVote SDK\WSapp for win-5.1.0.43"
#>
param(
    [string]$VcpkgRoot     = $env:VCPKG_ROOT     ?? "C:\vcpkg",
    [string]$SunvoteSdkDir = $env:SUNVOTE_SDK_DIR ?? ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot

function Step([int]$n, [int]$total, [string]$msg) {
    Write-Host "[$n/$total] $msg" -ForegroundColor Cyan
}

# ── 1. Verify prerequisites in PATH ───────────────────────────────────────────
Step 1 5 "Checking prerequisites..."

foreach ($tool in @("git", "cmake")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Host "ERROR: '$tool' not found in PATH." -ForegroundColor Red
        Write-Host "Please install it and ensure it is on your PATH, then rerun." -ForegroundColor Yellow
        exit 1
    }
}
Write-Host "      git and cmake found in PATH." -ForegroundColor Green

# ── 2. Install / bootstrap vcpkg ──────────────────────────────────────────────
Step 2 5 "Setting up vcpkg at $VcpkgRoot..."

if (Test-Path "$VcpkgRoot\vcpkg.exe") {
    Write-Host "      vcpkg already bootstrapped." -ForegroundColor Green
} elseif (Test-Path "$VcpkgRoot\bootstrap-vcpkg.bat") {
    Write-Host "      Bootstrapping existing clone..."
    Push-Location $VcpkgRoot
    cmd /c "bootstrap-vcpkg.bat -disableMetrics"
    Pop-Location
} else {
    Write-Host "      Cloning vcpkg (this may take a minute)..."
    git clone https://github.com/microsoft/vcpkg $VcpkgRoot --depth=1
    if ($LASTEXITCODE -ne 0) { Write-Host "Clone failed." -ForegroundColor Red; exit 1 }
    Push-Location $VcpkgRoot
    cmd /c "bootstrap-vcpkg.bat -disableMetrics"
    Pop-Location
}

if (-not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Host "ERROR: vcpkg.exe not found after bootstrap." -ForegroundColor Red
    exit 1
}
Write-Host "      vcpkg OK: $VcpkgRoot\vcpkg.exe" -ForegroundColor Green

# ── 3. .env ───────────────────────────────────────────────────────────────────
Step 3 5 "Setting up .env..."

if (-not (Test-Path "$ProjectRoot\.env")) {
    Copy-Item "$ProjectRoot\.env.example" "$ProjectRoot\.env"
    Write-Host "      .env created from .env.example." -ForegroundColor Green
    Write-Host ""
    Write-Host "  ACTION REQUIRED: Open .env and fill in:" -ForegroundColor Yellow
    Write-Host "    SESSION_SECRET   — your Vedantu scheduling API secret" -ForegroundColor Yellow
    Write-Host "    OFFLINE_AES_KEY  — a random 64-character hex string" -ForegroundColor Yellow
    Write-Host ""
} else {
    Write-Host "      .env already exists." -ForegroundColor Green
}

# ── 4. CMake configure ────────────────────────────────────────────────────────
Step 4 5 "Configuring CMake..."

$cmakeArgs = @(
    "-B", "$ProjectRoot\build",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-A", "x64",
    $ProjectRoot
)

if ($SunvoteSdkDir -ne "") {
    Write-Host "      SunVote SDK dir: $SunvoteSdkDir" -ForegroundColor Cyan
    $cmakeArgs += "-DSUNVOTE_SDK_DIR=$SunvoteSdkDir"
} else {
    Write-Host "      SUNVOTE_SDK_DIR not set — using pre-staged sdk_embed/ binaries." -ForegroundColor DarkGray
}

cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configure failed. Check output above." -ForegroundColor Red
    exit 1
}
Write-Host "      CMake configured." -ForegroundColor Green

# ── 5. Build Release ──────────────────────────────────────────────────────────
Step 5 5 "Building Release..."
Write-Host "      NOTE: First build downloads vcpkg packages — may take 10–30 minutes." -ForegroundColor Yellow

cmake --build "$ProjectRoot\build" --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed. See errors above." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "SETUP COMPLETE" -ForegroundColor Green
Write-Host "  Executable : $ProjectRoot\build\Release\VedantuClickerSystem.exe"
Write-Host "  AppRelease : $ProjectRoot\AppRelease\VedantuClickerSystem.exe"
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Edit .env with your credentials (SESSION_SECRET, OFFLINE_AES_KEY)"
Write-Host "  2. Connect the SunVote USB dongle"
Write-Host "  3. Run build\Release\VedantuClickerSystem.exe"
