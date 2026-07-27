# run_render.ps1 - the renderer-migration smoke (V179 test retrofit).
#
#   powershell -File tests\run_render.ps1            # bench GL, then Vulkan
#   powershell -File tests\run_render.ps1 -Soldiers 600 -MaxRatio 1.6
#
# Replaces the hand-edit-settings.cfg ritual used through V161-V178. For
# each backend it runs --bench from INSIDE the build dir (so bench.txt and
# the settings edit never touch the repo root), then asserts:
#   raylib: bench.txt parses, avg_ms > 0
#   vulkan: the executor actually came up - the log must show
#           VULKAN DEVICE LIVE / FRAME EXECUTOR LIVE / SHADOW PASS LIVE
#           (shadow line skipped when shadows off in settings) - and
#           avg_ms <= MaxRatio * GL avg_ms (parity regression guard).
# The build-dir settings.cfg is restored no matter what (finally).
# Exit code: 0 green, 1 failure.
param(
    [string]$BuildDir = "build",
    [int]$Soldiers = 300,
    [double]$MaxRatio = 1.6
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$exe = Join-Path $BuildDir "openwarband.exe"
$cfg = Join-Path $BuildDir "assets\settings.cfg"
if (-not (Test-Path $exe)) { Write-Host "run_render: no exe at $exe"; exit 1 }
if (-not (Test-Path $cfg)) { Write-Host "run_render: no settings at $cfg"; exit 1 }

function Invoke-Bench([string]$renderer, [string]$log) {
    $c = Get-Content $cfg -Raw
    if ($c -match "renderer \w+") { $c = $c -replace "renderer \w+", "renderer $renderer" }
    else { $c += "`nrenderer $renderer`n" }
    [System.IO.File]::WriteAllText((Resolve-Path $cfg), $c)
    # cd inside cmd itself: PowerShell's location does not move the process
    # CWD that child processes inherit. Running from build\ keeps bench.txt
    # and the settings read inside the build dir.
    # (explicit exe path: NoDefaultCurrentDirectoryInExePath may disable
    # cmd's CWD search, so a bare name is not found even after cd)
    $abs = (Resolve-Path $BuildDir).Path
    cmd /c "cd /d `"$abs`" && `"$abs\openwarband.exe`" --bench $Soldiers > $log 2>&1"
    $b = Get-Content (Join-Path $BuildDir "bench.txt") -Raw
    $m = [regex]::Match($b, "avg_ms=([0-9.]+)")
    if (-not $m.Success) { throw "bench.txt unparseable after $renderer run" }
    [double]$m.Groups[1].Value
}

$orig = Get-Content $cfg -Raw
$failed = $false
try {
    $gl = Invoke-Bench "raylib" "bench_gl.log"
    Write-Host ("GL     avg {0:n2} ms" -f $gl)

    $vk = Invoke-Bench "vulkan" "bench_vk.log"
    Write-Host ("VULKAN avg {0:n2} ms" -f $vk)

    $log = Get-Content (Join-Path $BuildDir "bench_vk.log") -Raw
    $shadowsOn = $orig -notmatch "shadows off"
    $want = @("VULKAN DEVICE LIVE", "VULKAN FRAME EXECUTOR LIVE")
    if ($shadowsOn) { $want += "VULKAN SHADOW PASS LIVE" }
    foreach ($w in $want) {
        if ($log -notmatch [regex]::Escape($w)) { Write-Host "MISSING: $w"; $failed = $true }
        else { Write-Host "ok: $w" }
    }
    if ($vk -gt $gl * $MaxRatio) {
        Write-Host ("PARITY FAIL: vulkan {0:n2}ms > {1} x GL {2:n2}ms" -f $vk, $MaxRatio, $gl)
        $failed = $true
    }
} catch {
    Write-Host "run_render: $_"
    $failed = $true
} finally {
    [System.IO.File]::WriteAllText((Resolve-Path $cfg), $orig)   # always restore
}
if ($failed) { exit 1 }
Write-Host "render smoke: green (both backends)"
exit 0
