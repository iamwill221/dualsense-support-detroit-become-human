# Installs build\libScePad.dll into the game folder, keeping the original as libScePad_orig.dll.
# Safe to re-run, e.g. after a game update or "Verify integrity" restored the original.
param([string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Detroit Become Human')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$proxy = Join-Path $root 'build\libScePad.dll'
$dll = Join-Path $GameDir 'libScePad.dll'
$orig = Join-Path $GameDir 'libScePad_orig.dll'

if (-not (Test-Path $proxy)) { throw "Run scripts\build.ps1 first ($proxy missing)" }
if (-not (Test-Path $dll)) { throw "libScePad.dll not found in $GameDir" }
if (Get-Process DetroitBecomeHuman -ErrorAction SilentlyContinue) { throw "Close the game first" }

if (-not (Test-Path $orig)) {
    Move-Item $dll $orig
    Write-Host "original saved as libScePad_orig.dll"
}
Copy-Item $proxy $dll -Force
Write-Host "proxy installed. Remember: Steam Input must be disabled for this game."
