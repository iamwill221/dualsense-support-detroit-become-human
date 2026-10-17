# Builds build\libScePad.dll (and the test tools with -Tools).
# Uses `zig cc` from PATH, or falls back to the ziglang pip package (`pip install ziglang`).
param(
    [switch]$DebugLog,  # log every button / stick change to %TEMP%\dualsense_proxy.log
    [switch]$Tools      # also build tools\*.exe
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Force $out | Out-Null

if (Get-Command zig -ErrorAction SilentlyContinue) {
    $zig = @('zig')
} else {
    python -c "import ziglang" 2>$null
    if ($LASTEXITCODE -ne 0) { throw "zig not found. Install it, or run: pip install ziglang" }
    $zig = @('python', '-m', 'ziglang')
}
function Invoke-Zig([string[]]$ccArgs) {
    $exe = $zig[0]
    $pre = @($zig | Select-Object -Skip 1)
    & $exe @pre cc -target x86_64-windows-gnu @ccArgs
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

$flags = @('-shared', '-O2', '-Wall')
if ($DebugLog) { $flags += '-DDS_PROXY_DEBUG' }
Invoke-Zig ($flags + @('-o', "$out\libScePad.dll", "$root\src\libScePad.c", '-lsetupapi', '-lhid'))
Write-Host "built $out\libScePad.dll"

if ($Tools) {
    Invoke-Zig @('-O1', '-o', "$out\padtest.exe", "$root\tools\padtest.c")
    Invoke-Zig @('-O1', '-o', "$out\perf.exe", "$root\tools\perf.c")
    Invoke-Zig @('-O1', '-o', "$out\ditest.exe", "$root\tools\ditest.c", '-ldinput8', '-ldxguid')
    Write-Host "built tools in $out"
}
