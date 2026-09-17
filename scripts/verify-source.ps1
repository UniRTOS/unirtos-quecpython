[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$failures = [System.Collections.Generic.List[string]]::new()
$legacyMarkers = @(
    ('G:' + '\Workspace'),
    ('pythonSDK_' + 'UnirtosV1.1'),
    ('Temp' + '\Felix')
)

foreach ($file in Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Force) {
    $relative = $file.FullName.Substring($resolvedRoot.Length + 1)

    if ($file.Extension -eq '.pyc' -or $relative -match '(^|[\\/])__pycache__([\\/]|$)') {
        $failures.Add("Python cache artifact: $relative")
    }

    if ($file.Length -le 8MB -and $file.FullName -ne $PSCommandPath) {
        $text = [System.IO.File]::ReadAllText($file.FullName)
        foreach ($marker in $legacyMarkers) {
            if ($text.Contains($marker)) {
                $failures.Add("Legacy absolute-path marker '$marker': $relative")
            }
        }
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Source hygiene checks passed: $resolvedRoot"

