# Removes the proxy and restores the game's original libScePad.dll.
param([string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Detroit Become Human')
$ErrorActionPreference = 'Stop'
$dll = Join-Path $GameDir 'libScePad.dll'
$orig = Join-Path $GameDir 'libScePad_orig.dll'

if (-not (Test-Path $orig)) { throw "libScePad_orig.dll not found; nothing to restore (or use Steam > Verify integrity)" }
if (Get-Process DetroitBecomeHuman -ErrorAction SilentlyContinue) { throw "Close the game first" }

Remove-Item $dll -Force -ErrorAction SilentlyContinue
Move-Item $orig $dll
Write-Host "original libScePad.dll restored"
