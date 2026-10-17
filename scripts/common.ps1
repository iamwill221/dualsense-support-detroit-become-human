# Shared helpers for install.ps1 / uninstall.ps1.

$GameExe = 'DetroitBecomeHuman.exe'
$GameSubdir = 'steamapps\common\Detroit Become Human'

# Finds the game folder in every Steam library listed in libraryfolders.vdf.
function Find-GameDir {
    $steam = $null
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
        try {
            $p = Get-ItemProperty $key -ErrorAction Stop
            $steam = if ($p.SteamPath) { $p.SteamPath } else { $p.InstallPath }
            if ($steam) { break }
        } catch {}
    }
    $libraries = @()
    if ($steam) {
        $steam = $steam -replace '/', '\'
        $libraries += $steam
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libraries += $m.Groups[1].Value -replace '\\\\', '\'
            }
        }
    }
    $libraries += 'C:\Program Files (x86)\Steam'
    foreach ($lib in $libraries | Select-Object -Unique) {
        $dir = Join-Path $lib $GameSubdir
        if (Test-Path (Join-Path $dir $GameExe)) { return $dir }
    }
    return $null
}

function Resolve-GameDir([string]$GameDir) {
    if (-not $GameDir) { $GameDir = Find-GameDir }
    while (-not $GameDir -or -not (Test-Path (Join-Path $GameDir $GameExe))) {
        if ($GameDir) { Write-Host "  '$GameDir' does not contain $GameExe." -ForegroundColor Yellow }
        else { Write-Host "Could not find Detroit: Become Human automatically." -ForegroundColor Yellow }
        Write-Host "  Tip: in Steam, right-click the game > Manage > Browse local files, then copy the folder path."
        $GameDir = (Read-Host "Paste the game folder path (or leave empty to cancel)").Trim('"', ' ')
        if (-not $GameDir) { throw "Cancelled." }
    }
    return $GameDir
}

function Assert-GameClosed {
    while (Get-Process DetroitBecomeHuman -ErrorAction SilentlyContinue) {
        Write-Host "The game is running. Close it, then press Enter." -ForegroundColor Yellow
        [void](Read-Host)
    }
}

# True if this file is our proxy (it references the renamed original).
function Test-IsProxy([string]$path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    return [Text.Encoding]::ASCII.GetString($bytes).Contains('libScePad_orig.dll')
}

function Test-CanWrite([string]$dir) {
    $probe = Join-Path $dir ".dsproxy_write_test"
    try { [IO.File]::WriteAllText($probe, ''); Remove-Item $probe -Force; return $true } catch { return $false }
}

# Re-runs the calling script as administrator with the same game folder.
function Restart-Elevated([string]$script, [string]$GameDir, [switch]$Pause) {
    Write-Host "Administrator rights are needed to write to the game folder. Asking Windows..."
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$script`"", '-GameDir', "`"$GameDir`"")
    if ($Pause) { $argList += '-Pause' }
    Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait
}

function Write-Done([string]$msg) { Write-Host ""; Write-Host $msg -ForegroundColor Green }
