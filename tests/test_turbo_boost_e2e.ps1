# External debugger acceptance test for the accelerator-only launch assist and
# player-directed race Turbo. The shipping executable is unmodified by the
# harness: gdb observes the ROM-owned flag/timer at the native hook boundary.
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $repo "build\aerogauge_modern.exe"
$rom = Join-Path $repo "AeroGauge (USA).z64"
$gdbCommand = Get-Command gdb.exe -ErrorAction SilentlyContinue
if (-not (Test-Path $exe) -or -not (Test-Path $rom) -or $null -eq $gdbCommand) {
    Write-Host "SKIP: needs build, ROM, and gdb.exe"
    exit 77
}

$watch = Join-Path $PSScriptRoot "turbo_boost_watch.gdb"

function Invoke-TurboScenario([string]$enabled, [string]$name) {
    $log = Join-Path ([System.IO.Path]::GetTempPath()) "aero_test_turbo_boost_$name.log"
    $stdout = "$log.stdout"
    $env:AERO_EASY_TURBO = $enabled
    $arguments = @("--batch", "--nx", "-x", $watch, $exe)
    $p = Start-Process -FilePath $gdbCommand.Source -ArgumentList $arguments `
        -WorkingDirectory $repo -RedirectStandardOutput $stdout -RedirectStandardError $log `
        -PassThru -WindowStyle Hidden -Wait
    $text = (Get-Content $stdout -Raw -ErrorAction SilentlyContinue) +
            (Get-Content $log -Raw -ErrorAction SilentlyContinue)
    Remove-Item $stdout, $log -Force -ErrorAction SilentlyContinue
    if ($p.ExitCode -ne 0) {
        Write-Host "FAIL: $name debugger exit code $($p.ExitCode)"
        exit 1
    }
    return $text
}

try {
    $env:AERO_HEADLESS = "1"
    $env:AERO_WARP = "1:1"
    $env:AERO_MODERN_INPUT = "8000:0:0" # physical A, straight launch
    $env:AERO_MODERN_INPUT_AFTER = "1700:a000:80:0" # A+Z and hard right in race
    $env:AERO_MODERN_MAX_VIS = "2600"
    $positive = Invoke-TurboScenario "1" "enabled"
    $negative = Invoke-TurboScenario "0" "disabled"
} finally {
    Remove-Item Env:AERO_HEADLESS, Env:AERO_WARP, Env:AERO_MODERN_INPUT,
        Env:AERO_MODERN_INPUT_AFTER, Env:AERO_EASY_TURBO, Env:AERO_MODERN_MAX_VIS `
        -ErrorAction SilentlyContinue
}

if (-not $positive.Contains("[turbo-harness] PASS start_boost=1 race_turbo=1")) {
    Write-Host "FAIL: enabled real-ROM run did not observe both boosts"
    exit 1
}
if ($negative.Contains("[turbo-harness] start boost awarded") -or
    $negative.Contains("[turbo-harness] race turbo awarded") -or
    $negative.Contains("[turbo-harness] PASS")) {
    Write-Host "FAIL: disabled real-ROM run observed a boost"
    exit 1
}
Write-Host "PASS: enabled assist awards both boosts; disabled run awards neither"
exit 0
