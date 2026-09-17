# Debugging

A useful investigation has a fixed ROM, a fixed build, a short reproduction,
captured logs, and a statement of what would disprove the current idea. Do not
turn a plausible explanation into a project rule until a test or measurement
supports it.

## First checks

Run from the repository root:

~~~bash
AERO_HEADLESS=1 AERO_MODERN_MAX_VIS=120 ./build/aerogauge_modern 2> boot.log
~~~

On Windows, use the executable with the same environment variables in
PowerShell:

~~~powershell
$env:AERO_HEADLESS = "1"
$env:AERO_MODERN_MAX_VIS = "120"
.\build\aerogauge_modern.exe 2> boot.log
Remove-Item Env:AERO_HEADLESS, Env:AERO_MODERN_MAX_VIS
~~~

Check the log for:

- ROM validation;
- the first VI retrace;
- game-thread creation;
- the first graphics task;
- scene transitions; and
- a zero exit status at the requested VI limit.

If the first VI never arrives, the problem is in startup, runtime scheduling,
input, or the ROM boundary. If the first VI arrives but scene progress stops,
use the scene lines and input probes before changing the renderer.

## Logs

The program writes diagnostic output to stderr. These switches add focused
evidence:

| Setting | What it shows |
| --- | --- |
| AERO_HARNESS_LOG=1 | Low-rate VI and graphics-thread health lines |
| AERO_FRAME_LOG=logs/frame.log | Slow graphics calls and VI timing gaps; VI lines go to logs/frame.log.vi |
| AERO_AUDIO_STATS=1 | Audio device, buffering, and rebuffer information |
| AERO_AUDIO_RMS=1 | Non-zero PCM activity over time |
| AERO_CRASH_SYMBOLS=path | An explicit symbol-table search path |
| LAMBO_THREAD_TRACE=1 | Runtime thread-message trace from local patch 0001 |
| RT64_MATCH_DEBUG=1 or 2 | Interpolation matching diagnostics from local patch 0006 |

Create the logs directory first. These probes are intentionally quiet by
default because synchronous console output can affect timing.

The crash handler prints a guest PC, a native backtrace when available, and a
recent trace ring to stderr. Keep the complete output. A symbol table beside
the executable gives names for generated functions; AERO_CRASH_SYMBOLS can
point to another copy of aerogauge.syms.toml.

To test the crash-report path without waiting for a real fault:

~~~powershell
$env:AERO_CRASH_TEST = "debug crash test"
.\build\aerogauge_modern.exe
Remove-Item Env:AERO_CRASH_TEST
~~~

Use this only on a test run. It deliberately terminates the process.

## Reaching a useful game state

The developer warp uses the game's own race setup after the request is
accepted:

~~~bash
AERO_HEADLESS=1 AERO_WARP=1:1 AERO_MODERN_MAX_VIS=2600 ./build/aerogauge_modern
~~~

The first number is the track, from 1 to 6. The optional second number is the
craft, from 1 to 10. F1 through F6 do the same thing in a windowed run.

For a scheduled warp:

~~~bash
AERO_WARP_AT=1200:1:1 ./build/aerogauge_modern
~~~

The first field is a VI count. The warp request crosses from the SDL/VI side
to the game thread and is accepted only at a safe scene boundary.

For scripted input, use AERO_MODERN_INPUT for held N64 button bits and
AERO_INPUT_PULSE for repeated button edges. The exact formats are in
[Configuration](configuration.md). Keep a copy of the command and the
resulting stderr in the investigation.

## Native debugger

The source build includes debug information for the port and generated
functions. A basic interactive run is:

~~~powershell
gdb.exe --args build\aerogauge_modern.exe "AeroGauge (USA).z64"
(gdb) run
(gdb) bt
~~~

On Linux, replace gdb.exe with gdb and use the Linux executable. The crash
handler and GDB answer different questions: the handler maps a guest PC and
keeps a recent event ring; GDB shows the native call stack and live variables.

The tracked debugger scripts are:

- tests/haptics_watch.gdb, used by test_haptics_e2e.ps1;
- tests/turbo_boost_watch.gdb, used by test_turbo_boost_e2e.ps1; and
- tests/savestate scripts, which start the executable and inspect its logs.

Use the wrapper tests when possible. They set the environment and clean up
their temporary files. Do not depend on an untracked debugger file.

## Display-list and framebuffer captures

Use AERO_HEADLESS=1 to run the software renderer without a graphics device.
The renderer can inspect or capture the game's real display list:

~~~powershell
$env:AERO_HEADLESS = "1"
$env:AERO_DL_GEOMSET = "30"
$env:AERO_MODERN_MAX_VIS = "900"
.\build\aerogauge_modern.exe 2> geom.log
Remove-Item Env:AERO_HEADLESS, Env:AERO_DL_GEOMSET, Env:AERO_MODERN_MAX_VIS
~~~

Other capture switches are listed in
[Configuration](configuration.md). They may write BMP, text, or RDRAM dump
files to the current directory. Treat those files as temporary and never
commit ROM or guest-memory dumps.

Some older probes use a game-state sentinel that is not mapped in the current
port. A state-based trigger can therefore remain inactive. Use a send count
trigger where the probe supports one, or first add the missing state mapping
as a documented investigation. Do not silently change a threshold and call
the capture comparable.

When comparing a port capture with an emulator capture, record:

- ROM identity and hash;
- port commit and dependency gitlinks;
- renderer and graphics API;
- window size and aspect settings;
- exact VI or display-list trigger;
- input command;
- output files; and
- the difference that the experiment is meant to explain.

## Save-state debugging

F7 writes a developer snapshot and F8 loads it. Automatic runs use
AERO_STATE_SAVE and AERO_STATE_LOAD. A snapshot is the low 8 MiB guest-memory
image plus a small header. It does not contain native stacks or in-flight
background work.

Only save or load after the scene has been stable for the required settle
window. A mid-load or live-renderer load can race native readers. The
windowed-restore failure is tracked by the
[save-state issue](https://github.com/alondero/aerogauge-recomp/issues/22).
Use the headless round-trip script while investigating the state format.

## Reproducible investigation record

Put a dated record in docs/investigations with:

1. the question;
2. the exact build, ROM, and host;
3. the command and environment;
4. expected and actual output;
5. the current hypothesis;
6. a falsifying test; and
7. the next decision needed from the human maintainer.

If the result changes a lasting boundary, add or update an ADR. If it only
explains a historical capture, keep it in the investigation.
