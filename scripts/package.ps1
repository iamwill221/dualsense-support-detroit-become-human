# Builds the player-facing zip: dist\Detroit-DualSense-<version>.zip
# containing libScePad.dll, Install.bat, Uninstall.bat, the scripts they run and README.txt.
param([string]$Version = (Get-Date -Format 'yyyy.MM.dd'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot 'build.ps1')

$name = "Detroit-DualSense-$Version"
$stage = Join-Path $root "dist\$name"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item "$root\build\libScePad.dll" $stage
Copy-Item "$root\package\*" $stage
Copy-Item "$root\scripts\install.ps1", "$root\scripts\uninstall.ps1", "$root\scripts\common.ps1" $stage

$zip = Join-Path $root "dist\$name.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "packaged $zip"
