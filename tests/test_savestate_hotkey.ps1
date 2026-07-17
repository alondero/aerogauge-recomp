# Regression test for the F7 save-state hotkey path (the "F7 did nothing" report,
# 2026-07-17). The env-var path (test_savestate_roundtrip.ps1) never exercises the
# SDL-thread hotkey wiring: F7 edge-detect in input_sample -> request bit -> settled-gate
# consumption in aero_savestate_tick. This test drives it end-to-end with a real window:
#
#   1. Launch the game WINDOWED (keyboard input needs the SDL window; headless has none).
#   2. Force keyboard focus onto the game (AttachThreadInput -- plain AppActivate/
#      SetForegroundWindow lies under Windows focus-stealing prevention; we verify with
#      GetForegroundWindow and SKIP if focus really cannot be acquired).
#   3. Inject a HELD F7 (down, 350 ms, up): the port polls SDL_GetKeyboardState once per
#      frame, so the press must span at least one frame -- an instantaneous tap can land
#      between samples.
#   4. Assert the IMMEDIATE acknowledgement line "[savestate] F7: save requested" appears
#      (this is the fix: a request held by the settled gate used to be indistinguishable
#      from a dead hotkey).
#   5. Assert the save itself fires once the boot cascade reaches a settled scene
#      ("[savestate] saved ... "), proving the request bit crosses to the game thread.
#
# Requires the ROM + built exe + an interactive desktop; skipped (exit 0) when missing.
param(
    [string]$Exe = ""
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
if ($Exe -eq "") { $Exe = Join-Path $repo "build\aerogauge_modern.exe" }
$rom  = Join-Path $repo "AeroGauge (USA).z64"
if (-not (Test-Path $Exe) -or (-not (Test-Path $rom))) {
    Write-Host "SKIP: needs $Exe and the ROM"
    exit 0
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HotkeyTest {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public static bool ForceFocus(IntPtr hwnd) {
        uint pid; uint tid = GetWindowThreadProcessId(hwnd, out pid);
        uint my = GetCurrentThreadId();
        AttachThreadInput(my, tid, true);
        ShowWindow(hwnd, 9);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        AttachThreadInput(my, tid, false);
        System.Threading.Thread.Sleep(400);
        return GetForegroundWindow() == hwnd;
    }
    public static void HoldKey(byte vk, byte scan) {
        keybd_event(vk, scan, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(350);
        keybd_event(vk, scan, 2, UIntPtr.Zero); // KEYEVENTF_KEYUP
    }
}
'@

$tmp   = [System.IO.Path]::GetTempPath()
$log   = Join-Path $tmp "aero_test_hotkey.log"
$state = Join-Path $tmp "aero_test_hotkey.astate"
Remove-Item $log, $state -ErrorAction SilentlyContinue

$env:AERO_STATE_FILE = $state
$p = Start-Process -FilePath $Exe -ArgumentList "`"$rom`"" -WorkingDirectory $repo `
     -RedirectStandardError $log -PassThru
Remove-Item Env:AERO_STATE_FILE -ErrorAction SilentlyContinue

function Cleanup { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }

# Wait for the SDL window.
$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    $hwnd = [HotkeyTest]::FindWindow("SDL_app", "AeroGauge")
    if ($hwnd -ne [IntPtr]::Zero) { break }
    if ($p.HasExited) { Write-Host "FAIL: game exited during boot; log: $log"; exit 1 }
    Start-Sleep -Milliseconds 500
}
if ($hwnd -eq [IntPtr]::Zero) {
    Cleanup; Write-Host "SKIP: no game window appeared (headless environment?)"; exit 0
}
Start-Sleep -Seconds 3   # let the render loop + input pump come up

if (-not [HotkeyTest]::ForceFocus($hwnd)) {
    Cleanup; Write-Host "SKIP: could not acquire keyboard focus for the game window"; exit 0
}
[HotkeyTest]::HoldKey(0x76, 0x41)   # VK_F7, scancode 0x41

# Assert 1: immediate acknowledgement, independent of the settled gate.
$deadline = (Get-Date).AddSeconds(5)
$acked = $false
while ((Get-Date) -lt $deadline) {
    if (Select-String -Path $log -Pattern "\[savestate\] F7: save requested" -Quiet -ErrorAction SilentlyContinue) {
        $acked = $true; break
    }
    Start-Sleep -Milliseconds 250
}
if (-not $acked) {
    Cleanup
    Write-Host "FAIL: F7 produced no immediate '[savestate] F7: save requested' acknowledgement; log: $log"
    exit 1
}

# Assert 2: the held request fires once the boot cascade settles (attract race at latest).
$deadline = (Get-Date).AddSeconds(60)
$saved = $false
while ((Get-Date) -lt $deadline) {
    if (Select-String -Path $log -Pattern "\[savestate\] saved" -Quiet -ErrorAction SilentlyContinue) {
        $saved = $true; break
    }
    if ($p.HasExited) { break }
    Start-Sleep -Milliseconds 500
}
Cleanup
if (-not $saved) {
    Write-Host "FAIL: F7 request never fired a save on a settled frame; log: $log"
    exit 1
}
if (-not (Test-Path $state)) {
    Write-Host "FAIL: save log line appeared but $state was not written"
    exit 1
}
Remove-Item $state -ErrorAction SilentlyContinue
Write-Host "PASS: F7 acknowledged immediately and saved on the next settled frame"
exit 0
