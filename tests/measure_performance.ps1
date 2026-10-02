<#
    Measure CPU time in a fixed windowed D3D12 race scenario on Windows.
    Supply a baseline Release binary built from the comparison revision.
    Runs use isolated configuration and save directories. CPU time sums all
    cores, so process CPU milliseconds can exceed elapsed milliseconds.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BaselineExe,
    [string]$Exe = 'build/aerogauge_modern.exe',
    [string]$RepoRoot = '.',
    [ValidateRange(1,10)][int]$Repeats = 2,
    [ValidateRange(10,60)][int]$WarmupSeconds = 30,
    [ValidateRange(1,30)][int]$SampleSeconds = 5,
    [string]$OutputDirectory = 'build/performance'
)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$BaselineExe = (Resolve-Path -LiteralPath $BaselineExe).Path
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$romPath = (Resolve-Path -LiteralPath (Join-Path $taskRoot 'AeroGauge (USA).z64')).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

function Receive-AvailableLines($process, $state) {
    while ($null -ne $state.NextLine -and $state.NextLine.IsCompleted) {
        $line = $state.NextLine.GetAwaiter().GetResult()
        if ($null -eq $line) { $state.NextLine = $null; break }
        $state.Log.AppendLine($line) | Out-Null
        if ($line.StartsWith('[state]')) {
            $state.InRace = $line -match 'scene=5 req=5 phase=3 race_mode=4'
        }
        $state.NextLine = $process.StandardError.ReadLineAsync()
    }
}

$results = @()
foreach ($repeat in 1..$Repeats) {
    foreach ($scenario in @('baseline','updated','low-power')) {
        $runRoot = Join-Path $outputRoot ("perf-$scenario-$repeat-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
        # Portable mode has precedence over LOCALAPPDATA. Use our own working
        # directory and marker so an existing portable player profile is safe.
        [IO.File]::WriteAllText((Join-Path $runRoot 'portable.txt'), '')
        $config = @{ window_width=1600; window_height=900; full_track=$true; draw_distance_scale=100; developer_mode=$false }
        if ($scenario -eq 'low-power') {
            $config.res_option='Original'; $config.ds_option=1; $config.msaa_option='None'
            $config.rr_option='Original'; $config.hpfb_option='Off'
        }
        $configPath = Join-Path $runRoot 'graphics.json'
        [IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        $ps = [Diagnostics.ProcessStartInfo]::new()
        $ps.FileName = if ($scenario -eq 'baseline') { $BaselineExe } else { $Exe }
        $ps.WorkingDirectory = $runRoot
        $ps.Arguments = '"' + $romPath + '"'
        $ps.UseShellExecute = $false
        $ps.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $ps.RedirectStandardError = $true
        $ps.RedirectStandardOutput = $true
        foreach ($name in @($ps.EnvironmentVariables.Keys)) {
            if ($name.StartsWith('AERO_', [StringComparison]::OrdinalIgnoreCase)) {
                $ps.EnvironmentVariables.Remove($name)
            }
        }
        $ps.EnvironmentVariables['LOCALAPPDATA'] = $runRoot
        $ps.EnvironmentVariables['AERO_GRAPHICS_CONFIG'] = $configPath
        $ps.EnvironmentVariables['AERO_ENHANCEMENTS_CONFIG'] = (Join-Path $runRoot 'enhancements.json')
        $ps.EnvironmentVariables['AERO_PAK_PATH'] = (Join-Path $runRoot 'test.mpk')
        $ps.EnvironmentVariables['AERO_HEADLESS'] = '0'
        $ps.EnvironmentVariables['AERO_WARP_AT'] = '360:1:1'
        $ps.EnvironmentVariables['AERO_MODERN_MAX_VIS'] = [string](60 * ($WarmupSeconds + $SampleSeconds + 10))
        Write-Output "Starting $scenario repeat $repeat"
        $p = [Diagnostics.Process]::Start($ps)
        try {
            $state = @{ Log=[Text.StringBuilder]::new(); NextLine=$p.StandardError.ReadLineAsync(); InRace=$false }
            $stdout = $p.StandardOutput.ReadToEndAsync()
            # Bounded waits keep logs draining and permit cancellation while the
            # guest enters the race and the driver creates its initial pipelines.
            for ($second = 0; $second -lt $WarmupSeconds; ++$second) {
                Start-Sleep -Seconds 1
                Receive-AvailableLines $p $state
                if ($p.HasExited) { throw "Early exit: $scenario; $($state.Log)" }
            }
            if (!$state.InRace) { throw "Race is not ready before sampling: $scenario. Increase WarmupSeconds. $($state.Log)" }
            $p.Refresh()
            $main = $p.Threads | Sort-Object StartTime | Select-Object -First 1
            $threadId = $main.Id
            $mainStart = $main.TotalProcessorTime.TotalMilliseconds
            $cpuStart = $p.TotalProcessorTime.TotalMilliseconds
            $clock = [Diagnostics.Stopwatch]::StartNew()
            for ($second = 0; $second -lt $SampleSeconds; ++$second) {
                Start-Sleep -Seconds 1
                Receive-AvailableLines $p $state
                if ($p.HasExited -or !$state.InRace) { throw "Run left the live race during sampling: $scenario" }
            }
            $p.Refresh()
            $wall = $clock.Elapsed.TotalMilliseconds
            $cpu = $p.TotalProcessorTime.TotalMilliseconds - $cpuStart
            $mainEnd = ($p.Threads | Where-Object Id -eq $threadId).TotalProcessorTime.TotalMilliseconds
            $workingSet = $p.WorkingSet64 / 1MB
            $deadline = [DateTime]::UtcNow.AddSeconds(45)
            while (!$p.HasExited -and [DateTime]::UtcNow -lt $deadline) {
                Start-Sleep -Milliseconds 100
                Receive-AvailableLines $p $state
            }
            if (!$p.HasExited) { throw "Timeout: $scenario" }
            $p.WaitForExit()
            while ($null -ne $state.NextLine) {
                $state.NextLine.Wait()
                Receive-AvailableLines $p $state
            }
            $log = $state.Log.ToString()
            [IO.File]::WriteAllText((Join-Path $runRoot 'stderr.log'), $log, [Text.UTF8Encoding]::new($false))
            if ($p.ExitCode -ne 0 -or $log -notmatch '\[rt64\] first send_dl' -or
                $log -notmatch '\[warp\].*game-mode 0 race' -or $log -notmatch 'phase=3 race_mode=4') {
                throw "Run did not render and reach a live race cleanly: $scenario; $log"
            }
            $result = [pscustomobject]@{scenario=$scenario; repeat=$repeat; wall_ms=[math]::Round($wall,1); process_cpu_ms=[math]::Round($cpu,1); main_cpu_ms=[math]::Round($mainEnd-$mainStart,1); working_set_mib=[math]::Round($workingSet,1); exit=$p.ExitCode}
            $results += $result
            $result | ConvertTo-Json -Compress | Write-Output
        } finally {
            if (!$p.HasExited) { $p.Kill(); $p.WaitForExit() }
            $p.Dispose()
        }
    }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'performance-results.json') -Encoding UTF8
