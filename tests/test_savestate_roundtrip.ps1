# Regression test for the developer save-state round trip (#17, src/aero_savestate.c).
#
# Two headless runs. Run 1 warps into a race and auto-saves once the race is SETTLED
# (scene==request stable, race phase 3 -- the gate in aero_savestate_tick). Run 2 warps
# into the same race and restores the snapshot over it (the warp-first workflow from
# patches/0007), then must keep racing: the two historical failure modes were
#   - a crash/freeze from restoring under unsettled loader/audio threads (fixed by the
#     settled gate), and
#   - an instant time-over exit (phase 6 within 2 frames) from the restored osGetTime
#     anchor 0x8016C4F0 belonging to the save process's clock epoch (fixed by the
#     anchor rebase in do_load).
# The post-restore assertion is therefore: the load fired, AND the run's LAST scene
# transition is still the settled race (scene=5 ... phase=3) -- no phase-6/scene-6 walk.
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
$tmp   = [System.IO.Path]::GetTempPath()
$state = Join-Path $tmp "aero_test_savestate.astate"
$log1  = Join-Path $tmp "aero_test_savestate_save.log"
$log2  = Join-Path $tmp "aero_test_savestate_load.log"
Remove-Item $state -ErrorAction SilentlyContinue

# --- Run 1: capture -----------------------------------------------------------
$env:AERO_HEADLESS = "1"
$env:AERO_WARP = "1"
$env:AERO_STATE_SAVE = $state
$env:AERO_STATE_SAVE_DELAY = "120"
$env:AERO_MODERN_MAX_VIS = "3500"
$p = Start-Process -FilePath $exe -WorkingDirectory $repo -RedirectStandardError $log1 -PassThru -WindowStyle Hidden -Wait
Remove-Item Env:AERO_STATE_SAVE, Env:AERO_STATE_SAVE_DELAY -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0) {
    Write-Host "FAIL: capture run exit code $($p.ExitCode); log: $log1"
    exit 1
}
if (-not (Select-String -Path $log1 -Pattern "\[savestate\] saved" -Quiet)) {
    Write-Host "FAIL: capture run never saved a snapshot; log: $log1"
    exit 1
}

# --- Run 2: warp-first restore ------------------------------------------------
$env:AERO_STATE_LOAD = $state
$env:AERO_STATE_LOAD_SCENE = "5"    # fire at the settled warped race (warp-first workflow)
$env:AERO_STATE_LOAD_DELAY = "60"
$env:AERO_MODERN_MAX_VIS = "4500"
$p = Start-Process -FilePath $exe -WorkingDirectory $repo -RedirectStandardError $log2 -PassThru -WindowStyle Hidden -Wait
Remove-Item Env:AERO_HEADLESS, Env:AERO_WARP, Env:AERO_MODERN_MAX_VIS, `
            Env:AERO_STATE_LOAD, Env:AERO_STATE_LOAD_SCENE, Env:AERO_STATE_LOAD_DELAY -ErrorAction SilentlyContinue
if ($p.ExitCode -ne 0) {
    Write-Host "FAIL: restore run exit code $($p.ExitCode); log: $log2"
    exit 1
}
if (-not (Select-String -Path $log2 -Pattern "\[savestate\] loaded" -Quiet)) {
    Write-Host "FAIL: restore run never loaded the snapshot; log: $log2"
    exit 1
}
# Last state_probe transition must still be the settled race: a post-restore self-exit
# logs later transitions (phase=6, then scene=6) after the restore.
$lastLine = (Select-String -Path $log2 -Pattern "\[state\] vi=" | Select-Object -Last 1).Line
if ($lastLine -notmatch "scene=5 .*phase=3") {
    Write-Host "FAIL: post-restore world left the race (last transition: $lastLine); log: $log2"
    exit 1
}
Write-Host "PASS: settled race snapshot restored over a warped race and kept racing"
exit 0
