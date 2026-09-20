# Testing

## Android checks

Run `python -B tests/test_android_build.py` for patch-series idempotence,
desktop-prefix compatibility and preservation of conflicting local edits.
The ROM-import test uses the real Java import class with your local ROM:

```sh
javac -d build-android/tests android/app/src/main/java/io/github/alondero/aerogaugerecomp/RomImport.java tests/android/RomImportTest.java
java -cp build-android/tests io.github.alondero.aerogaugerecomp.RomImportTest "AeroGauge (USA).z64"
```

It verifies all three byte orders and that corrupt, short, oversized and
interrupted imports preserve the installed file. No ROM data is embedded.
Run `android/gradlew -p android :app:lintDebug` for Android static checks after
preparing dependencies. See [Android acceptance checks](android.md#acceptance-checks)
for the required device workflows; record actual outcomes and untested cases.

The test suite has three levels:

1. host tests that use synthetic data and do not need a ROM;
2. ROM-backed tests that run generated game functions; and
3. end-to-end tests that start the real executable with a window, audio
   device, graphics device, or debugger.

A test that is skipped because its prerequisite is missing does not prove that
the missing behavior works. Record the skip reason in the issue or pull
request that owns the change.

## CTest

After a successful build, list the tests:

~~~bash
ctest --test-dir build -N
~~~

Run the available CTest suite with failure output:

~~~bash
ctest --test-dir build --output-on-failure
~~~

The current CMake file registers these tests:

| CTest name | Level | Needs |
| --- | --- | --- |
| audio_playback_buffering | Host | SDL2 test libraries; no ROM |
| controller_accessories | Host | No ROM |
| input_stick_scaling | Host | Runtime input source from the initialized submodule; no ROM |
| eeprom_exit_flush | Runtime host | Built N64ModernRuntime |
| rsp_task_submission | Runtime host | Patched N64ModernRuntime; no ROM or audio device |
| hud_shift_scale | Host | No ROM |
| hud_messages | Host | RT64 headers from the initialized submodule |
| race_intro | Host | RT64 headers from the initialized submodule |
| full_track_policy | Host | No ROM |
| turbo_boost_gate | Host | No ROM |
| frontend_settings | Host | RecompFrontend and SDL2 test libraries; no ROM |
| live_config_updates | Host | Runtime headers; no ROM |
| graphics_config_threadsafe | Host | Runtime source from the initialized submodule; no ROM |
| user_data_dir | Host | No ROM |
| controller_pak_rom_filesystem | ROM-backed | Generated functions and the accepted ROM |
| scene_scissor | ROM-backed | Generated scissor builder from the accepted ROM |
| audio_intro_playback | End to end | Windows desktop, ROM, executable, and usable audio device |
| haptics_race_e2e | End to end | Windows, ROM, executable, gdb.exe, and a run that reaches the race |
| turbo_boost_e2e | End to end | Windows, ROM, executable, and gdb.exe |
| audio_task_crash | End to end | Windows, ROM, executable; headless audio path |
| play_logging_quiet | End to end | Windows, ROM, executable, and a working RT64 graphics device |

The ROM-backed tests are only added when RecompiledFuncs exists at configure
time. Re-run CMake after generating the functions if they are missing from
CTest. The Windows tests are only added on Windows. play_logging_quiet is
also only added when the ROM exists during configuration.

The CTest wrappers use these skip behaviors:

- audio_intro_playback, haptics_race_e2e, and turbo_boost_e2e use return code
  77 when their host requirement is absent.
- audio_task_crash prints a skip notice and returns success when its ROM or
  executable is absent. Treat that notice as a skipped test in reports.
- play_logging_quiet has no automatic no-GPU skip. It needs both its RT64 leg
  and its headless leg to reach their expected log markers.

## Standalone host tests

These tests are useful but are not currently registered with CTest. Run them
from the repository root after initialising the submodules.

~~~bash
g++ -std=c++20 -w -I lib/rt64/src/contrib/hlslpp/include \
  tests/test_aspect_overscan.cpp lib/rt64/src/common/rt64_common.cpp \
  -o build/test_aspect_overscan
./build/test_aspect_overscan

g++ -std=c++17 -I src tests/test_draw_distance.cpp \
  -o build/test_draw_distance
./build/test_draw_distance

g++ -std=c++20 -I lib/rt64/src/contrib/hlslpp/include \
  tests/test_viewproj_decompose.cpp lib/rt64/src/common/rt64_math.cpp \
  -o build/test_viewproj_decompose
./build/test_viewproj_decompose

gcc -std=c11 -I lib/N64ModernRuntime/N64Recomp/include -x c \
  tests/test_warp_gating.cpp src/aero_warp.c tests/warp_loader_stub.c \
  -o build/test_warp_gating
./build/test_warp_gating
~~~

The audio oversize guard is a Windows SDL build check. Its source file has the
full library and include paths for the bundled SDL dependency:

~~~bash
g++ -I src -I lib/N64ModernRuntime/ultramodern/include \
  -I lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include \
  tests/test_audio_oversize_guard.cpp src/aero_audio.cpp \
  -L lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64 \
  -lSDL2 -o build/test_audio_oversize_guard
~~~

Run that binary from the build directory on Windows so SDL2.dll can be found.
Linux developers should use the SDL2 include and library paths installed by
their distribution.

The built executable also has a no-ROM lighting self-test:

~~~bash
AERO_LIGHTING_SELFTEST=1 ./build/aerogauge_modern
~~~

On PowerShell:

~~~powershell
$env:AERO_LIGHTING_SELFTEST = "1"
.\build\aerogauge_modern.exe
Remove-Item Env:AERO_LIGHTING_SELFTEST
~~~

## ROM-backed checks

The normal ROM-backed smoke command is:

~~~bash
AERO_HEADLESS=1 AERO_MODERN_MAX_VIS=120 ./build/aerogauge_modern
~~~

Useful longer checks are already wrapped by CTest or the scripts in tests:

- test_audio_task_crash.ps1 runs a headless race with generated aspMain audio
  and checks for the old RSP crash signature.
- test_savestate_roundtrip.ps1 saves a settled race in one run and loads it
  over a warped race in another run.
- test_savestate_hotkey.ps1 checks the real SDL F7 path on an interactive
  Windows desktop.
- test_haptics_e2e.ps1 uses gdb to confirm collision and Turbo reach the
  haptic hook.
- test_turbo_boost_e2e.ps1 checks enabled and disabled real-ROM input paths.
- test_audio_intro.ps1 runs the intro and attract sequence with a real audio
  device.
- test_play_logging_quiet.ps1 runs both RT64 and headless renderer legs.

The save-state scripts are not CTest tests. Run them on Windows from the
repository root:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_savestate_roundtrip.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_savestate_hotkey.ps1
~~~

The scripts create temporary logs and save-state files. They clean up their
own test files, but keep a failure log path in their error message.

## What a regression test should prove

Choose the smallest test that can fail for the bug:

- pure math or classification: add a host test;
- a guest-memory contract: use synthetic RDRAM and a focused hook test;
- a ROM function or display-list boundary: add a ROM-backed test and record
  the exact ROM identity;
- a renderer or audio device interaction: add a bounded end-to-end test with
  an explicit device prerequisite; and
- a user workflow: add a scripted launch or manual acceptance checklist.

The test must state its expected and actual behavior, thread assumptions,
generated inputs, and skip conditions. A screenshot alone is evidence, not a
repeatable test.
