<#
.SYNOPSIS
    Extracts a complete, multi-resolution icon group out of a PE module into a .ico file.

.DESCRIPTION
    Windows stores an icon as two kinds of resource: an RT_GROUP_ICON directory that
    lists the images, and one RT_ICON per image. This script reads both and rebuilds a
    standalone .ico, preserving every size and colour depth in the group.

    It exists because the application icon shipped in src/App/app.ico was taken from the
    Windows Task Manager, and a checked-in binary asset should be reproducible rather
    than a mystery. Re-run it to regenerate that file:

        tools\extract-icon.ps1 -Module $env:windir\System32\taskmgr.exe -List
        tools\extract-icon.ps1 -Module $env:windir\System32\taskmgr.exe -Output src\App\app.ico

    Without -GroupId the lowest-numbered group is used, which is the one the shell
    shows for the executable.

.PARAMETER Module
    Path to the .exe or .dll to read. The module is opened as a data file, so it is
    never loaded or executed.

.PARAMETER Output
    Path of the .ico to write.

.PARAMETER GroupId
    Numeric id of the RT_GROUP_ICON to extract. Defaults to the lowest one.

.PARAMETER List
    Only list the icon groups and their image sizes; write nothing.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Module,
    [string]$Output,
    [int]$GroupId = -1,
    [switch]$List
)

$ErrorActionPreference = 'Stop'

# Both paths are handed to Win32 through .NET, which resolves relative paths against
# the process working directory rather than PowerShell's location. Resolving them here
# keeps a relative -Module or -Output meaning what the caller expects.
$Module = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Module)

if (-not (Test-Path -LiteralPath $Module)) {
    throw "Module not found: $Module"
}

if (-not ('Tmpp.IconExtractor' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

namespace Tmpp
{
    /// <summary>One image inside an RT_GROUP_ICON directory.</summary>
    public sealed class IconImage
    {
        public byte Width;
        public byte Height;
        public byte ColorCount;
        public ushort Planes;
        public ushort BitCount;
        public uint BytesInRes;
        public ushort Id;
    }

    /// <summary>
    /// Reads icon resources straight out of a PE file and reassembles them into the
    /// layout of a .ico file. See the MSDN entries for GRPICONDIR and ICONDIR: the two
    /// differ only in that the group directory names each image by resource id where the
    /// .ico names it by file offset.
    /// </summary>
    public static class IconExtractor
    {
        private const int RT_ICON = 3;
        private const int RT_GROUP_ICON = 14;
        private const uint LOAD_LIBRARY_AS_DATAFILE = 0x00000002;

        private delegate bool EnumResNameProc(IntPtr hModule, IntPtr lpszType, IntPtr lpszName, IntPtr lParam);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FreeLibrary(IntPtr hModule);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool EnumResourceNames(IntPtr hModule, IntPtr lpszType, EnumResNameProc lpEnumFunc, IntPtr lParam);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr FindResource(IntPtr hModule, IntPtr lpName, IntPtr lpType);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadResource(IntPtr hModule, IntPtr hResInfo);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LockResource(IntPtr hResData);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint SizeofResource(IntPtr hModule, IntPtr hResInfo);

        /// <summary>Numeric names of every resource of the given type, in resource order.</summary>
        public static List<int> EnumerateIds(string module, int type)
        {
            IntPtr h = LoadLibraryEx(module, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE);
            if (h == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "LoadLibraryEx failed for " + module);
            }

            try
            {
                List<int> ids = new List<int>();
                EnumResNameProc callback = delegate(IntPtr m, IntPtr t, IntPtr name, IntPtr p)
                {
                    // Named resources are not used for icons; only integer ids are.
                    if (((ulong)name >> 16) == 0)
                    {
                        ids.Add((int)name);
                    }
                    return true;
                };

                if (!EnumResourceNames(h, (IntPtr)type, callback, IntPtr.Zero))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "EnumResourceNames failed");
                }

                return ids;
            }
            finally
            {
                FreeLibrary(h);
            }
        }

        /// <summary>Raw bytes of one resource, or null when it is absent.</summary>
        private static byte[] ReadResource(IntPtr h, int type, int id)
        {
            IntPtr info = FindResource(h, (IntPtr)id, (IntPtr)type);
            if (info == IntPtr.Zero)
            {
                return null;
            }

            uint size = SizeofResource(h, info);
            if (size == 0)
            {
                return null;
            }

            IntPtr loaded = LoadResource(h, info);
            if (loaded == IntPtr.Zero)
            {
                return null;
            }

            IntPtr data = LockResource(loaded);
            if (data == IntPtr.Zero)
            {
                return null;
            }

            byte[] buffer = new byte[size];
            Marshal.Copy(data, buffer, 0, (int)size);
            return buffer;
        }

        private static ushort ReadUInt16(byte[] b, int offset)
        {
            return (ushort)(b[offset] | (b[offset + 1] << 8));
        }

        private static void WriteUInt16(byte[] b, int offset, ushort value)
        {
            b[offset] = (byte)(value & 0xFF);
            b[offset + 1] = (byte)((value >> 8) & 0xFF);
        }

        private static void WriteUInt32(byte[] b, int offset, uint value)
        {
            b[offset] = (byte)(value & 0xFF);
            b[offset + 1] = (byte)((value >> 8) & 0xFF);
            b[offset + 2] = (byte)((value >> 16) & 0xFF);
            b[offset + 3] = (byte)((value >> 24) & 0xFF);
        }

        /// <summary>Parses the GRPICONDIR of a group into one IconImage per member.</summary>
        public static List<IconImage> ReadGroup(string module, int groupId)
        {
            IntPtr h = LoadLibraryEx(module, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE);
            if (h == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "LoadLibraryEx failed");
            }

            try
            {
                byte[] group = ReadResource(h, RT_GROUP_ICON, groupId);
                if (group == null)
                {
                    throw new InvalidOperationException("RT_GROUP_ICON " + groupId + " not found");
                }

                int count = ReadUInt16(group, 4);
                List<IconImage> images = new List<IconImage>(count);
                for (int i = 0; i < count; i++)
                {
                    int at = 6 + i * 14;
                    IconImage image = new IconImage();
                    image.Width = group[at];
                    image.Height = group[at + 1];
                    image.ColorCount = group[at + 2];
                    image.Planes = ReadUInt16(group, at + 4);
                    image.BitCount = ReadUInt16(group, at + 6);
                    image.BytesInRes = (uint)(group[at + 8] | (group[at + 9] << 8) | (group[at + 10] << 16) | (group[at + 11] << 24));
                    image.Id = ReadUInt16(group, at + 12);
                    images.Add(image);
                }

                return images;
            }
            finally
            {
                FreeLibrary(h);
            }
        }

        /// <summary>Writes the group as a standalone .ico, keeping every member.</summary>
        public static void WriteIcon(string module, int groupId, string outputPath)
        {
            List<IconImage> images = ReadGroup(module, groupId);
            if (images.Count == 0)
            {
                throw new InvalidOperationException("Icon group " + groupId + " is empty");
            }

            IntPtr h = LoadLibraryEx(module, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE);
            if (h == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "LoadLibraryEx failed");
            }

            try
            {
                List<byte[]> blobs = new List<byte[]>(images.Count);
                foreach (IconImage image in images)
                {
                    byte[] blob = ReadResource(h, RT_ICON, image.Id);
                    if (blob == null)
                    {
                        throw new InvalidOperationException("RT_ICON " + image.Id + " referenced by group " + groupId + " is missing");
                    }
                    if (blob.Length != image.BytesInRes)
                    {
                        throw new InvalidOperationException("RT_ICON " + image.Id + " is " + blob.Length + " bytes but the group declares " + image.BytesInRes);
                    }
                    blobs.Add(blob);
                }

                // ICONDIR (6 bytes) followed by one ICONDIRENTRY (16 bytes) per image,
                // then the image data laid out back to back.
                int headerSize = 6 + images.Count * 16;
                int total = headerSize;
                foreach (byte[] blob in blobs)
                {
                    total += blob.Length;
                }

                byte[] file = new byte[total];
                WriteUInt16(file, 0, 0);                       // reserved
                WriteUInt16(file, 2, 1);                       // type: icon
                WriteUInt16(file, 4, (ushort)images.Count);

                int offset = headerSize;
                for (int i = 0; i < images.Count; i++)
                {
                    IconImage image = images[i];
                    byte[] blob = blobs[i];
                    int at = 6 + i * 16;
                    file[at] = image.Width;
                    file[at + 1] = image.Height;
                    file[at + 2] = image.ColorCount;
                    file[at + 3] = 0;                          // reserved
                    WriteUInt16(file, at + 4, image.Planes);
                    WriteUInt16(file, at + 6, image.BitCount);
                    WriteUInt32(file, at + 8, (uint)blob.Length);
                    WriteUInt32(file, at + 12, (uint)offset);
                    Buffer.BlockCopy(blob, 0, file, offset, blob.Length);
                    offset += blob.Length;
                }

                string directory = Path.GetDirectoryName(Path.GetFullPath(outputPath));
                if (!string.IsNullOrEmpty(directory))
                {
                    Directory.CreateDirectory(directory);
                }

                File.WriteAllBytes(outputPath, file);
            }
            finally
            {
                FreeLibrary(h);
            }
        }
    }
}
'@
}

$groups = [Tmpp.IconExtractor]::EnumerateIds($Module, 14)
if ($groups.Count -eq 0) {
    throw "No RT_GROUP_ICON resources in $Module"
}
$groups = $groups | Sort-Object

if ($List) {
    Write-Host "Icon groups in $Module"
    foreach ($id in $groups) {
        $images = [Tmpp.IconExtractor]::ReadGroup($Module, $id)
        $shapes = $images | ForEach-Object {
            $w = if ($_.Width -eq 0) { 256 } else { [int]$_.Width }
            $h = if ($_.Height -eq 0) { 256 } else { [int]$_.Height }
            "$w`x$h/$($_.BitCount)bpp"
        }
        Write-Host ("  group {0,-6} {1,2} image(s): {2}" -f $id, $images.Count, ($shapes -join ', '))
    }
    return
}

if ($GroupId -lt 0) {
    $GroupId = $groups[0]
    Write-Host "Using lowest group id: $GroupId"
}

if (-not $Output) {
    throw "-Output is required unless -List is used."
}

$Output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)

[Tmpp.IconExtractor]::WriteIcon($Module, $GroupId, $Output)

$written = Get-Item -LiteralPath $Output
$images = [Tmpp.IconExtractor]::ReadGroup($Module, $GroupId)
Write-Host ("Wrote {0} ({1:N0} bytes, {2} image(s), group {3})" -f $written.FullName, $written.Length, $images.Count, $GroupId)
foreach ($image in $images) {
    $w = if ($image.Width -eq 0) { 256 } else { [int]$image.Width }
    $h = if ($image.Height -eq 0) { 256 } else { [int]$image.Height }
    Write-Host ("  {0,3}x{1,-3} {2,2}bpp {3,8:N0} bytes" -f $w, $h, $image.BitCount, $image.BytesInRes)
}
