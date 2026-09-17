<#
.SYNOPSIS
    Verifies FlashTool download configuration addresses against a packaged release.

.DESCRIPTION
    Compares the burn address of every [pkgflx*] section in a FlashTool download
    ini with the address recorded for the matching image in imagedata.json
    (XIP base 0x800000).  A stale configuration would silently overwrite the
    wrong flash region - for example writing customer_app2.bin over the tail of
    the application image - so the script exits non-zero on any mismatch or
    missing image.

.PARAMETER IniPath
    FlashTool download configuration, e.g. quec_download_usb.ini or
    quec_download_usb_incremental.ini.

.PARAMETER ImageDataJson
    imagedata.json produced with the package (release directory or the
    pkgimg_gen directory next to the ini).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts/verify-flash-config.ps1 `
        -IniPath 'G:\unirtos\CLI_test0717\quec_download_usb_incremental.ini' `
        -ImageDataJson 'G:\unirtos-validation\qpy-validation\qos_build\release\qpy-validation\imagedata.json'
#>
param(
    [Parameter(Mandatory = $true)][string]$IniPath,
    [Parameter(Mandatory = $true)][string]$ImageDataJson,
    [int]$XipBase = 0x800000
)

$ErrorActionPreference = 'Stop'

function Convert-HexOrDec([string]$Value) {
    $t = $Value.Trim()
    if ($t.StartsWith('0x', [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($t.Substring(2), 16)
    }
    return [Convert]::ToInt64($t, 10)
}

function Read-IniSections([string]$Path) {
    $sections = @{}
    $current = $null
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $current = $Matches[1].Trim().ToLowerInvariant()
            $sections[$current] = @{}
            continue
        }
        if ($null -eq $current -or $trimmed -eq '' -or $trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) {
            continue
        }
        $kv = $trimmed.Split('=', 2)
        if ($kv.Count -eq 2) {
            $sections[$current][$kv[0].Trim().ToLowerInvariant()] = $kv[1].Trim()
        }
    }
    return $sections
}

if (-not (Test-Path -LiteralPath $IniPath -PathType Leaf)) { throw "Ini not found: $IniPath" }
if (-not (Test-Path -LiteralPath $ImageDataJson -PathType Leaf)) { throw "imagedata.json not found: $ImageDataJson" }

$imageData = Get-Content -LiteralPath $ImageDataJson -Raw | ConvertFrom-Json
$imageByFile = @{}
foreach ($entry in $imageData.imageinfo) {
    $name = [System.IO.Path]::GetFileName([string]$entry.file).ToLowerInvariant()
    if ($name) { $imageByFile[$name] = [int64]$entry.addr }
}

$sections = Read-IniSections $IniPath
$failures = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]

foreach ($name in @('pkgflx0', 'pkgflx1', 'pkgflx2')) {
    if (-not $sections.ContainsKey($name)) {
        $failures.Add("$name section is missing")
        $rows.Add([pscustomobject]@{ Section=$name; Image='-'; IniAddr='-'; Expected='-'; Result='MISSING' })
        continue
    }
    $section = $sections[$name]
    $filePath = $section['filepath']
    $burnAddr = $section['burnaddr']
    if (-not $filePath -or -not $burnAddr) {
        $failures.Add("$name must define filepath and burnaddr")
        $rows.Add([pscustomobject]@{ Section=$name; Image='-'; IniAddr='-'; Expected='-'; Result='INCOMPLETE' })
        continue
    }
    $imageName = [System.IO.Path]::GetFileName($filePath)
    $key = $imageName.ToLowerInvariant()
    if (-not $imageByFile.ContainsKey($key)) {
        $failures.Add("$name references '$imageName', which is not part of the packaged images")
        $rows.Add([pscustomobject]@{ Section=$name; Image=$imageName; IniAddr=$burnAddr; Expected='-'; Result='NOT-IN-PACKAGE' })
        continue
    }
    $expected = $imageByFile[$key] - $XipBase
    $actual = Convert-HexOrDec $burnAddr
    $ok = ($actual -eq $expected)
    $rows.Add([pscustomobject]@{
        Section  = $name
        Image    = $imageName
        IniAddr  = ('0x{0:X}' -f $actual)
        Expected = ('0x{0:X}' -f $expected)
        Result   = $(if ($ok) { 'OK' } else { 'MISMATCH' })
    })
    if (-not $ok) {
        $failures.Add(("{0} burnaddr 0x{1:X} != expected 0x{2:X} for {3}" -f $name, $actual, $expected, $imageName))
    }
}

$rows | Format-Table -AutoSize | Out-Host
if ($failures.Count -gt 0) {
    Write-Host ''
    Write-Host 'FLASH CONFIG VERIFICATION FAILED:' -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    exit 1
}
Write-Host ''
Write-Host "FLASH CONFIG VERIFICATION PASSED: $IniPath" -ForegroundColor Green
exit 0
