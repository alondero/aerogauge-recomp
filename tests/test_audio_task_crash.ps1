# Regression test for the aspMain "Unhandled jump target" crash (2026-07-17).
#
# Repro chain: a zero AI backlog report (headless ideal-drain always; windowed underruns
# occasionally) made the game's mixer request maximum-length audio frames (3+ subframes per
# RSP task). Oversized frames hit a latent bug in the game's own command-list builder — a
# voice whose pull yields zero samples emits A_ENVMIXER with a stale SETBUFF count — and the
# envmixer's wet-buffer writes then wrap past DMEM 0x1000, shredding the ACMD dispatch table.
# Pre-fix this crashed run 1 of every headless warp race within ~30 s. The fix is the
# virtual AI FIFO in src/aero_audio.cpp (console-rate drain model) plus fail-soft RSP task
# handling in patches/0013.
#
# Requires the ROM + built exe; skipped (exit 0 with a notice) when either is missing.
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$exe  = Join-Path $repo "build\aerogauge_modern.exe"
$rom  = Join-Path $repo "AeroGauge (USA).z64"
if (-not (Test-Path $exe) -or (-not (Test-Path $rom))) {
    Write-Host "SKIP: needs build\aerogauge_modern.exe and the ROM"
    exit 0
}
$log = Join-Path ([System.IO.Path]::GetTempPath()) "aero_test_audio_task_crash.log"
$env:AERO_WARP = "1"
$env:AERO_MODERN_MAX_VIS = "3600"   # 60 s self-exiting race (music + reverb active)
$env:AERO_HEADLESS = "1"            # no audio device: the historically crash-prone path
$p = Start-Process -FilePath $exe -WorkingDirectory $repo -RedirectStandardError $log -PassThru -WindowStyle Hidden -Wait
$txt = Get-Content $log -Raw
Remove-Item Env:AERO_WARP, Env:AERO_MODERN_MAX_VIS, Env:AERO_HEADLESS -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0) {
    Write-Host "FAIL: exit code $($p.ExitCode); log: $log"
    exit 1
}
if ($txt.Contains("Unhandled jump target") -or $txt.Contains("exited unexpectedly")) {
    Write-Host "FAIL: RSP microcode crash signature in stderr; log: $log"
    exit 1
}
if (-not $txt.Contains("first NON-SILENT buffer")) {
    Write-Host "FAIL: audio pipeline produced no PCM (crash fix must not silence audio); log: $log"
    exit 1
}
Write-Host "PASS: 3600-VI headless race, audio live, no RSP crash"
exit 0
