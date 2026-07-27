# run_suite.ps1 - the canonical headless regression runner (V179 test retrofit).
#
#   powershell -File tests\run_suite.ps1              # full 137-script suite
#   powershell -File tests\run_suite.ps1 -Filter vet  # only scripts matching *vet*
#   powershell -File tests\run_suite.ps1 -Exe build-x\openwarband.exe
#
# Pass criterion: every script prints "harness: done" AND the exe exits 0.
# Per-test stdout lands in build\testout\<name>.txt for meaning-checks
# (state dumps) - the suite proves "didn't wedge or crash", the outputs
# prove the mechanism; read them for anything you just changed.
# Exit code: number of failures (0 = green).
param(
    [string]$Exe = "build\openwarband.exe",
    [string]$Filter = "*",
    [switch]$Quiet
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot          # repo root (tests\..)
Set-Location $root
if (-not (Test-Path $Exe)) { Write-Host "run_suite: no exe at $Exe"; exit 1 }
$outDir = Join-Path (Split-Path -Parent $Exe) "testout"
New-Item -ItemType Directory -Force $outDir | Out-Null

$scripts = Get-ChildItem "tests\*.txt" | Where-Object { $_.BaseName -like $Filter }
if (-not $scripts) { Write-Host "run_suite: nothing matches '$Filter'"; exit 1 }

$fail = @(); $slow = @(); $sw = [Diagnostics.Stopwatch]::StartNew()
foreach ($s in $scripts) {
    $o = Join-Path $outDir "$($s.BaseName).txt"
    $t = [Diagnostics.Stopwatch]::StartNew()
    # relative, unquoted: repo paths are space-free, and cmd's /c quote
    # stripping rules make nested quotes more fragile than none
    cmd /c "$Exe --script tests\$($s.Name) > $o 2>&1"
    $code = $LASTEXITCODE
    $t.Stop()
    $txt = Get-Content $o -Raw -ErrorAction SilentlyContinue
    $ok = ($code -eq 0) -and $txt -and ($txt -match "harness: done")
    if (-not $ok) {
        $fail += "$($s.BaseName) (exit $code$(if ($txt -notmatch 'harness: done') { ', no done marker' }))"
        if (-not $Quiet) { Write-Host "FAIL  $($s.BaseName)" }
    }
    if ($t.Elapsed.TotalSeconds -gt 20) { $slow += "{0} ({1:n1}s)" -f $s.BaseName, $t.Elapsed.TotalSeconds }
}
$sw.Stop()
Write-Host ("suite: {0}/{1} passed in {2:n0}s" -f ($scripts.Count - $fail.Count), $scripts.Count, $sw.Elapsed.TotalSeconds)
if ($slow -and -not $Quiet) { Write-Host "slowest: $($slow -join ', ')" }
if ($fail) { Write-Host "FAILED:"; $fail | ForEach-Object { Write-Host "  $_" } }
exit $fail.Count
