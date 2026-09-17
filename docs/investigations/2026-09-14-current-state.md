# Current-state ledger — 2026-09-14

## Method

This ledger was made from the checked-in source, CMake files, build scripts,
release metadata, tests, source comments, the repository issue tracker, and
the open settings-frontend pull request. It is a static audit. I did not
launch the port in this investigation and I did not install or play the
comparison projects.

The working tree contains the supported 8 MiB ROM as local user data, but the
ROM is not part of the repository. The dependency submodules are recorded as
gitlinks and are not initialized in this worktree.

## Branch and release

| Item | Observation |
| --- | --- |
| Checked-in commit | f010792, tagged v0.2.1 |
| Public release state | v0.2.1 is the latest listed release at the time of this audit |
| Declared player hosts | 64-bit Windows and 64-bit Linux |
| Declared unsupported host | macOS in this branch |
| Normal player renderer | RT64; headless runs select the software test renderer |
| Current settings UI | Native Windows menu; JSON files on all hosts |
| Pending UI work | A shared Windows/Linux settings screen is proposed in the open [settings frontend pull request](https://github.com/alondero/aerogauge-recomp/pull/47), not in this branch |

The release page is evidence for what the project presents as a released
workflow. It is not a substitute for running the executable on every host.

## What the tree currently provides

| Area | Current evidence | Confidence |
| --- | --- | --- |
| ROM-derived CPU output | Recompiler inputs, symbol generator, and ignored RecompiledFuncs boundary | Static |
| ROM-derived audio output | aspMain.us.toml and ignored src/aspMain.cpp boundary | Static |
| Windows player path | CMake D3D12/Win32 setup, SDL input/window code, release package | Static and release metadata |
| Linux player path | CMake Vulkan/SDL setup, shell build script, release package | Static and release metadata |
| JSON settings | aero_config.cpp and configuration tests | Static and host-testable |
| Native Windows menu | aero_menu.cpp | Static |
| Headless capture path | AERO_HEADLESS and stub_renderer.cpp | Static and test-wired |
| Controller Pak and EEPROM | aero_pak.cpp plus runtime save wiring | Static and host/ROM tests |
| Audio queue path | aero_audio.cpp plus registered audio tests | Static and test-wired |
| Widescreen HUD pass | ROM hooks, aero_hud_widescreen.c, host math tests | Static and partially test-wired |
| Full-course mode | aero_full_track.cpp and policy test | Static and experimental |
| Save-state path | aero_savestate.c, debugger scripts, and issue tracking | Static and experimental |

“Static” means the source establishes the path. “Test-wired” means a test
exists in CMake or the test directory. Neither word means that this audit
personally observed a successful player run.

## Platform-specific behavior

- Windows uses RT64's Direct3D 12 path, a Win32 window handle, a native menu,
  and several Windows-only end-to-end tests.
- Linux uses RT64's Vulkan path and has no equivalent native settings menu in
  the current branch.
- The current CMake file has no supported macOS application path. Upstream
  RT64's Metal support does not add AeroGauge's missing window, input, build,
  and release work.
- Some tests are deliberately Windows-only because they use PowerShell,
  gdb.exe, Win32 window behavior, or a real Windows audio path.

## Experimental or incomplete areas

- Full-track geometry bypasses the game's three-zone visibility window by
  writing synthetic display lists into a reserved guest-memory range.
- The HUD pass rewrites the display-list stream after the game's 2D handlers
  have emitted it and shifts one matrix for the speedometer needle.
- Save-state loading restores guest RDRAM but not native thread stacks,
  registers, or in-flight work. An open issue reports windowed restore
  failures.
- Live graphics updates write JSON synchronously today and have an open
  runtime data-race issue.
- The open settings frontend pull request has not been merged. Its reported
  API/developer/texture restart rules, missing fog/sky controls, and
  physical-controller navigation still need an explicit acceptance decision.
- The Linux and Windows dependency patch lists are not yet shown to be
  equivalent. See the [patch parity investigation](2026-09-14-build-patch-parity.md).
- The current port has no versioned code-mod or general code-mod loading
  interface. RT64 texture replacement is an asset path, not a full mod API.

## Decisions a human maintainer still owns

1. Whether the Linux patch list should include the synchronous RSP
   fail-soft change and the EEPROM flush change in the same way as Windows.
2. Which local RT64 behavior is AeroGauge-specific and which deserves an
   upstream issue, test, or contribution.
3. Whether the next game-code work should improve symbol quality, begin
   decompilation, or formalize a narrow source/code-mod seam.
4. Whether save-state loading remains a debug-only tool or gains a stronger
   synchronization contract.
5. What a versioned mod boundary owns: assets, generated code, native code,
   configuration, load order, and failure behavior.
6. Whether and when the shared settings frontend should replace the current
   Windows-only menu and JSON-first Linux workflow.

These are project choices. Documentation should expose their trade-offs,
not decide them by sounding certain.
