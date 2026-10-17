# Removes the DualSense proxy and restores the game's original libScePad.dll.
param([string]$GameDir, [switch]$Pause)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $GameDir = Resolve-GameDir $GameDir
    Write-Host "Game folder: $GameDir"
    $dll = Join-Path $GameDir 'libScePad.dll'
    $orig = Join-Path $GameDir 'libScePad_orig.dll'

    if (-not (Test-Path $orig)) {
        if ((Test-Path $dll) -and -not (Test-IsProxy $dll)) { Write-Done "Nothing to do: the original DLL is already in place."; return }
        throw "libScePad_orig.dll not found. Use Steam > Verify integrity of game files to restore the original."
    }
    if (-not (Test-CanWrite $GameDir)) {
        Restart-Elevated $PSCommandPath $GameDir -Pause:$Pause
        return
    }
    Assert-GameClosed

    Remove-Item $dll -Force -ErrorAction SilentlyContinue
    Move-Item $orig $dll
    Write-Done "DualSense support removed; the original libScePad.dll is restored."
    Write-Host "You can turn Steam Input back on for the game if you want."
} catch {
    Write-Host ""
    Write-Host "Uninstall failed: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if ($Pause) { Write-Host ""; Read-Host "Press Enter to close" | Out-Null }
}
