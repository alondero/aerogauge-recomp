# Testing

## Automatic pull-request checks

Every push and pull request runs workflow linting, documentation and whitespace
checks, the shared patch inventory and application tests, generated-file guard
and consistency tests, Android build-script checks, and host-test registration
checks. These checks also run for changes confined to tests or build scripts.

The Linux and Windows host jobs build seven tests without ROMs,
submodules, SDL, or a renderer: full_track_policy, gyro_steering, hud_shift_scale,
input_cadence, startup_mode, rom_startup, and user_data_dir. Their target definitions are shared with the
full build in cmake/HostTests.cmake. To run this tier locally:

~~~text
cmake -S . -B build/host-checks -DAERO_HOST_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-checks --config Release --parallel 2
python -B scripts/check_host_tests.py build/host-checks
~~~

The runner compares CTest's actual registration with tests/host_test_names.txt
before executing tests. Removing or unexpectedly adding a test fails the check;
update the reviewed roster when intentionally changing this tier.

Run the tooling regressions with:

~~~text
python -B scripts/dependency_patches.py --check
python -B tests/test_dependency_patches.py
python -B tests/test_generated_files.py
python -B tests/test_host_checks.py
python -B tests/test_android_build.py
~~~

The patch tests cover repeated application, already-applied prefixes, and local
edit preservation. The generated-file tests exercise both regional outputs and
both path separator styles. They also verify missing required inputs fail and
that generation mismatches restore the developer's original output bytes.

Trusted desktop release jobs run this check after obtaining the ROMs and before
any build step can regenerate the tracked outputs:

~~~text
python -B scripts/check_docs.py --require-generated us jp
~~~

This regenerates and compares both regions' tracked TOML outputs, preserving the
working files. Required mode fails when either ROM or tracked output is missing.
Ordinary documentation checks skip unavailable regions explicitly, so public
pull requests need no private ROM access. Full CTest, ROM-backed, renderer, and
device checks below remain separate from this small automatic host tier.

## Android checks

Run `python -B tests/test_android_build.py` for a positive Android version code
for the workflow's dry-run version. Shared patch-series checks are listed above. The
version-code check mirrors Gradle's formula without requiring the Android SDK.
Run `python -B tests/test_shader_endian_swap.py` for the Adreno byte-swap
regression. It checks that the expression patch 0023 removes and the expression
it adds are bit-identical over the full 16-bit domain, that the removed one
compiles to `OpBitwiseOr` while the added one compiles to `OpBitwiseXor` at both
optimization levels the build uses, and that the checked-out `FbCommon.hlsli` no
longer contains the rejected spelling. The compile checks need the dxc
submodule. An initialized `lib/rt64` submodule that still uses the rejected
spelling fails this test rather than skipping it, because the supported build
scripts apply patch 0023 before compiling.
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

## Multiplayer checks

`gyro_steering` is a dependency-free synthetic sensor test for neutral and gyro
bias calibration, sensitivity, dead zone, return to center, recentering, invalid
and repeated timestamps, long sampling gaps, invalid readings, Android rotation
math, player isolation, and manual keyboard/gamepad/touch priority. Its sensor
axes and units follow SDL's sensor contract. `frontend_settings` and its Japanese
variant check unavailable-sensor controls, main-thread preference updates,
per-player persistence/reload, the recenter action, Easy Turbo independence,
and the port-owned Audio page: the master volume and mute are applied
immediately, the volume slider is disabled while muted, and both survive a
reload through enhancements.json.
`gyro_sdl_bridge` sends synthetic SDL sensor events through the real host bridge
with fake device discovery/assignment. It checks zero hardware timestamps,
independent simultaneous turns, disabling one player, background/resume,
reassignment, duplicate sources, stale delivery, and disconnect. It needs SDL
and frontend headers but no SDL library or driver. Its simulated controllers
start with reporting disabled and emit events only after both sensors have
been enabled. Capability discovery, successful/failed activation, reuse of
already enabled sensors, and settings-only schema synchronization are checked
separately. Rotation expectations use fixed physical gravity vectors matching
SDL's Android accelerometer remap, plus neutral and left/right steering checks.
These checks require no physical motion sensor and cannot establish driver
behavior or steering feel.

Physical gyro acceptance (record device, driver, connection mode, and outcomes):

- On an Android phone and a landscape-native tablet, calibrate while raised,
  verify neutral and both steering directions, vary sensitivity/dead zone,
  return to center, and recenter at a different resting angle.
- Rotate between landscape orientations, background/resume, open/close the
  Android action sheet, and interrupt sensor delivery. Steering must go neutral
  until fresh calibration; discarded background samples must not create a turn.
- Repeat with an assigned SDL motion gamepad. Disconnect/reconnect and reassign
  it between players. Calibrate each source independently in split screen; tilting
  one controller must not steer the other player. Android device motion must
  never steer Player 2.
- Verify keyboard, ordinary gamepad and touch overrides, settings capture, and
  Easy Turbo with gyro enabled and disabled. Check a host without sensors keeps
  ordinary bindings and disables motion activation.

These physical checks remain required before claiming device validation.

frontend_settings and frontend_settings_jp include SDL virtual gamepad checks
for assignment order, independent bindings on identical pads, stick isolation,
saved profile selections, keyboard/gamepad and shared-keyboard assignment,
reset behavior, and vacant disconnected slots. Reconnect checks exercise new
SDL instance IDs, reversed enumeration of identical models with distinct serials,
missing and duplicate serials, missed removal events, unchanged held P2 input
and selected profiles, and manual assignment cancellation and replacement.
The test executable wraps only SDL_GameControllerGetSerial to supply deterministic
serials for SDL virtual pads, whose API has no serial field. This tests the
production registration and restoration code without proving any host driver's
physical identity stability. They construct the real Controls
player cards before assignment and verify the second shared-keyboard profile
starts empty. On Windows, the test closes and detaches its virtual pads but
retains SDL's global driver state until process exit: the pinned SDL 2.26.3 can
deliver a Windows Gaming Input callback after freeing its joystick lock during
shutdown. The tests do not validate physical driver teardown.
multiplayer_assists uses synthetic RDRAM to check simultaneous presses,
independent assist settings, launch controls, heat rejection, and per-player
rumble in both regions, including zero/default/maximum strengths, multiplied
player/event gains, simultaneous collisions and Turbo, event mutes, global
gates, and clearing pending feedback. frontend_settings and frontend_settings_jp
also verify queued per-player rumble edits, persistence/reload, menu refresh,
and missing, invalid, and out-of-range configuration fields.
multiplayer_rom_input_us and multiplayer_rom_input_jp
run the ROM's own pad reader to verify neutral ports preserve player order.

Physical acceptance: assign two gamepads, edit each profile, select 2 Players,
and race in split screen. Check each player's steering, Turbo and launch assist
with the other assist disabled, collision rumble, pause/settings focus, and
disconnect/reconnect P1 while P2 holds controls and produces rumble. P1 must stay
neutral until its own pad returns; P2 must retain its port and rumble sink.
Repeat with two identical pads, reverse reconnect order, and use a pad or driver
without a serial to check the manual Assign players fallback. Check manual
reassignment clears the old reservation and rumble. Repeat with keyboard/gamepad
in both player orders and shared keyboards, restart to check saved bindings
and required fresh assignment, and check Android P1 touch with a P2 pad.

For Settings > Rumble, set Player 1's overall strength to 0 while Player 2
boosts and collides, then swap their settings. Try overall and event strengths
at 0, 50, and 100, toggle each event independently, and collide during Turbo.
Unplug or reassign a vibrating pad; neither the old pad nor the new assignment
should retain old feedback. Repeat with a gamepad without a motor and with
keyboard/touch players to confirm their input still works. Restart to verify
saved strengths, then launch with AERO_RUMBLE=0 or AERO_RUMBLE_TURBO=0 and
check the affected controls are disabled without replacing saved preferences.
Physical motor strength and platform drivers require these manual checks;
synthetic RDRAM tests only verify the requested amplitudes and feedback state.
Virtual pads and synthetic memory do not establish physical rumble or visuals.

For Settings > Audio, move the master volume while music, effects and the
announcer play, then mute and unmute. Check that the game keeps synthesising
while muted, that the music/effects balance is unchanged, and that unmute
restores the level that was set before. Restart to verify the saved level and
mute state, then launch with AERO_MASTER_VOLUME=0 and check the slider is
disabled while the game is silent. Audible output and the absence of pops or
underruns require these manual checks; the host tests only verify the samples
the port hands to the device.

## CTest

When Japanese generated code is present, `car_lod_jp`, `scene_scissor_jp`, and
`controller_pak_rom_filesystem_jp` run the corresponding Japanese routines with
the same behavioral assertions as USA. `race_intro_jp` checks the Japanese
ticker's register contract. No game bytes are embedded in these tests.
`mod_guards` and `mod_guards_jp` check that each generated regional guard list
protects injected hooks while leaving ordinary functions available to mods,
and that patch metadata remains valid after overlay registration.
`frontend_settings_jp` runs the settings adapter with the Japanese region and
checks that initial Mods settings use its code-package compatibility ID.

`audio_gain` covers the host master gain with a stubbed device that captures
the queued samples. It checks that unity is an exact pass-through, that zero is
exact silence, that an intermediate gain rounds half away from zero and
saturates a full-scale sample, that the guest's source buffer is never
modified, that a guest buffer the game reuses is not attenuated twice, that a
concurrent gain change never mixes two snapshots inside one buffer, and that
the virtual AI drain keeps its backlog while muted.

After building with both ROMs, run the shared-executable regression:

~~~text
python -B tests/test_rom_regions.py --exe build/aerogauge_modern.exe --usa "AeroGauge (USA).z64" --japan "AeroGauge (Japan) (Rev A).z64" --all-courses
~~~

On Linux omit `.exe`. This uses temporary portable profiles and tests both
swapped byte orders, all six course launches with full LOD/Easy Turbo enabled,
non-silent PCM, separate ROM caches, and cross-region save-state rejection.
Omit `--all-courses` for a shorter first-course check. These are headless checks;
they do not establish GPU visuals, physical-controller behavior, or audible
announcer quality.

The Windows release job explicitly builds both regions' car LOD, scissor,
Controller Pak, and race-intro tests, then runs them alongside the audio
submission test. Both Linux release matrix entries (x64 and ARM64) build
all targets and run every registered Linux CTest except `vi_special_features`,
which needs a desktop GPU. All desktop jobs run the full shared-executable
regression above with SDL dummy video/audio drivers before removing the
private ROMs.
Missing Japanese test targets fail the build; this coverage runs on release
workflow dispatch, not on the ROM-free pull-request documentation workflow.

The Android host import test accepts both normalized ROM paths:

~~~text
javac -d build/android-tests android/app/src/main/java/io/github/alondero/aerogaugerecomp/RomImport.java tests/android/RomImportTest.java
java -cp build/android-tests io.github.alondero.aerogaugerecomp.RomImportTest "AeroGauge (USA).z64" "AeroGauge (Japan) (Rev A).z64"
~~~

It also checks that a USA-only APK rejects Japanese imports before replacing
the installed file.

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
| audio_gain | Host | SDL2 test libraries; no ROM. Checks the host master gain: unity, zero, intermediate rounding, saturation, an untouched guest buffer, a reused guest buffer that is not attenuated twice, concurrent gain changes, and the virtual drain while muted |
| controller_accessories | Host | No ROM |
| input_stick_scaling | Host | Runtime input source from the initialized submodule; no ROM |
| input_cadence | Host | 250 Hz SDL-bridge deadline and stall recovery; no ROM or SDL |
| gyro_steering | Host | Synthetic calibrated motion steering and manual input; no ROM or SDL |
| gyro_sdl_bridge | Host | Real SDL bridge with synthetic events/devices; SDL and frontend headers, no ROM or driver |
| eeprom_exit_flush | Runtime host | Built N64ModernRuntime |
| rsp_task_submission | Runtime host | Patched N64ModernRuntime; no ROM or audio device |
| shutdown_contract | Runtime host | Patched N64ModernRuntime; no ROM, renderer, or SDL |
| racer_shadow_depth | Host | Synthetic RDRAM and runtime OSTask type; no ROM or renderer |
| hud_shift_scale | Host | No ROM |
| hud_messages | Host | RT64 headers from the initialized submodule |
| race_intro | Host | RT64 headers from the initialized submodule |
| full_track_policy | Host | No ROM |
| startup_mode | Host | No ROM; asserts the desktop startup default and the AERO_LAUNCHER=1 opt-in |
| rom_startup | Host | No ROM; explicit-path precedence, local/cache ordering, noninteractive failure, picker retry and cancellation |
| rom_picker_init_failure and rom_picker_open_failure | Host | Real startup adapter with SDL/NFD fixtures that fail picker calls; checks stderr, non-cancel failure status, and dialog/cleanup counts; dependency headers, no ROM or desktop |
| full_track_registration and full_track_registration_jp | Host | Synthetic sparse course tables and 150-object registration in both regional address layouts; runtime headers, no ROM |
| turbo_boost_gate | Host | No ROM |
| frontend_settings | Host | RecompFrontend and SDL2 test libraries; no ROM |
| frontend_navigation_wrap | Host | Real RecompFrontend/RmlUi directional events and nested single-item wrapping; no ROM or graphics device |
| frontend_navigation_candidates | Host | Real RecompFrontend/RmlUi wrapping with multiple candidates including the current focus; no ROM or graphics device |
| frontend_navigation_entry | Host | Real RecompFrontend/RmlUi navigation from a focused container into live children; no ROM or graphics device |
| frontend_navigation_rebuild | Host | Real RecompFrontend/RmlUi events after replacing a focused container's descendants; no ROM or graphics device |
| frontend_navigation_grid | Host | Real RecompFrontend/RmlUi grid navigation to and from a focusable empty row; no ROM or graphics device |
| frontend_navigation_removal | Host | Real RecompFrontend/RmlUi navigation after removing direct and flattened descendants; no ROM or graphics device |
| live_config_updates | Host | Runtime headers; no ROM |
| graphics_config_threadsafe | Host | Runtime source from the initialized submodule; no ROM |
| user_data_dir | Host | No ROM |
| controller_pak_rom_filesystem | ROM-backed | Generated functions and the accepted ROM |
| scene_scissor | ROM-backed | Generated scissor builder from the accepted ROM |
| car_lod | ROM-backed | Generated car visibility, model and animation functions from the accepted ROM |
| vi_special_features | End to end | Python 3, ROM, executable, and a working RT64 graphics device |
| audio_intro_playback | End to end | Windows desktop, ROM, executable, and usable audio device |
| haptics_race_e2e | End to end | Windows, ROM, executable, gdb.exe, and a run that reaches the race |
| turbo_boost_e2e | End to end | Windows, ROM, executable, and gdb.exe |
| audio_task_crash | End to end | Windows, ROM, executable; headless audio path |
| play_logging_quiet | End to end | Windows, ROM, executable, and a working RT64 graphics device |
| coordinated_shutdown | End to end | Windows, ROM, and executable; headless path |

The ROM-backed tests are only added when RecompiledFuncs exists at configure
time. Re-run CMake after generating the functions if they are missing from
CTest. The Windows tests are only added on Windows. play_logging_quiet and
coordinated_shutdown are also only added when the ROM exists during
configuration.

`frontend_navigation_wrap` sends real RmlUi directional events through nested
single-item groups and checks that wrapping retains the current focus.
`frontend_navigation_candidates` verifies that wrapping still chooses the
nearest other control when the destination contains the current focus.
`frontend_navigation_entry` checks entry into a focused container's live
children before and after caching them. `frontend_navigation_rebuild` checks
immediate cache invalidation when a hidden container's children are deleted,
retention of a focusable empty container, and entry into replacement children.
`frontend_navigation_grid` checks movement to and from a focusable empty row
without indexing a nonexistent child.
`frontend_navigation_removal` checks cache invalidation for direct children and
descendants flattened into an ancestor navigation list. The cache assertions
use the frontend's public accessor so allocator reuse cannot mask freed pointers.
All six tests also check normal row/column movement in all four directions.

`car_lod` executes the generated routines with synthetic car/camera data. It
checks original and forced distance boundaries, retained near/angular rejection,
camera changes, model rebuilding on live toggles, all ten craft IDs and the
mode-5 animated-transform override. `frontend_settings` checks the Graphics
option's Apply/Discard, persistence and environment override. For visual
acceptance, compare moving cars around the original LOD/cutoff distances in
RT64, including split screen, and measure performance with all racers visible.

`racer_shadow_depth` uses a synthetic task display list to verify the exact
shadow setup match, fail-closed behavior, task-relative bounds, repeated
course/shadow passes in one root list, and preservation of a HUD pass that
reuses the shadow render mode.

`shutdown_contract` links the dependency's real shutdown coordinator and
checks the coordinated game-thread shutdown contract: `request()` publishes the
flag and releases every registered guest thread exactly once, a second request
is idempotent, a guest thread registered after the request is released at once,
`join_threads()` reports the guest threads that are still running, and
unregistering an unknown token does not corrupt the count. It needs no ROM, no
renderer, and no SDL. The contract itself is described in
[docs/reference/runtime.md](reference/runtime.md#failure-and-shutdown).

The CTest wrappers use these skip behaviors:

- audio_intro_playback, haptics_race_e2e, and turbo_boost_e2e use return code
  77 when their host requirement is absent.
- audio_task_crash prints a skip notice and returns success when its ROM or
  executable is absent. Treat that notice as a skipped test in reports.
- play_logging_quiet has no automatic no-GPU skip. It needs both its RT64 leg
  and its headless leg to reach their expected log markers.
- coordinated_shutdown has no automatic skip either: it needs the ROM and the
  executable, but no graphics device, because it runs on the headless path.
- vi_special_features has no automatic no-GPU skip. It runs 1800 VIs with
  isolated portable storage and checks the live VI control word during startup
  and a race. Run it directly with
  `python -B tests/test_vi_features.py --exe build/aerogauge_modern.exe --repo .`
  (omit `.exe` on Linux). A software-renderer run cannot validate this path.

## Standalone host tests

For Windows CPU measurements, use the optional
[performance harness](../tests/measure_performance.ps1) with an independently
built baseline Release executable. It needs the USA ROM and a D3D12 device;
see [the audit](performance.md) for the command, scenario and measured limits.
It is a measurement tool, not a pass/fail claim about FPS or low-end hardware.

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

Desktop ROM startup acceptance should use an isolated portable profile and
record the OS and picker backend. With no conventional filename or cached
ROM, select an arbitrarily named supported ROM; reject a short file, wrong
game/revision and corrupt dump, retry, and cancel. Repeat with `.z64`, `.n64`
and `.v64` input, then remove the external source and relaunch using the cached
copy. Check both regions in a dual-region build, local-ROM precedence over a
cached other region, and USA-only rejection of Japan. An explicit invalid path
must fail without fallback; a headless run must never show a dialog. Check a
read-only or full import directory fails before gameplay. The dependency-free
`rom_startup` test checks policy only, not physical dialogs or runtime hashing.

Run the real startup/cache regression with a supported executable and ROM:

~~~text
python -B tests/test_rom_startup_e2e.py --exe build/aerogauge_modern.exe --usa "AeroGauge (USA).z64"
~~~

On Linux omit `.exe`. Add `--japan "AeroGauge (Japan) (Rev A).z64"` with a
dual-region executable to check regional cache/local precedence. The script
uses temporary portable profiles, headless execution, and dummy host audio;
it covers all three USA byte orders, rejected sizes and hashes, cached launch
after removing the source, explicit-path failure despite a valid cache, and
a cache destination that cannot be written as a file. It does not validate
the physical picker, graphics, or an Android device.

For USA-only acceptance, build without Japanese generated code and pass
`--unsupported-japan "AeroGauge (Japan) (Rev A).z64"` instead of `--japan`.
That checks an explicit Japanese import fails without replacing the USA cache,
and a local Japanese filename is ignored while cached USA startup still works.
Compile the actual `src/main.cpp` in both configurations: the policy-only host
test cannot detect an unguarded reference to a Japanese-only global.

The full desktop build also provides `test_rom_picker`. Run
`ctest --test-dir build -R "^rom_picker_.*_failure$" --output-on-failure` to
exercise initialization and open failures through the real startup adapter.
Both checks require the error on stderr as well as correct failure and cleanup
behavior; native dialogs and runtime calls are fixtures in these tests.

`cheats_us` and `cheats_jp` execute extracted damage arithmetic and complete
standings updates with production hooks. `frontend_settings` and its Japanese
variant cover immediate toggles and session-only storage. See
[Cheats](cheats.md#verification) for the contracts and interactive checks.

The normal ROM-backed smoke command is:

~~~bash
AERO_HEADLESS=1 AERO_MODERN_MAX_VIS=120 ./build/aerogauge_modern
~~~

That command uses the bounded exit: it stops the harness after a VI budget and
ends the process without unwinding the guest threads. To exercise the
coordinated shutdown instead, request the quit and give the guest threads time
to stop:

~~~bash
AERO_HEADLESS=1 AERO_MODERN_MAX_VIS=100000 AERO_QUIT_AFTER_MS=4000 ./build/aerogauge_modern
~~~

The expected evidence is `[shutdown] guest threads joined` followed by the boot
summary, with a zero exit status and no crash report. A `NATIVE CRASH` banner
is a failure: the coordinated shutdown must not leave a guest thread registered
in a guest message queue, because a later send would resume a thread whose
context has already been released. The same command in a windowed run
exercises the RT64 renderer, the SDL window, and the audio device on the way
out. The same run with a deliberately short `AERO_SHUTDOWN_JOIN_MS` must fall
back to the bounded exit instead of releasing RDRAM under a live guest thread;
the log then reports the guest threads that did not stop.

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
- test_coordinated_shutdown.ps1 requests the coordinated quit in a real run and
  checks that every guest thread was joined and that no crash was reported.

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
