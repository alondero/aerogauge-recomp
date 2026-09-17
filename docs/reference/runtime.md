# Runtime reference

The executable links the translated game against
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime). The parent
repository pins the runtime gitlink at
[ae1ffbb](https://github.com/N64Recomp/N64ModernRuntime/tree/ae1ffbb909d9f93c88c41830deb539f7feef5ed2).
The source submodule may be empty until a developer runs the recursive
submodule command in [BUILDING](../../BUILDING.md).

## What the runtime provides

The public runtime is split into two useful ideas:

- librecomp connects translated game code to host services such as ROM access,
  overlays, save files, and process setup;
- ultramodern supplies native versions of N64-style threads, message queues,
  timers, controller polling, audio work, video timing, RSP tasks, and the
  renderer callback.

The exact implementation belongs to the pinned submodule. Port code should
depend on the public runtime interfaces and its documented callback contracts,
not on an internal runtime object unless the dependency change is captured as
a local patch.

## Memory model

The runtime exposes the guest's low 8 MiB of RDRAM to translated code and
port hooks. N64 virtual addresses are not host pointers. Port code uses
N64Recomp helpers such as MEM_B, MEM_HU, and MEM_W so that KSEG address
masking and the N64 byte order are handled consistently.

The current port also reserves the guest range 0x80700000 through
0x807FFFFF for synthetic full-track data. That reservation is a local
assumption, not a runtime guarantee.

The host side owns the byte buffer, native threads, SDL objects, renderer
objects, and file handles. A guest pointer stored in RDRAM can be restored as
data; a host pointer or native call stack cannot.

## Thread and callback boundary

The current ownership model is:

| Owner | Responsibility |
| --- | --- |
| SDL/main thread | Window events, keyboard and first gamepad sampling, window changes, and rumble requests |
| Game thread(s) | Translated game functions and game-boundary port hooks |
| VI timing callback | Video timing observations, framebuffer events, and bounded test exit |
| Graphics thread | Receives graphics tasks and invokes the renderer context |
| Audio path | Runs the generated audio task and queues PCM |
| Runtime save service | EEPROM persistence and save-file coordination |
| RT64 threads | Display-list interpretation and presentation after the port supplies a context |

The main thread publishes a complete input snapshot. Game code reads that
snapshot through the runtime callback. SDL calls should remain on the SDL
thread unless the runtime explicitly documents otherwise.

The current save-state hook runs on the game thread at a frame boundary. It
copies guest RDRAM but cannot copy native register files, C stacks, or work
already in progress on another thread. This is why the feature is a
debugging aid rather than a general save system. The evidence behind the
clock rebasing and settle gate is in the
[save-state investigation](../investigations/2026-09-14-savestate.md).

## Current local runtime patches

The build applies these parent-repository patches to the pinned runtime:

| Patch | Purpose | Boundary |
| --- | --- | --- |
| 0001 | Scheduler, video timing, and audio path changes needed by the port | Local runtime integration; recheck against upstream before refresh |
| 0007 | Thread-context registry and relink support for the RDRAM save-state experiment | Transitional save-state support |
| 0012 | Correct PI DMA completion message behavior | Candidate for an upstream fix if the behavior is general |
| 0013 | Synchronous non-graphics RSP task handling with fail-soft behavior | Applied by Windows build path; Linux parity is unresolved |
| 0014 | EEPROM flush barrier before process exit | Candidate for upstream if the shutdown contract is general |

Patch 0001 also exposes a legacy thread-trace hook used by the diagnostic
path. Its symbol and environment-variable names are compatibility details of
the local patch, not a model for a new runtime API.

Patch numbers are local file names, not issue identifiers. The exact order is
in [BUILDING](../../BUILDING.md). The platform-parity question is recorded in
the [patch investigation](../investigations/2026-09-14-build-patch-parity.md).

## Failure and shutdown

The runtime can stop a bounded headless run after a VI limit. The port flushes
EEPROM before its current process-exit path. This avoids leaving generated
threads and save writes in an uncontrolled teardown, but it is not a proof of
graceful shutdown.

An error in ROM validation, a missing generated function, an unsupported
runtime callback, or a host device can stop the program before the first
frame. Keep the complete stderr output and record which boundary failed.

## Adding a runtime dependency

First ask whether the need is:

- a ROM-specific hook in the port;
- a general runtime behavior that should be proposed upstream;
- a test-only seam; or
- a missing future mod interface.

Do not widen the runtime API to make one guest-memory workaround easier. A
runtime change needs a minimal test, a local patch with a clear base commit,
and a maintainer decision about upstreaming.
