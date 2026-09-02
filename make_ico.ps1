<#
.SYNOPSIS
    Converts a PNG to a multi-resolution Windows .ico file.

.DESCRIPTION
    Generates vedantu.ico from a source PNG using the .NET System.Drawing API.
    Produces a standard multi-size ICO (256, 128, 64, 48, 32, 16 px).

.PARAMETER SourcePng
    Path to the source PNG image. Defaults to a file named vedantu_icon.png
    in the same directory as this script.

.PARAMETER OutputIco
    Path for the output .ico file. Defaults to vedantu.ico in the project root.

.EXAMPLE
    .\make_ico.ps1
    .\make_ico.ps1 -SourcePng "C:\path\to\icon.png" -OutputIco ".\vedantu.ico"
#>
param(
    [string]$SourcePng  = (Join-Path $PSScriptRoot "vedantu_icon.png"),
    [string]$OutputIco  = (Join-Path $PSScriptRoot "vedantu.ico")
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $SourcePng)) {
    Write-Host "ERROR: Source PNG not found at: $SourcePng" -ForegroundColor Red
    Write-Host "Usage: .\make_ico.ps1 -SourcePng 'C:\path\to\icon.png'" -ForegroundColor Yellow
    exit 1
}

$sizes = @(256, 128, 64, 48, 32, 16)

$srcBitmap = [System.Drawing.Bitmap]::new($SourcePng)

$ms = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($ms)

# ICO header
$bw.Write([uint16]0)       # Reserved
$bw.Write([uint16]1)       # Type: 1 = ICO
$bw.Write([uint16]$sizes.Count)

# Image data memory streams
$imageStreams = @()
foreach ($sz in $sizes) {
    $resized = [System.Drawing.Bitmap]::new($srcBitmap, $sz, $sz)
    $ims = [System.IO.MemoryStream]::new()
    $resized.Save($ims, [System.Drawing.Imaging.ImageFormat]::Png)
    $imageStreams += $ims
    $resized.Dispose()
}

# Directory entries
$offset = 6 + ($sizes.Count * 16)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $sz   = $sizes[$i]
    $data = $imageStreams[$i].ToArray()
    $bw.Write([byte]($sz -eq 256 ? 0 : $sz))  # width (0 = 256)
    $bw.Write([byte]($sz -eq 256 ? 0 : $sz))  # height
    $bw.Write([byte]0)     # color count
    $bw.Write([byte]0)     # reserved
    $bw.Write([uint16]1)   # planes
    $bw.Write([uint16]32)  # bit count
    $bw.Write([uint32]$data.Length)
    $bw.Write([uint32]$offset)
    $offset += $data.Length
}

# Image data
foreach ($ims in $imageStreams) {
    $bw.Write($ims.ToArray())
    $ims.Dispose()
}

$bw.Flush()
[System.IO.File]::WriteAllBytes($OutputIco, $ms.ToArray())
$ms.Dispose()
$srcBitmap.Dispose()

Write-Host "ICO written to: $OutputIco" -ForegroundColor Green
Write-Host "Sizes: $($sizes -join ', ') px"
