# Dependency patch inventory

These files are build inputs. They are applied to pinned public submodules by
the supported build scripts. They are not a private fork and this page is not
an upstream proposal log.

The exact diff is in each patch file. This table gives the local purpose,
platform, and ownership question.

| Patch | Dependency | Host | Local purpose and boundary |
| --- | --- | --- | --- |
| [0001](0001-ultramodern-runtime-scheduler-audio-vi.patch) | N64ModernRuntime | Windows, Linux | Scheduler, VI timing, audio, and local diagnostic thread tracing. Separate generic runtime behavior from game policy before changing it. |
| [0004](0004-plume-d3d12-mingw-com-abi-struct-return.patch) | RT64 Plume | Windows | MinGW and Direct3D 12 COM ABI compatibility. |
| [0005](0005-rt64-mingw-gcc-compat.patch) | RT64 | Windows | MinGW compiler compatibility. |
| [0006](0006-rt64-interp-angular-velocity-matching.patch) | RT64 | Windows, Linux | Angular-velocity-aware interpolation matching. |
| [0007](0007-ultramodern-savestate-thread-context-relink.patch) | N64ModernRuntime | Windows, Linux | Thread-context relinking for the developer save-state tool. It is not a general save-state guarantee. |
| [0008](0008-rt64-skybox-stretch-parallaxless-backdrop.patch) | RT64 | Windows, Linux | AeroGauge sky and backdrop behavior. Keep camera policy out of a general renderer API. |
| [0009](0009-rt64-widescreen-split-subviewport.patch) | RT64 | Windows, Linux | Widescreen split-screen viewport behavior used by the port. |
| [0010](0010-rt64-viewproj-decompose-axis-aligned-pivot.patch) | RT64 | Windows, Linux | View and projection decomposition behavior used by HUD work. |
| [0011](0011-rt64-aspect-adjust-overscan-inset-viewport.patch) | RT64 | Windows, Linux | Aspect and overscan viewport handling. |
| [0012](0012-librecomp-pi-dma-completion-osiomesg.patch) | N64ModernRuntime | Windows, Linux | PI DMA completion message behavior. |
| [0013](0013-ultramodern-sp-task-synchronous-failsoft.patch) | N64ModernRuntime | Windows, Linux | Execute non-graphics RSP tasks before the game reuses their descriptor and signal completion even after a failed task. Required on Linux too: asynchronous audio failure can terminate the game during the Time Trial Pak check. |
| [0014](0014-librecomp-flush-eeprom-on-exit.patch) | N64ModernRuntime | Windows, Linux | EEPROM flush before the current process-exit path. |
| [0015](0015-runtime-host-config-storage.patch) | N64ModernRuntime | Windows, Linux | Host-owned configuration storage used by the settings integration. |
| [0016](0016-recompfrontend-integration.patch) | RecompFrontend | Windows, Linux | SDL, assets, settings, and frontend integration. |
| [0017](0017-runtime-game-presentation.patch) | N64ModernRuntime | Windows, Linux | Port-owned game presentation after the runtime update. |
| [0018](0018-ultramodern-graphics-config-snapshot.patch) | N64ModernRuntime | Windows, Linux | Return the graphics configuration by value. A live `set_graphics_config()` from the menu thread otherwise raced the game, VI, and graphics threads that read the accessor's reference after its mutex was released. |

## Application order

Android additionally applies [0019](0019-rt64-android-cross-build.patch) to
RT64, [0020](0020-plume-android-sdl-window.patch) to Plume,
[0021](0021-sdl-android-usb-receiver.patch) to the pinned Android SDL source,
and [0022](0022-recompfrontend-android.patch) to RecompFrontend. These adapt
host shader generation, SDL Vulkan surfaces and loader dispatch, RGBA presentation,
Android USB receiver registration, and frontend file-dialog/platform boundaries.
They follow the same pinned sources as desktop and are based on the
[Lamborghini Android implementation](https://github.com/alondero/automobililamborghini-recomp/tree/main/patches).
RT64 and frontend presentation formats must change together. These are temporary
platform compatibility patches; compare them with the pinned upstream before removal.

The host scripts are the executable build contract:

| Host | Applied patches |
| --- | --- |
| Linux | 0001, 0007, 0012, 0013, 0014, 0015, 0017, 0018, 0016, 0006, 0008, 0009, 0010, 0011 |
| Android | Runtime: 0001, 0007, 0012, 0013, 0014, 0015, 0017, 0018; frontend: 0016, 0022; RT64: 0006, 0008, 0009, 0010, 0011, 0019; Plume: 0020; SDL: 0021 |
| Windows | 0001, 0007, 0012, 0013, 0014, 0015, 0017, 0018, 0016, 0006, 0008, 0009, 0010, 0011, 0005, 0004 |

Keep this inventory, [BUILDING.md](../BUILDING.md), and both host scripts in
agreement. The documentation checker validates patch hunk counts, but only a
clean submodule at its pinned commit proves that a patch still applies.

## Maintaining a patch

1. Start from the pinned dependency commit.
2. Apply the existing patches in the order above.
3. Make the smallest dependency change and add a focused test when possible.
4. Export the change as a numbered patch. Do not edit a patch by hand without
   validating its hunk counts and application.
5. Decide whether the change is AeroGauge-specific, a temporary compatibility
   fix, or a candidate for the dependency's upstream project.
6. Update the build scripts, this inventory, and the relevant test or reference
   page in the same pull request.

If a patch no longer applies, stop and review the dependency change. Do not
reset unrelated submodule work or silently refresh the patch.

## Renderer boundary

RT64 owns general display-list interpretation, presentation, aspect handling,
interpolation, extended GBI commands, and texture replacement. The port owns
ROM-specific HUD classification, course geometry policy, game presentation,
and the SDL/runtime wiring.

A general renderer fix needs a small reproducer and an upstream proposal. A
game-specific display-list rule belongs in the port. A compiler or platform
compatibility patch should stay small and should be removed when the pinned
upstream supports the same toolchain.
