<#
.SYNOPSIS
    Assembles the distributable package: a clean folder, a portability check, and a zip.

.DESCRIPTION
    The build output directory is not a release. It also holds the static libraries, the
    test binary and gmock, and — in every configuration except Release — the program
    databases. This script copies only what the application needs at run time into a
    versioned folder under dist\, adds the Visual C++ runtime next to the executable, and
    zips the result.

    On the runtime: the app links against the dynamic CRT, so msvcp140.dll and friends are
    loaded at startup. Windows does not ship them — they arrive with the Visual C++
    Redistributable — so a clean Windows installation may not have them. They are copied
    out of the Visual Studio redistributable directory into the package, which is the
    documented app-local deployment model and removes the last external dependency.
    Pass -NoCrt to skip that and rely on the machine having the redistributable.

    The version comes from vcpkg.json and is checked against the version resource in the
    built executable. Those two are written in different places, so the check is what
    stops a package whose file version disagrees with the manifest from being produced.

    Nothing here is destructive outside dist\.

.PARAMETER Configuration
    Configuration to package. Defaults to Release. Packaging anything else is allowed but
    warned about, because only Release is built without debug information.

.PARAMETER OutputDir
    Where to write the package folder and the zip. Defaults to dist\ next to the solution.

.PARAMETER NoZip
    Skip creating the zip, leaving only the folder.

.PARAMETER NoCrt
    Do not copy the Visual C++ runtime into the package.

.PARAMETER SkipVerify
    Do not run tools\verify-portable.ps1 against the assembled folder.

.PARAMETER Build
    Build the configuration first, through tools\build.bat, before packaging.

.EXAMPLE
    tools\package.ps1
    tools\package.ps1 -Build
    tools\package.ps1 -Configuration RelWithDebInfo -NoZip
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$OutputDir,
    [switch]$NoZip,
    [switch]$NoCrt,
    [switch]$SkipVerify,
    [switch]$Build
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist' }

function Write-Step([string]$Message) { Write-Host $Message }

function Fail([string]$Message) {
    Write-Host ''
    Write-Host "PACKAGE_FAILED  $Message"
    exit 1
}

# --- Version, from the one place it is declared ------------------------------
$vcpkgPath = Join-Path $root 'vcpkg.json'
if (-not (Test-Path -LiteralPath $vcpkgPath)) { Fail "vcpkg.json not found at $vcpkgPath" }

$vcpkgText = Get-Content -LiteralPath $vcpkgPath -Raw
if ($vcpkgText -notmatch '"version"\s*:\s*"([^"]+)"') {
    Fail "could not read the ""version"" field from $vcpkgPath"
}
$version = $Matches[1]

$packageName = "TaskManagerPlusPlus-$version-win-x64"

Write-Host "=== PACKAGE $packageName ==="
Write-Step "  configuration : $Configuration"
Write-Step "  version       : $version (vcpkg.json)"

# --- Optional build ----------------------------------------------------------
if ($Build) {
    $buildScript = Join-Path $PSScriptRoot 'build.bat'
    if (-not (Test-Path -LiteralPath $buildScript)) { Fail "build.bat not found at $buildScript" }

    Write-Host ''
    Write-Host "=== BUILD ($Configuration) ==="
    & cmd /c "`"$buildScript`" $Configuration"
    if ($LASTEXITCODE -ne 0) { Fail "build.bat $Configuration failed with exit code $LASTEXITCODE" }
}

# --- Locate the build output -------------------------------------------------
$source = Join-Path $root "build\x64\$Configuration"
$sourceExe = Join-Path $source 'tmpp.exe'

if (-not (Test-Path -LiteralPath $sourceExe)) {
    Fail "tmpp.exe not found in $source. Build first: tools\build.bat $Configuration"
}

# --- Version drift guard -----------------------------------------------------
# app.rc carries the same version by hand. If the two ever diverge, the package would
# claim one version in its file name and another in its property sheet.
$exeVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($sourceExe)
if ($exeVersion.ProductVersion -ne $version) {
    Fail ("version mismatch: vcpkg.json says '$version' but $sourceExe reports " +
          "'$($exeVersion.ProductVersion)'. Update src\App\app.rc to match.")
}

if ($Configuration -ne 'Release') {
    Write-Warning "Packaging '$Configuration'. Only Release is built without debug information."
}

# --- Assemble ----------------------------------------------------------------
$staging = Join-Path $OutputDir $packageName

if (Test-Path -LiteralPath $staging) {
    Write-Step "  cleaning      : $staging"
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Path $staging -Force | Out-Null

# Build artefacts that must never reach a user. tmpp_tests.exe and gmock are part of the
# solution but not of the product; the .lib files are link inputs; .pdb never ships.
$excluded = @(
    '*.pdb', '*.lib', '*.exp', '*.ilk', '*.idb', '*.obj', '*.tlog',
    '*.lastbuildstate', '*.log',
    'tmpp_tests.exe', 'gmock.dll', 'gmock_main.dll', 'gtest.dll', 'gtest_main.dll',
    'run.bat'
)

Write-Step "  copying       : $source"

$robocopyArgs = @($source, $staging, '/E', '/NJH', '/NJS', '/NDL', '/NFL', '/NP', '/R:1', '/W:1', '/XF') + $excluded
& robocopy @robocopyArgs | Out-Null
# robocopy reports bit flags: 1 copied, 2 extra files, 4 mismatches. 8 and above are errors.
if ($LASTEXITCODE -ge 8) { Fail "robocopy failed with exit code $LASTEXITCODE" }

# Belt and braces: robocopy's /XF matches names, so a stray artefact under a
# differently-cased name or in a subdirectory would slip through. Assert on the result.
$leaked = @(Get-ChildItem -LiteralPath $staging -Recurse -File |
    Where-Object {
        $_.Extension -in '.pdb', '.lib', '.exp', '.ilk', '.idb', '.obj', '.tlog' -or
        $_.Name -in 'tmpp_tests.exe', 'gmock.dll', 'gmock_main.dll', 'gtest.dll', 'gtest_main.dll', 'run.bat'
    })

if ($leaked.Count -gt 0) {
    Fail ("build artefacts reached the package: " + (($leaked | ForEach-Object { $_.Name }) -join ', '))
}

# --- Visual C++ runtime, app-local ------------------------------------------
if (-not $NoCrt) {
    $vsBase = $env:VSBASE
    if (-not $vsBase) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
            if ($found) { $vsBase = ($found | Select-Object -First 1).Trim() }
        }
    }

    if (-not $vsBase) {
        foreach ($candidate in @(
            "$env:ProgramFiles\Microsoft Visual Studio\2022\Community",
            "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional",
            "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise",
            "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools",
            'D:\Program Files\Microsoft Visual Studio\18\Community'
        )) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'Common7\Tools\VsDevCmd.bat')) { $vsBase = $candidate; break }
        }
    }

    $crtDir = $null
    if ($vsBase) {
        $redistRoot = Join-Path $vsBase 'VC\Redist\MSVC'
        if (Test-Path -LiteralPath $redistRoot) {
            $crtDir = Get-ChildItem -LiteralPath $redistRoot -Directory |
                Sort-Object Name -Descending |
                ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
                Select-Object -First 1
        }
    }

    if (-not $crtDir) {
        Fail ("the Visual C++ runtime directory was not found, so the package would depend on " +
              "the machine having the Redistributable installed. Set VSBASE to the Visual Studio " +
              "installation, or pass -NoCrt to package without it.")
    }

    $crtFiles = @(Get-ChildItem -LiteralPath $crtDir.FullName -Filter '*.dll')
    foreach ($file in $crtFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination $staging -Force
    }
    $crtBytes = ($crtFiles | Measure-Object -Property Length -Sum).Sum
    Write-Step ("  runtime       : {0} DLL(s), {1:N1} MB, from {2}" -f $crtFiles.Count, ($crtBytes / 1MB), $crtDir.FullName)
}
else {
    Write-Step '  runtime       : skipped (-NoCrt); the machine must have the VC++ Redistributable'
}

# --- Licence and a note for whoever opens the folder ------------------------
$license = Join-Path $root 'LICENSE'
if (Test-Path -LiteralPath $license) {
    Copy-Item -LiteralPath $license -Destination $staging -Force
}

$readme = @"
TaskManagerPlusPlus $version
$(('=' * ("TaskManagerPlusPlus $version".Length)))

A task manager for Windows, built with C++20 and WinUI 3.

Running it
----------
Start tmpp.exe. There is no installer and nothing is written outside the user's
own profile.

Portable
--------
Delete this folder and the application is gone, apart from its settings, which
live in %LOCALAPPDATA%\TaskManagerPlusPlus. Delete that directory too for a
complete removal.

Requirements
------------
Windows 11 x64 (build 22000 or later). The Visual C++ runtime is included in this
folder, so there is nothing to install and no dependency beyond the operating
system itself.

Licence
-------
MIT. See LICENSE.
"@

Set-Content -LiteralPath (Join-Path $staging 'README.txt') -Value $readme -Encoding UTF8

# --- Verify ------------------------------------------------------------------
if (-not $SkipVerify) {
    Write-Host ''
    $verifier = Join-Path $PSScriptRoot 'verify-portable.ps1'
    if (-not (Test-Path -LiteralPath $verifier)) { Fail "verify-portable.ps1 not found at $verifier" }

    & $verifier -Path $staging
    if ($LASTEXITCODE -ne 0) { Fail 'the assembled package did not pass the portability check' }
}

# --- Zip ---------------------------------------------------------------------
$zipPath = $null
if (-not $NoZip) {
    $zipPath = Join-Path $OutputDir "$packageName.zip"
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

    Write-Host ''
    Write-Step "  compressing   : $zipPath"

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # includeBaseDirectory keeps the archive tidy: extracting produces the versioned
    # folder rather than spraying a hundred files into the current directory.
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $staging,
        $zipPath,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $true)
}

# --- Summary -----------------------------------------------------------------
$fileCount = (Get-ChildItem -LiteralPath $staging -Recurse -File).Count
$folderBytes = (Get-ChildItem -LiteralPath $staging -Recurse -File | Measure-Object -Property Length -Sum).Sum

Write-Host ''
Write-Host '=== PACKAGE_OK ==='
Write-Host ("  folder : {0}" -f $staging)
Write-Host ("           {0} file(s), {1:N1} MB" -f $fileCount, ($folderBytes / 1MB))
if ($zipPath) {
    $zipBytes = (Get-Item -LiteralPath $zipPath).Length
    Write-Host ("  zip    : {0}" -f $zipPath)
    Write-Host ("           {0:N1} MB" -f ($zipBytes / 1MB))
}

exit 0
