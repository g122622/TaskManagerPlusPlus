<#
.SYNOPSIS
    Computes the release version YYMMDDNN and its derived representations.

.DESCRIPTION
    The product version is the local date as YYMMDD followed by a two-digit
    sequence number for that day: 26111901 is the first build on 19 Nov 2026.

    Windows Installer cannot use that string as a ProductVersion: an MSI
    ProductVersion is major.minor.build with major/minor at most 255 and build
    at most 65535. The same date is therefore mapped to YY.MM.(DD*100+NN), which
    preserves the ordering of YYMMDDNN and stays inside those limits. The
    executable's FILEVERSION uses the natural four-field form YY,MM,DD,NN.

    CI passes the full version because only it knows how many builds already ran
    today. A local invocation defaults to sequence 01; pass -Sequence to build
    more than one version on the same day.

.PARAMETER Version
    Full eight-digit version, for example 26111901. When omitted, today's date
    and -Sequence are used.

.PARAMETER Sequence
    Sequence number (1-99) for today when -Version is not supplied.

.PARAMETER Json
    Emit JSON instead of a PowerShell object, for callers that cannot consume
    the object directly.

.EXAMPLE
    tools\version.ps1

.EXAMPLE
    tools\version.ps1 -Sequence 3

.EXAMPLE
    tools\version.ps1 -Version 26111901 -Json
#>
[CmdletBinding()]
param(
    [string]$Version,
    [int]$Sequence = 1,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

if (-not $Version) {
    $Version = (Get-Date).ToString('yyMMdd') + ('{0:D2}' -f $Sequence)
}

if ($Version -notmatch '^(?<yy>\d{2})(?<mm>\d{2})(?<dd>\d{2})(?<nn>\d{2})$') {
    throw "version '$Version' is not YYMMDDNN (eight digits), e.g. 26111901"
}

$yy = [int]$Matches['yy']
$mm = [int]$Matches['mm']
$dd = [int]$Matches['dd']
$nn = [int]$Matches['nn']

# A date that does not exist must not become a version number.
try { [void](Get-Date -Year (2000 + $yy) -Month $mm -Day $dd) }
catch { throw "version '$Version' does not contain a valid date (YY=$yy MM=$mm DD=$dd)" }

if ($nn -lt 1 -or $nn -gt 99) {
    throw "version '$Version': the sequence must be 01..99"
}

$result = [pscustomobject]@{
    Display     = $Version
    FileVersion = ('{0}.{1}.{2}.{3}' -f $yy, $mm, $dd, $nn)
    FileMajor   = $yy
    FileMinor   = $mm
    FilePatch   = $dd
    FileBuild   = $nn
    MsiVersion  = ('{0}.{1}.{2}' -f $yy, $mm, ($dd * 100 + $nn))
}

if ($Json) { $result | ConvertTo-Json -Compress } else { $result }
