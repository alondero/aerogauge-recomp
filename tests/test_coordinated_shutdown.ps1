<#
.SYNOPSIS
    Regression test: the coordinated game-thread shutdown must stop every guest
    thread and must not crash.

.DESCRIPTION
    The coordinated shutdown is the port's quit path (issue #114). The port
    requests the shutdown, the runtime releases the guest threads, and the port
    joins them before it lets the runtime release RDRAM. A guest thread that
    unwinds has to leave the guest message queue it was blocked on: the queues
    live in guest memory, so a stale entry makes a later send resume a thread
    whose context has already been released. That was a use-after-free, and it
    showed up as an intermittent EXCEPTION_ACCESS_VIOLATION in the scheduler.

    Runs the real binary with AERO_QUIT_AFTER_MS, which requests the same
    coordinated quit that closing the window requests. Asserts:
      1. the run reached the game (the VI retrace marker is present);
      2. the coordinated shutdown joined every guest thread;
      3. the process exited with status 0 and printed the boot summary;
      4. no native crash report was produced.

    Requires the ROM at the repo root (same requirement as the build itself).
    Runs on the headless path, so it needs no graphics device and no window.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tests/test_coordinated_shutdown.ps1 `
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

# No VI cap: the quit comes from the coordinated shutdown, not from the
# bounded headless exit.
$env:AERO_MODERN_MAX_VIS = '100000'
$env:AERO_HEADLESS = '1'
$env:AERO_QUIT_AFTER_MS = '4000'
$env:AERO_SHUTDOWN_JOIN_MS = '5000'

$envChild = Join-Path $env:TEMP ("aero_shutdown_test_gfx_{0}.json" -f $PID)
'{}' | Set-Content -Path $envChild -Encoding utf8

$ps = [System.Diagnostics.ProcessStartInfo]::new()
$ps.FileName = $Exe
$ps.WorkingDirectory = $RepoRoot
$ps.UseShellExecute = $false
$ps.RedirectStandardError = $true
$ps.RedirectStandardOutput = $true
$ps.EnvironmentVariables['AERO_GRAPHICS_CONFIG'] = $envChild
$ps.EnvironmentVariables['AERO_MODERN_MAX_VIS'] = '100000'
$ps.EnvironmentVariables['AERO_HEADLESS'] = '1'
$ps.EnvironmentVariables['AERO_QUIT_AFTER_MS'] = '4000'
$ps.EnvironmentVariables['AERO_SHUTDOWN_JOIN_MS'] = '5000'

$p = [System.Diagnostics.Process]::Start($ps)
$exitedCleanly = $p.WaitForExit(120000)
if (-not $exitedCleanly) { $p.Kill(); $p.WaitForExit() }
$log = ($p.StandardError.ReadToEnd()) -as [string]
if ($log -eq $null) { $log = '' }
$exitCode = $p.ExitCode
$p.Dispose()
Remove-Item $envChild -ErrorAction SilentlyContinue

$failures = 0

if (-not $exitedCleanly) {
    Write-Host "FAIL: the game did not exit within 120 s (the coordinated quit should stop it)"
    $failures++
}

if (-not $log.Contains('[probe] FIRST VI retrace')) {
    Write-Host "FAIL: the run never reached the game (the VI retrace marker is missing)"
    $failures++
}

if (-not $log.Contains('[shutdown] guest threads joined')) {
    Write-Host "FAIL: the coordinated shutdown did not join the guest threads"
    $failures++
}

if (-not $log.Contains('[probe] boot summary')) {
    Write-Host "FAIL: the coordinated shutdown did not reach the boot summary"
    $failures++
}

if ($log.Contains('NATIVE CRASH')) {
    Write-Host "FAIL: the coordinated shutdown produced a native crash report"
    $failures++
}

if ($exitCode -ne 0) {
    Write-Host "FAIL: exit status $exitCode (expected 0)"
    $failures++
}

if ($failures -eq 0) {
    Write-Host "PASS: the coordinated shutdown joined every guest thread and exited cleanly"
    exit 0
}

Write-Host "--- captured stderr ---"
Write-Host $log
exit 1
