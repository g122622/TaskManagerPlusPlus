<#
.SYNOPSIS
    Builds the MSI installer from an assembled package.

.DESCRIPTION
    The MSI is built with WiX Toolset 3.14, which is downloaded on first use into
    .cache\wix314 (gitignored) so the machine does not need a global installation.
    WiX 3 is used because it needs only the .NET Framework that ships with Windows;
    WiX 4+ needs the .NET SDK, which this project does not otherwise require.

    Product.wxs carries the product identity and UI; tools\msi\harvest.ps1 turns
    the packaged folder into Payload.wxs, one component per file.

    The MSI version is YY.MM.(DD*100+NN); the installed folder and shortcut keep
    the plain name. Windows Installer only allows major.minor.build with
    major/minor at most 255, so 26111901 becomes 26.11.1901. See tools\version.ps1.

.PARAMETER Version
    Release version YYMMDDNN. Defaults to today's date and sequence 01.

.PARAMETER PackageDir
    Packaged folder to install. Defaults to dist\TaskManagerPlusPlus-<Version>-win-x64.

.PARAMETER OutputDir
    Where to write the .msi. Defaults to dist\.

.PARAMETER WixDir
    Directory that already contains candle.exe and light.exe. Skips the download.

.EXAMPLE
    tools\build-msi.ps1 -Version 26111901

.EXAMPLE
    tools\build-msi.ps1
#>
[CmdletBinding()]
param(
    [string]$Version,
    [string]$PackageDir,
    [string]$OutputDir,
    [string]$WixDir
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Fail([string]$Message) {
    Write-Host ''
    Write-Host "MSI_FAILED  $Message"
    exit 1
}

$versionScript = Join-Path $PSScriptRoot 'version.ps1'
if (-not (Test-Path -LiteralPath $versionScript)) { Fail "version.ps1 not found" }

$v = & $versionScript -Version $Version
if (-not $v) { Fail 'could not compute the release version' }

if (-not $PackageDir) { $PackageDir = Join-Path $root "dist\TaskManagerPlusPlus-$($v.Display)-win-x64" }
if (-not (Test-Path -LiteralPath (Join-Path $PackageDir 'tmpp.exe'))) {
    Fail "tmpp.exe is not in $PackageDir. Run tools\package.ps1 first."
}
if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist' }

Write-Host "=== MSI $($v.Display) ==="
Write-Host "  msi version : $($v.MsiVersion)"
Write-Host "  payload     : $PackageDir"

# --- WiX --------------------------------------------------------------------
$wixVersion = '3.14.1'
$wixUrl = 'https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip'

if ($WixDir) {
    $wixRoot = $WixDir
}
else {
    $wixRoot = Join-Path $root '.cache\wix314'
}

$candle = Join-Path $wixRoot 'candle.exe'
$light = Join-Path $wixRoot 'light.exe'

if (-not (Test-Path -LiteralPath $candle) -or -not (Test-Path -LiteralPath $light)) {
    if ($WixDir) { Fail "candle.exe and light.exe were not found in $WixDir" }

    Write-Host "  fetching    : WiX $wixVersion (one-time, ~41 MB)"
    $cache = Join-Path $root '.cache'
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $zip = Join-Path $cache 'wix314.zip'

    if (-not (Test-Path -LiteralPath $zip)) {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $wixUrl -OutFile $zip -UseBasicParsing
    }

    if (Test-Path -LiteralPath $wixRoot) { Remove-Item -LiteralPath $wixRoot -Recurse -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $wixRoot)

    if (-not (Test-Path -LiteralPath $candle)) { Fail "candle.exe was not found after extracting $zip" }
}

# --- Harvest ----------------------------------------------------------------
$intermediate = Join-Path $root 'build\msi'
if (Test-Path -LiteralPath $intermediate) { Remove-Item -LiteralPath $intermediate -Recurse -Force }
New-Item -ItemType Directory -Path $intermediate -Force | Out-Null

$payloadWxs = Join-Path $intermediate 'Payload.wxs'
& (Join-Path $PSScriptRoot 'msi\harvest.ps1') -AppDir $PackageDir -Output $payloadWxs
# harvest.ps1 is PowerShell, so $LASTEXITCODE does not describe it; the file is
# the contract.
if (-not (Test-Path -LiteralPath $payloadWxs)) { Fail 'harvest.ps1 did not produce Payload.wxs' }

# --- Compile and link -------------------------------------------------------
$productWxs = Join-Path $PSScriptRoot 'msi\Product.wxs'
$defines = @(
    '-nologo',
    '-arch', 'x64',
    "-dVersion=$($v.MsiVersion)",
    "-dDisplayVersion=$($v.Display)",
    # candle insists on a trailing backslash to treat -out as a directory.
    '-out', "$intermediate\"
)

Write-Host '  candle      : Product.wxs, Payload.wxs'
& $candle @defines $productWxs $payloadWxs
if ($LASTEXITCODE -ne 0) { Fail "candle failed with exit code $LASTEXITCODE" }

$msiPath = Join-Path $OutputDir "TaskManagerPlusPlus-$($v.Display)-win-x64.msi"
if (Test-Path -LiteralPath $msiPath) { Remove-Item -LiteralPath $msiPath -Force }

$wixUiWxl = Join-Path $wixRoot 'WixUI_en-us.wxl'
$lightArgs = @(
    '-nologo',
    '-ext', 'WixUIExtension',
    '-cultures:en-us',
    # Per-machine install with an All Users start-menu shortcut: ICE38/43/57
    # expect a per-user key path for shortcut components, which conflicts with
    # the HKLM key path required by a per-machine component. The shortcut is
    # valid; only the validation rules are inapplicable here.
    '-sice:ICE38',
    '-sice:ICE43',
    '-sice:ICE57',
    '-out', $msiPath,
    (Join-Path $intermediate 'Product.wixobj'),
    (Join-Path $intermediate 'Payload.wixobj')
)
if (Test-Path -LiteralPath $wixUiWxl) { $lightArgs += @('-loc', $wixUiWxl) }

Write-Host "  light       : $msiPath"
& $light @lightArgs
if ($LASTEXITCODE -ne 0) { Fail "light failed with exit code $LASTEXITCODE" }

# light always writes a .wixpdb (same base name, .wixpdb extension) next to the
# .msi. It is diagnostics, not a deliverable, so it does not stay in dist\.
$wixPdb = [System.IO.Path]::ChangeExtension($msiPath, '.wixpdb')
Remove-Item -LiteralPath $wixPdb -Force -ErrorAction SilentlyContinue

$msiBytes = (Get-Item -LiteralPath $msiPath).Length
Write-Host ''
Write-Host '=== MSI_OK ==='
Write-Host ("  version : {0}" -f $v.Display)
Write-Host ("  file    : {0}" -f $msiPath)
Write-Host ("            {0:N1} MB" -f ($msiBytes / 1MB))

exit 0
