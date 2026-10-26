# Dependency patch inventory

These files are build inputs. They are applied to pinned public submodules by
the supported build scripts. They are not a private fork and this page is not
an upstream proposal log.

Mod support uses the package manager already present in the pinned
N64ModernRuntime and RecompFrontend submodules. `src/aero_mods.cpp` registers
the AeroGauge ID and RT64 texture content, while CMake generates guards for
port-modified functions. Local frontend patch [0026](0026-recompfrontend-mod-failure-recovery.patch)
restores the manager controls if loading fails and the runtime returns to the
stopped state. Patch [0027](0027-recompfrontend-stale-focus-recovery.patch)
recovers navigation when a focused element disappears from the rebuilt visible
navigation tree.
Patch [0028](0028-recompfrontend-navigation-target-lifetime.patch) invalidates
navigation caches when children are destroyed or removed, including ancestor
lists that flatten descendants. Focused containers rebuild their live children,
and focusable empty containers remain navigation destinations. Wrapping retains
the current focus only when there is no other candidate. These prevent invalid
destination traversal without blocking movement into live settings controls.

The exact diff is in each patch file. This table gives the local purpose
and ownership question. [series.json](series.json) is the authoritative order
and platform selection for all three build paths. Run
`python -B scripts/dependency_patches.py --check` after changing the manifest.
The shared applicator preflights every dependency and preserves staged and
unstaged local edits; conflicts stop setup instead of resetting submodules.
Use a separate clean worktree when target platforms have conflicting patch
contexts.

| Patch | Dependency | Local purpose and boundary |
| --- | --- | --- |
| [0001](0001-ultramodern-runtime-scheduler-audio-vi.patch) | N64ModernRuntime | Scheduler, VI timing, audio, and local diagnostic thread tracing. Separate generic runtime behavior from game policy before changing it. |
| [0004](0004-plume-d3d12-mingw-com-abi-struct-return.patch) | RT64 Plume | MinGW and Direct3D 12 COM ABI compatibility. |
| [0005](0005-rt64-mingw-gcc-compat.patch) | RT64 | MinGW compiler compatibility. |
| [0006](0006-rt64-interp-angular-velocity-matching.patch) | RT64 | Angular-velocity-aware interpolation matching. |
| [0007](0007-ultramodern-savestate-thread-context-relink.patch) | N64ModernRuntime | Thread-context relinking for the developer save-state tool. It is not a general save-state guarantee. |
| [0008](0008-rt64-skybox-stretch-parallaxless-backdrop.patch) | RT64 | AeroGauge sky and backdrop behavior. Keep camera policy out of a general renderer API. |
| [0009](0009-rt64-widescreen-split-subviewport.patch) | RT64 | Widescreen split-screen viewport behavior used by the port. |
| [0010](0010-rt64-viewproj-decompose-axis-aligned-pivot.patch) | RT64 | View and projection decomposition behavior used by HUD work. |
| [0011](0011-rt64-aspect-adjust-overscan-inset-viewport.patch) | RT64 | Aspect and overscan viewport handling. |
| [0012](0012-librecomp-pi-dma-completion-osiomesg.patch) | N64ModernRuntime | PI DMA completion message behavior. |
| [0013](0013-ultramodern-sp-task-synchronous-failsoft.patch) | N64ModernRuntime | Execute non-graphics RSP tasks before the game reuses their descriptor and signal completion even after a failed task. Required on Linux too: asynchronous audio failure can terminate the game during the Time Trial Pak check. |
| [0014](0014-librecomp-flush-eeprom-on-exit.patch) | N64ModernRuntime | EEPROM flush before the current process-exit path. |
| [0015](0015-runtime-host-config-storage.patch) | N64ModernRuntime | Host-owned configuration storage used by the settings integration. |
| [0016](0016-recompfrontend-integration.patch) | RecompFrontend | SDL, assets, settings, and frontend integration. |
| [0017](0017-runtime-game-presentation.patch) | N64ModernRuntime | Port-owned game presentation after the runtime update. |
| [0018](0018-ultramodern-graphics-config-snapshot.patch) | N64ModernRuntime | Return the graphics configuration by value. A live `set_graphics_config()` from the menu thread otherwise raced the game, VI, and graphics threads that read the accessor's reference after its mutex was released. |
| [0024](0024-recompfrontend-input-synchronization.patch) | RecompFrontend | Synchronize input-binding state and provide a locked binding snapshot for the port's SDL menu-toggle cache. |
| [0025](0025-recompfrontend-controller-cleanup.patch) | RecompFrontend | Collect detached and shutdown controller states on the SDL main thread so missed removal events or window teardown cannot leave frontend handles registered. |
| [0026](0026-recompfrontend-mod-failure-recovery.patch) | RecompFrontend | Local recovery fix restores Install, Refresh, and mod-toggle controls after a failed load. This general frontend behavior is a candidate for upstream RecompFrontend. |
| [0027](0027-recompfrontend-stale-focus-recovery.patch) | RecompFrontend | Recover directional navigation when the focused element is no longer present in its parent's rebuilt visible navigation list. |
| [0028](0028-recompfrontend-navigation-target-lifetime.patch) | RecompFrontend | Invalidate navigation caches on child removal, rebuild focused containers, retain focusable empty containers, and preserve the nearest-candidate search with a single-item wrap fallback. General frontend correctness fix; candidate for upstream RecompFrontend. |
| [0029](0029-recompfrontend-multiplayer-profiles.patch) | RecompFrontend | Legacy fallback before assignment, isolated and persisted player profiles, bounded assignment, and clearing detached references. Candidate for upstream RecompFrontend. |
| [0030](0030-recompfrontend-controller-reconnect.patch) | RecompFrontend | Process-local slot restoration using SDL serial plus model identity, duplicate-identity rejection, and disconnected-player guidance. Candidate for upstream RecompFrontend. |
| [0031](0031-ultramodern-coordinated-game-thread-shutdown.patch) | N64ModernRuntime | Stop/request/join contract for the guest threads so the owner can release RDRAM and the device resources in a defined order. The runtime owns the contract; the port owns the deadline and the fallback. |
| [0023](0023-rt64-adreno-endian-swap.patch) | RT64 | Spell the 16-bit byte swap with XOR instead of OR. The Adreno system Vulkan driver returns `VK_ERROR_UNKNOWN` from `vkCreateComputePipelines` for any compute shader containing the OR form, which previously left a null pipeline handle that crashed `vkCmdBindPipeline`. The masked fields are disjoint, so the result is identical on every driver. |

Patch [0029](0029-recompfrontend-multiplayer-profiles.patch) enables the port's
two-player integration: gameplay keeps the legacy P1 mapping before assignment,
assigned devices poll only their own profiles, menu actions and hints follow the
controller sending them, controller profiles are distinct
per slot even for identical pads, and selections survive controls.json reloads.
Assignments stop at the configured player limit; detached controller references
are cleared without compacting slots. Compared with the pinned RecompFrontend
b1a1477, this adds behavior missing from its existing assignment/profile APIs.
These are reusable frontend changes; the two-player limit, responsive virtual
ports, SDL/presentation synchronization, and assist policy stay in AeroGauge.

Patch [0030](0030-recompfrontend-controller-reconnect.patch) caches physical
identity on SDL registration and reserves committed player slots. It restores
only a unique serial/model match, preserves selected profiles, and leaves
missing or ambiguous identities for manual assignment. Reservations are never
persisted. Compared with pinned RecompFrontend b1a1477, this adds reconnect
behavior to the existing assignment API. Upstream HEAD 2ddbfc8 (checked for this
change) still has no physical-slot restoration on controller removal or addition.
The port runs cleanup and restoration under its existing presentation lock,
before input sampling and rumble routing; SDL lifecycle ownership stays there.

Patch [0031](0031-ultramodern-coordinated-game-thread-shutdown.patch) adds the
coordinated shutdown contract to the runtime. The runtime owns the guest
threads, so it owns the part that only the runtime can decide: a guest thread
that is parked in the scheduler or in the external message queue has to be
released, and a guest thread that is running translated code has to reach a
point where it can unwind. `ultramodern::shutdown` publishes that contract:
`request()` stops new guest work and releases every registered guest thread,
`join_threads()` reports how many guest threads are still running, and the
owner decides what a non-zero result means. The port owns the deadline, the
store flushes, and the fallback; the runtime never decides how long a shutdown
may take. The guest threads are released by the same mechanism the runtime
already uses to destroy a thread (`thread_terminated`), so no new unwinding
primitive is introduced.

The same patch corrects two upstream defects that the shutdown exposes. A guest
thread that unwinds has to leave the guest message queue it was blocked on;
`thread_queue_remove` only ever compared the head of the queue, so it failed
to remove a thread that was not first in the queue, and the blocked threads are
ordered by priority rather than by arrival. The runtime also never signalled a
thread whose context had already been released, which turned a stale queue
entry into a use-after-free. Both are general runtime defects rather than
shutdown-specific behaviour, so they are candidates for the dependency's
upstream project.

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

The shared applicator reads [series.json](series.json); build scripts contain
no separate patch lists. Inventory validation rejects unlisted patch files,
missing files, duplicate entries, and unknown platforms. The documentation
checker validates hunk counts; application to the pinned sources verifies
that the composed series still applies.

## Maintaining a patch

1. Start from the pinned dependency commit.
2. Apply the existing patches in the manifest order for the target platform.
3. Make the smallest dependency change and add a focused test when possible.
4. Export the change as a numbered patch. Do not edit a patch by hand without
   validating its hunk counts and application.
5. Decide whether the change is AeroGauge-specific, a temporary compatibility
   fix, or a candidate for the dependency's upstream project.
6. Update series.json, this inventory, and the relevant test or reference page
   in the same pull request.

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
