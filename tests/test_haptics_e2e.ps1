param([string]$Exe = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\aerogauge_modern.exe'))
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$rom = Join-Path $repo 'AeroGauge (USA).z64'
$gdb = Get-Command gdb.exe -ErrorAction SilentlyContinue
if (!(Test-Path $Exe) -or !(Test-Path $rom) -or !$gdb) { exit 77 }
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('aero-haptics-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $scratch | Out-Null
$settings = @{
    AERO_HEADLESS='1'; AERO_WARP='1:1'; AERO_EASY_TURBO='1';
    AERO_MODERN_INPUT='8000:0:0'; AERO_MODERN_INPUT_AFTER='1700:a000:80:0';
    AERO_MODERN_MAX_VIS='2400'; AERO_RUMBLE='1'; AERO_RUMBLE_TURBO='1';
    AERO_PAK_PATH=(Join-Path $scratch 'controller.mpk'); LOCALAPPDATA=$scratch
}
$previous = @{}
try {
    foreach ($key in $settings.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $settings[$key], 'Process')
    }
    $watch = (Join-Path $PSScriptRoot 'haptics_watch.gdb').Replace('\', '/')
    $rom = $rom.Replace('\', '/')
    $commands = Join-Path $scratch 'run.gdb'
    "set args `"$rom`"`nsource $watch" | Set-Content -LiteralPath $commands
    $stdout = Join-Path $scratch 'stdout.log'
    $stderr = Join-Path $scratch 'stderr.log'
    $process = Start-Process -FilePath $gdb.Source -ArgumentList @(
        '--batch', '--nx', '-x', "`"$commands`"", "`"$Exe`"") `
        -WorkingDirectory $scratch -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    # Retain the process handle so Windows PowerShell exposes ExitCode after a
    # timed WaitForExit (otherwise an already-exited process can report null).
    $null = $process.Handle
    if (!$process.WaitForExit(150000)) {
        # Terminate this test's debugger and inferior together, never other game instances.
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw 'Haptics race test timed out'
    }
    $process.WaitForExit()
    $text = (Get-Content $stdout -Raw) + (Get-Content $stderr -Raw)
    if ($process.ExitCode -ne 0 -or !$text.Contains('[haptics-test] PASS both race events reached haptic hook')) {
        throw "Race haptics test failed (debugger exit $($process.ExitCode)): $text"
    }
    Write-Host 'PASS: live P1 collision and turbo reached the race haptic hook'
} finally {
    foreach ($key in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process')
    }
    # Only the unique temporary directory created above contains test save data.
    $resolved = [IO.Path]::GetFullPath($scratch)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (!$resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^aero-haptics-[0-9a-f-]{36}$') {
        throw "Unexpected test cleanup path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
