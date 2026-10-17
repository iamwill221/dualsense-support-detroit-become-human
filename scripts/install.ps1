# Installs the DualSense proxy into Detroit: Become Human, keeping the original
# libScePad.dll as libScePad_orig.dll. Safe to re-run (e.g. after a game update).
# Finds the game automatically; pass -GameDir to override.
param([string]$GameDir, [switch]$Pause)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

try {
    # release zip: DLL next to this script; repo: build\libScePad.dll
    $proxy = @((Join-Path $PSScriptRoot 'libScePad.dll'),
               (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\libScePad.dll')) |
             Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $proxy) { throw "libScePad.dll not found next to this script or in build\ (run scripts\build.ps1 first)." }

    $GameDir = Resolve-GameDir $GameDir
    Write-Host "Game folder: $GameDir"
    $dll = Join-Path $GameDir 'libScePad.dll'
    $orig = Join-Path $GameDir 'libScePad_orig.dll'
    if (-not (Test-Path $dll) -and -not (Test-Path $orig)) { throw "libScePad.dll is missing from the game folder. Use Steam > Verify integrity of game files, then try again." }

    if (-not (Test-CanWrite $GameDir)) {
        Restart-Elevated $PSCommandPath $GameDir -Pause:$Pause
        return
    }
    Assert-GameClosed

    if (-not (Test-Path $orig)) {
        if (Test-IsProxy $dll) { throw "The game's original libScePad.dll is gone. Use Steam > Verify integrity of game files, then try again." }
        Move-Item $dll $orig
        Write-Host "Original saved as libScePad_orig.dll"
    }
    Copy-Item $proxy $dll -Force

    Write-Done "DualSense support installed."
    Write-Host @"

One more step, in Steam:
  right-click Detroit: Become Human > Properties > Controller
  > set it to "Disable Steam Input".

Then connect your DualSense (USB or Bluetooth) and start the game.
If a game update ever removes DualSense support, just run Install again.
"@
} catch {
    Write-Host ""
    Write-Host "Install failed: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if ($Pause) { Write-Host ""; Read-Host "Press Enter to close" | Out-Null }
}
