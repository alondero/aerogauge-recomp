<#
.SYNOPSIS
    Regression test: default play runs must not emit PERIODIC stderr lines from hot
    threads (frame-pacing hitch class).

.DESCRIPTION
    Root cause being locked down (2026-07-17): the gfx-thread [rt64] send_dl heartbeat
    and its swrender sibling [gfx] send_dl count= both printed to stderr once per second
    (count % 30 at this title's 30 fps). With stderr attached to a live Windows console,
    the synchronous console write measured 10-77 ms per line -- a visible hitch every
    second of play. The VI-thread [probe] fb swap # line (every 256 swaps, ~8.5 s) was
    the same defect class. All three are now gated behind AERO_HARNESS_LOG=1 (default off).

    Runs the real binary twice -- once on the headless / swrender path (no AERO_HEADLESS
    override; the binary's config hands swrender to ultramodern by default) and once with
    AERO_HEADLESS=0 forcing the RT64 context. For each run, ~900 VIs (~15 s; the
    heartbeat would fire ~15 times per path) with AERO_HARNESS_LOG unset. Asserts:
      1. sanity: the run actually exercised gfx + VI (one-time boot markers present);
      2. no "[gfx] send_dl count" heartbeat lines from EITHER renderer;
      3. no "[rt64] send_dl count" heartbeat lines (RT64 path);
      4. no "[probe] fb swap #N" lines beyond the 8 one-time boot lines.

    Requires the ROM at the repo root (same requirement as the build itself). The RT64
    leg needs a real graphics environment (RT64.dll in build/, Vulkan/D3D12 driver,
    Window); under WSLg without one, that leg will quickly fail at renderer init and is
    reported as skipped rather than failed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tests/test_play_logging_quiet.ps1 `
        -Exe build/aerogauge_modern.exe -RepoRoot .
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path $Exe).Path
$RepoRoot = (Resolve-Path $RepoRoot).Path

$env:AERO_MODERN_MAX_VIS = '900'
# Make sure both gates are off (the property under test) and the pacing probe file log is off.
Remove-Item Env:AERO_HARNESS_LOG -ErrorAction SilentlyContinue
Remove-Item Env:AERO_FRAME_LOG -ErrorAction SilentlyContinue

$tmpBase = Join-Path $env:TEMP ("aero_quiet_test_gfx_{0}" -f $PID)

# Returns $log = full stderr capture; $exit = the exit code (always 0 because the game
# self-exits on cap), $timedOut = $true if the watchdog had to kill the process.
function Invoke-Run {
    param([string]$ExtraEnv, [string]$Tag)
    $stderrPath = Join-Path $env:TEMP ("aero_quiet_test_stderr_{0}_{1}.txt" -f $PID, $Tag)
    $stdoutPath = Join-Path $env:TEMP ("aero_quiet_test_stdout_{0}_{1}.txt" -f $PID, $Tag)
    $envChild = "${tmpBase}_$Tag.json"
    # Create an isolated graphics config per leg so a write from one leg can't pollute the other.
    '{}' | Set-Content -Path $envChild -Encoding utf8
    $ps = [System.Diagnostics.ProcessStartInfo]::new()
    $ps.FileName = $Exe
    $ps.WorkingDirectory = $RepoRoot
    $ps.UseShellExecute = $false
    $ps.RedirectStandardError = $true
    $ps.RedirectStandardOutput = $true
    $ps.EnvironmentVariables['AERO_GRAPHICS_CONFIG'] = $envChild
    $ps.EnvironmentVariables['AERO_MODERN_MAX_VIS'] = '900'
    foreach ($kv in $ExtraEnv.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $name, $val = $kv.Split('=', 2)
        if ($val -eq $null) { $ps.EnvironmentVariables.Remove($name) }
        else                { $ps.EnvironmentVariables[$name] = $val }
    }
    # Gates off regardless of inherited state (don't trust host env).
    $ps.EnvironmentVariables.Remove('AERO_HARNESS_LOG')
    $ps.EnvironmentVariables.Remove('AERO_FRAME_LOG')
    $p = [System.Diagnostics.Process]::Start($ps)
    $exitedCleanly = $p.WaitForExit(120000)
    if (-not $exitedCleanly) { $p.Kill(); $p.WaitForExit() }
    $log = ($p.StandardError.ReadToEnd()) -as [string]
    if ($log -eq $null) { $log = '' }
    $exitCode = $p.ExitCode
    $p.Dispose()
    # Don't delete $stderrPath/$stdoutPath yet -- the check below logs them on FAIL.
    Remove-Item $envChild -ErrorAction SilentlyContinue
    [pscustomobject]@{
        Log       = $log
        TimedOut  = -not $exitedCleanly
        ExitCode  = $exitCode
        StderrPath = $stderrPath
        StdoutPath = $stdoutPath
    }
}

$failures = 0

function Test-RunQuietness {
    param([string]$Label, [string]$Log, [string[]]$PathMarkers, [string[]]$GfxMarkerKinds)
    Write-Host "--- Leg: $Label ---"
    # 1. Sanity: markers proving this render path actually ran must be present.
    # NOTE: .Contains(), not -like: [brackets] in the markers parse as wildcard
    # character classes under -like and never match.
    foreach ($marker in $PathMarkers) {
        if (-not $Log.Contains($marker)) {
            Write-Host "FAIL: $Label -- sanity marker missing: $marker (run never reached $Label path)"
            $script:failures++
        }
    }
    # 2/3. Each gfx-thread heartbeat line kind must be silent by default.
    foreach ($kind in $GfxMarkerKinds) {
        $hits = @([regex]::Matches($Log, [regex]::Escape($kind) + ' send_dl count='))
        if ($hits.Count -gt 0) {
            Write-Host "FAIL: $Label -- $($hits.Count) '$kind send_dl count=' heartbeat line(s) in a default run (must require AERO_HARNESS_LOG=1)"
            $script:failures++
        }
    }
    # 4. The VI-thread fb-swap line: only the 8 one-time boot lines are allowed.
    $swaps = @([regex]::Matches($Log, '\[probe\] fb swap #(\d+)'))
    foreach ($m in $swaps) {
        if ([int]$m.Groups[1].Value -gt 8) {
            Write-Host "FAIL: $Label -- periodic fb-swap line in a default run: $($m.Value) (must require AERO_HARNESS_LOG=1)"
            $script:failures++
            break
        }
    }
}

# Leg 1: default config boots RT64 (aero_rt64::enabled() = !AERO_HEADLESS=1). Asserts
# the [rt64] send_dl count= heartbeat is silent and the [probe] fb swap line is silent.
$r1 = Invoke-Run -ExtraEnv '' -Tag 'rt64'
if ($r1.TimedOut) {
    Write-Host "FAIL: game did not exit within 120 s on RT64 leg (AERO_MODERN_MAX_VIS=900 should stop it)"
    $failures++
} else {
    Test-RunQuietness -Label 'rt64' -Log $r1.Log `
        -PathMarkers @('[rt64] RT64 renderer initialised', '[probe] FIRST VI retrace') `
        -GfxMarkerKinds @('[rt64]')
}

# Leg 2: headless swrender path (AERO_HEADLESS=1 selects aero::headless::create_render_context
# as the renderer via the cfg in main.cpp). Asserts the headless [gfx] send_dl count=
# heartbeat is silent; the same [probe] fb swap line should also be quiet. Requires no
# graphics device, so runs in CI / WSLg without a window without skipping.
$r2 = Invoke-Run -ExtraEnv 'AERO_HEADLESS=1' -Tag 'swrender'
if ($r2.TimedOut) {
    Write-Host "FAIL: game did not exit within 120 s on swrender leg (AERO_MODERN_MAX_VIS=900 should stop it)"
    $failures++
} else {
    Test-RunQuietness -Label 'swrender' -Log $r2.Log `
        -PathMarkers @('[probe] FIRST VI retrace') `
        -GfxMarkerKinds @('[gfx]')
    if (-not $r2.Log.Contains('[gfx]')) {
        Write-Host "FAIL: swrender leg never exercised the gfx heartbeat path (run exited too early)"
        $failures++
    }
}

$pathsToRemove = @($tmpBase.json, $r1.StderrPath, $r1.StdoutPath, $r2.StderrPath, $r2.StdoutPath) | Where-Object { $_ -and (Test-Path $_ -ErrorAction SilentlyContinue) }
foreach ($p in $pathsToRemove) { Remove-Item $p -ErrorAction SilentlyContinue }

if ($failures -eq 0) {
    Write-Host "PASS: both render paths (RT64 default + headless swrender under AERO_HEADLESS=1) emitted no periodic hot-thread stderr lines"
    exit 0
}
exit 1
