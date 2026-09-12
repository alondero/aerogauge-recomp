# Real-device regression: intro through attract-mode demo, with no playback starvation.
# Needs a Windows desktop/audio device, the ROM, and a built executable.
param([Parameter(Mandatory = $true)][string]$Exe,
      [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$resolvedRepoRoot = (Resolve-Path $RepoRoot).Path
$rom = Join-Path $resolvedRepoRoot 'AeroGauge (USA).z64'
if (-not (Test-Path -LiteralPath $Exe) -or -not (Test-Path -LiteralPath $rom)) {
    Write-Host "SKIP: missing ROM ($rom) or executable ($Exe)"
    exit 77
}
$info = [System.Diagnostics.ProcessStartInfo]::new()
$info.FileName = (Resolve-Path $Exe).Path
$info.WorkingDirectory = $resolvedRepoRoot
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
$info.RedirectStandardError = $true
$info.RedirectStandardOutput = $true
foreach ($name in @('AERO_WARP', 'AERO_WARP_AT', 'AERO_STATE_LOAD', 'AERO_INPUT_PULSE',
                    'AERO_MODERN_INPUT', 'AERO_MODERN_INPUT_AFTER', 'AERO_HARNESS_LOG',
                    'AERO_FRAME_LOG', 'AERO_AUDIO_RMS', 'SDL_AUDIODRIVER')) {
    $info.EnvironmentVariables.Remove($name)
}
$info.EnvironmentVariables['AERO_HEADLESS'] = '0'
$info.EnvironmentVariables['AERO_MODERN_MAX_VIS'] = '3600'
$info.EnvironmentVariables['AERO_AUDIO_STATS'] = '1'
$process = [System.Diagnostics.Process]::Start($info)
$stderr = $process.StandardError.ReadToEndAsync()
$stdout = $process.StandardOutput.ReadToEndAsync()
if (-not $process.WaitForExit(120000)) {
    $process.Kill()
    $process.WaitForExit()
    throw 'Intro audio run timed out'
}
$log = $stderr.Result
$logPath = Join-Path $env:TEMP "aero_intro_audio_$PID.log"
[System.IO.File]::WriteAllText($logPath, $log)
if ($log -match 'SDL_OpenAudioDevice failed|SDL_InitSubSystem\(SDL_INIT_AUDIO\) failed') {
    Write-Host "SKIP: no usable audio device; see $logPath"
    exit 77
}
if ($process.ExitCode -ne 0 -or $log -match 'Unhandled jump target|exited unexpectedly') {
    throw "Intro audio run crashed; see $logPath"
}
if ($log -notmatch 'boot summary;.*vis=3600\b' -or
    $log -notmatch 'first NON-SILENT buffer' -or
    $log -notmatch 'playback started; rebuffers=0' -or
    $log -match 'rebuffers=[1-9]') {
    throw "Intro playback did not complete without starvation; see $logPath"
}
Write-Host "PASS: 3600-VI intro/demo, audible PCM, no playback rebuffers; $logPath"
