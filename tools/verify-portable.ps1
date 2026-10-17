<#
.SYNOPSIS
    Verifies that a published folder is self-contained enough to run on a clean machine.

.DESCRIPTION
    Two things are checked, both by reading the binaries in the folder rather than by
    trusting a file list:

      1. Every DLL that the Windows App SDK's reg-free WinRT manifest points at is
         actually present. That manifest is embedded in tmpp.exe as RT_MANIFEST, and it
         is what makes the unpackaged app able to activate Microsoft.UI.Xaml and friends
         without a registered package. A missing file there is an activation failure at
         startup, not a degraded feature.

      2. Every imported DLL of every executable and library in the folder resolves either
         to a file sitting next to it or to a DLL the operating system itself provides.
         Anything else is a dependency the package forgot, and it is exactly the class of
         bug that only shows up on a machine other than the developer's.

    The import tables are parsed directly out of the PE files, so the check needs no
    Visual Studio environment, no dumpbin, and above all does not execute anything.

    API sets (api-ms-win-*.dll, ext-ms-win-*.dll) are virtual names resolved by the
    loader from the host's API set schema; they never exist as files and are treated as
    OS-provided. So is anything found in the system directory, which is how the CRT and
    the Win32 surface are classified.

.PARAMETER Path
    Folder to verify. Defaults to the newest dist\TaskManagerPlusPlus-* folder, or dist
    itself when there is only one.

.PARAMETER Quiet
    Print only the summary.

.EXAMPLE
    tools\verify-portable.ps1
    tools\verify-portable.ps1 -Path dist\TaskManagerPlusPlus-0.1.0-win-x64
#>
[CmdletBinding()]
param(
    [string]$Path,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message) {
    if (-not $Quiet) { Write-Host $Message }
}

if (-not ('Tmpp.PortableCheck' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Tmpp
{
    /// <summary>
    /// Reads the embedded manifest and the import table of a PE file. Everything is done
    /// on an in-memory copy of the file, so nothing is loaded and no code runs.
    /// </summary>
    public static class PortableCheck
    {
        private const int RT_MANIFEST = 24;
        private const uint LOAD_LIBRARY_AS_DATAFILE = 0x00000002;

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FreeLibrary(IntPtr hModule);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr FindResource(IntPtr hModule, IntPtr lpName, IntPtr lpType);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadResource(IntPtr hModule, IntPtr hResInfo);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LockResource(IntPtr hResData);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint SizeofResource(IntPtr hModule, IntPtr hResInfo);

        /// <summary>The RT_MANIFEST resource of a module as text, or null when it has none.</summary>
        public static string ReadEmbeddedManifest(string module)
        {
            IntPtr h = LoadLibraryEx(module, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE);
            if (h == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "LoadLibraryEx failed for " + module);
            }

            try
            {
                IntPtr info = FindResource(h, (IntPtr)1, (IntPtr)RT_MANIFEST);
                if (info == IntPtr.Zero)
                {
                    return null;
                }

                uint size = SizeofResource(h, info);
                IntPtr loaded = LoadResource(h, info);
                if (loaded == IntPtr.Zero)
                {
                    return null;
                }

                IntPtr data = LockResource(loaded);
                if (data == IntPtr.Zero || size == 0)
                {
                    return null;
                }

                byte[] buffer = new byte[size];
                Marshal.Copy(data, buffer, 0, (int)size);

                // The resource may or may not carry a UTF-8 BOM; either way the XML
                // declaration inside is what the parser wants to see, so drop the BOM.
                int start = (buffer.Length >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) ? 3 : 0;
                return Encoding.UTF8.GetString(buffer, start, buffer.Length - start);
            }
            finally
            {
                FreeLibrary(h);
            }
        }

        /// <summary>
        /// Names of the DLLs a PE file imports, lower-cased for comparison. Returns an
        /// empty list for anything that is not a PE image.
        /// </summary>
        public static List<string> ReadImports(string path)
        {
            List<string> names = new List<string>();
            byte[] b = File.ReadAllBytes(path);

            if (b.Length < 0x40)
            {
                return names;
            }

            int pe = BitConverter.ToInt32(b, 0x3C);
            if (pe <= 0 || pe + 24 > b.Length || b[pe] != (byte)'P' || b[pe + 1] != (byte)'E')
            {
                return names;
            }

            int coff = pe + 4;
            int sectionCount = BitConverter.ToUInt16(b, coff + 2);
            int optionalSize = BitConverter.ToUInt16(b, coff + 16);
            int optional = coff + 20;
            if (optional + 2 > b.Length)
            {
                return names;
            }

            // The data directory sits at a different offset in PE32 and PE32+.
            ushort magic = BitConverter.ToUInt16(b, optional);
            int dataDirectory = optional + (magic == 0x20B ? 112 : 96);
            if (dataDirectory + 16 > b.Length)
            {
                return names;
            }

            int importRva = BitConverter.ToInt32(b, dataDirectory + 8);   // entry 1 = imports
            if (importRva == 0)
            {
                return names;
            }

            int sections = optional + optionalSize;

            // Translate a relative virtual address into a file offset using the section
            // table. Sizes are compared against both the virtual and the raw size because
            // a section may be padded in either direction.
            Func<int, int> toOffset = delegate(int rva)
            {
                for (int i = 0; i < sectionCount; i++)
                {
                    int s = sections + i * 40;
                    if (s + 40 > b.Length)
                    {
                        break;
                    }

                    uint virtualSize = BitConverter.ToUInt32(b, s + 8);
                    uint virtualAddress = BitConverter.ToUInt32(b, s + 12);
                    uint rawSize = BitConverter.ToUInt32(b, s + 16);
                    uint rawPointer = BitConverter.ToUInt32(b, s + 20);
                    uint span = Math.Max(virtualSize, rawSize);

                    if (rva >= virtualAddress && rva < virtualAddress + span)
                    {
                        return (int)(rawPointer + (rva - virtualAddress));
                    }
                }

                return -1;
            };

            int descriptor = toOffset(importRva);
            if (descriptor < 0)
            {
                return names;
            }

            // IMAGE_IMPORT_DESCRIPTOR: 20 bytes each, terminated by an all-zero entry.
            while (descriptor + 20 <= b.Length)
            {
                int nameRva = BitConverter.ToInt32(b, descriptor + 12);
                int originalThunk = BitConverter.ToInt32(b, descriptor + 0);
                int firstThunk = BitConverter.ToInt32(b, descriptor + 16);

                if (nameRva == 0 && originalThunk == 0 && firstThunk == 0)
                {
                    break;
                }

                if (nameRva != 0)
                {
                    int at = toOffset(nameRva);
                    if (at >= 0)
                    {
                        int end = at;
                        while (end < b.Length && b[end] != 0)
                        {
                            end++;
                        }

                        names.Add(Encoding.ASCII.GetString(b, at, end - at).ToLowerInvariant());
                    }
                }

                descriptor += 20;
            }

            return names;
        }
    }
}
'@
}

# --- Resolve the folder to verify -------------------------------------------
if (-not $Path) {
    $distRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'
    if (-not (Test-Path -LiteralPath $distRoot)) {
        throw "No dist folder found at $distRoot. Run tools\package.ps1 first, or pass -Path."
    }

    $candidates = Get-ChildItem -LiteralPath $distRoot -Directory | Where-Object { $_.Name -like 'TaskManagerPlusPlus-*' }
    if ($candidates.Count -eq 1) {
        $Path = $candidates[0].FullName
    }
    elseif ($candidates.Count -gt 1) {
        $Path = ($candidates | Sort-Object LastWriteTime -Descending)[0].FullName
    }
    else {
        $Path = $distRoot
    }
}

$Path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
if (-not (Test-Path -LiteralPath $Path)) {
    throw "Folder not found: $Path"
}

Write-Step "Verifying $Path"

$binaries = Get-ChildItem -LiteralPath $Path -Recurse -File |
    Where-Object { $_.Extension -in '.exe', '.dll' } |
    Sort-Object FullName

if ($binaries.Count -eq 0) {
    throw "No .exe or .dll files under $Path"
}

$problems = New-Object System.Collections.ArrayList

# --- 1. The reg-free WinRT manifest ----------------------------------------
$app = Join-Path $Path 'tmpp.exe'
if (-not (Test-Path -LiteralPath $app)) {
    throw "tmpp.exe is not in $Path"
}

$manifest = [Tmpp.PortableCheck]::ReadEmbeddedManifest($app)
if (-not $manifest) {
    [void]$problems.Add('tmpp.exe embeds no manifest, so reg-free WinRT activation cannot work')
}
else {
    $xml = [xml]$manifest
    $referenced = @($xml.SelectNodes('//*[local-name()="file"]') | ForEach-Object { $_.GetAttribute('name') } | Where-Object { $_ })
    $missing = @($referenced | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Path $_)) })

    Write-Step ("  manifest: {0} file reference(s), {1} missing" -f $referenced.Count, $missing.Count)
    foreach ($name in $missing) {
        [void]$problems.Add("manifest references $name, which is not in the package")
    }
}

# --- 2. Import closure ------------------------------------------------------
$systemDirectory = Join-Path $env:windir 'System32'
$present = @{}
foreach ($file in Get-ChildItem -LiteralPath $Path -Recurse -File) {
    $present[$file.Name.ToLowerInvariant()] = $true
}

$resolved = 0
$fromSystem = 0
$missingImports = @{}

foreach ($binary in $binaries) {
    foreach ($import in [Tmpp.PortableCheck]::ReadImports($binary.FullName)) {
        if ($present.ContainsKey($import)) {
            $resolved++
            continue
        }

        # API sets are virtual: the loader maps them through the host's schema and no
        # such file exists on disk.
        if ($import -like 'api-ms-win-*' -or $import -like 'ext-ms-win-*') {
            $fromSystem++
            continue
        }

        if (Test-Path -LiteralPath (Join-Path $systemDirectory $import)) {
            $fromSystem++
            continue
        }

        if (-not $missingImports.ContainsKey($import)) {
            $missingImports[$import] = New-Object System.Collections.ArrayList
        }
        [void]$missingImports[$import].Add($binary.Name)
    }
}

foreach ($import in ($missingImports.Keys | Sort-Object)) {
    $importers = ($missingImports[$import] | Select-Object -First 3) -join ', '
    $more = if ($missingImports[$import].Count -gt 3) { " (+$($missingImports[$import].Count - 3) more)" } else { '' }
    [void]$problems.Add("unresolved import $import, required by $importers$more")
}

Write-Step ("  imports : {0} resolved locally, {1} provided by Windows, {2} unresolved" -f $resolved, $fromSystem, $missingImports.Count)

# --- Summary ----------------------------------------------------------------
$totalBytes = (Get-ChildItem -LiteralPath $Path -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ''
if ($problems.Count -eq 0) {
    Write-Host ("PORTABLE_OK  {0} file(s), {1:N1} MB, {2} binaries checked" -f (Get-ChildItem -LiteralPath $Path -Recurse -File).Count, ($totalBytes / 1MB), $binaries.Count)
    exit 0
}

Write-Host ("PORTABLE_FAILED  {0} problem(s):" -f $problems.Count)
foreach ($problem in $problems) {
    Write-Host "  - $problem"
}
exit 1
