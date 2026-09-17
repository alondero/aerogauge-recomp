# Settings frontend review - 2026-09-17

## Scope

This record checks the settings frontend after it entered the current main
line. It records what is established by source and earlier targeted tests. It
does not claim a new manual acceptance run.

## Fixed inputs

- Main-line commit reviewed: `9b7269c0eabe7040a3bb8478bd9e5ee2952ec4bc`
- Supported ROM: USA 8 MiB dump, XXH3-64
  `0x89ea0690f3e22201`
- Target host: Windows x64
- Renderer path: RT64 with Direct3D 12
- Current checkout: dependency submodules were not initialized for this
  documentation review

## Evidence

The source and CMake wiring show that the same RecompFrontend settings source
is built on the Windows and Linux paths. The port owns the JSON files and
queues permanent settings actions to the SDL main thread. The frontend owns
temporary page state and confirmation behavior.

Earlier Windows evidence from the settings feature branch ran:

~~~text
ctest --test-dir build -R "^(frontend_settings|live_config_updates)$" --output-on-failure
~~~

Both tests passed in that run. This review did not rerun the command after the
worktree was recreated, and it did not launch the menu, test a physical
controller, run a Linux executable, or inspect a packaged release.

## Findings

1. Direct guest-memory writes remain in full-track geometry, widescreen HUD,
   and save-state code. They depend on ROM addresses, byte order, bounds,
   lifetime, and thread timing. They are transitional infrastructure.
2. The settings action queue and frontend render path share a lock. A
   blocking action can delay rendering or deadlock if the action waits for
   frontend work.
3. Patch 0015 makes port code the JSON storage owner. Patch 0016 combines
   generic frontend changes with AeroGauge integration. The generic portion
   may be a candidate for an upstream change, but no upstream contribution is
   accepted by this branch.
4. Texture replacement is an RT64 asset path. The port has no versioned
   general code-mod or plugin interface.

## Required falsifying checks

- Build and run the Windows menu from a clean checkout with the release asset
  layout.
- Build and run the Linux path and check keyboard, mouse, and controller
  navigation.
- Exercise Apply, Discard, fullscreen changes, restart-only settings, and
  unknown JSON keys while checking that the action queue does not block the
  renderer.
- Reduce any general RT64 patch to a small reproduction before proposing it
  upstream.
