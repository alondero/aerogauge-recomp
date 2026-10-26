# Runtime reference

The executable links the translated game against
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime). The parent
repository pins the runtime gitlink at
[cdf5abb](https://github.com/N64Recomp/N64ModernRuntime/tree/cdf5abbd5026fef5c364c676e4667c45e42b6863).
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
| SDL/main thread | Window events, RecompFrontend input-state updates, window changes, and rumble requests |
| Game thread(s) | Translated game functions and game-boundary port hooks |
| VI timing callback | Video timing observations, framebuffer events, and bounded test exit |
| Graphics thread | Receives graphics tasks and invokes the renderer context |
| Audio path | Runs the generated audio task and queues PCM |
| Runtime save service | EEPROM persistence and save-file coordination |
| RT64 threads | Display-list interpretation and presentation after the port supplies a context |

The main thread updates RecompFrontend's input state. Game code reads the
profile mapping through the runtime callback. SDL calls should remain on the
SDL thread unless the runtime explicitly documents otherwise.

The current save-state hook runs on the game thread at a frame boundary. It
copies guest RDRAM but cannot copy native register files, C stacks, or work
already in progress on another thread. This is why the feature is a
debugging aid rather than a general save system. A live renderer can still
read guest memory while a load replaces it, so the windowed load path remains
unsafe.

## Local runtime patch boundary

The [patch inventory](../../patches/README.md) lists every runtime patch, its
host coverage, and its purpose. Patch 0001 also exposes a diagnostic
thread-trace hook; its names are compatibility details of the local patch, not
a model for a new runtime API. Patch 0018 makes the graphics-config accessor
return a snapshot copy instead of an unlocked reference, so the graphics
thread, the VI callback, and the game thread can each read the live
configuration while the settings menu replaces it. That is a repair of a
runtime synchronization defect, not an API shape to copy elsewhere. Patch
0031 adds the coordinated game-thread shutdown contract described under
[Failure and shutdown](#failure-and-shutdown); the contract belongs to the
runtime because the runtime owns the game threads, while the deadline, the
store flushes, and the fallback stay in the port. The exact application order
is part of [BUILDING](../../BUILDING.md).

## Failure and shutdown

### The coordinated game-thread shutdown contract

Local patch [0031](../../patches/0031-ultramodern-coordinated-game-thread-shutdown.patch)
adds `ultramodern/shutdown.hpp` to the runtime. It is the runtime's half of the
shutdown handshake: the runtime owns the game threads, so the runtime owns the
part that only the runtime can decide.

| Call | Owner | Meaning |
| --- | --- | --- |
| `shutdown::register_thread(token, wake)` | runtime | A guest thread exists and can be parked. Registration happens before the thread can block, so a shutdown request cannot miss it. |
| `shutdown::request()` | port | Stop new guest work. Publishes the flag and releases every registered guest thread. Idempotent. |
| `shutdown::requested()` | runtime | The flag is published. Guest code checks it at its blocking points. |
| `shutdown::pending_threads()` | port | How many registered guest threads have not finished unwinding yet. |
| `shutdown::join_threads(timeout)` | port | Wait up to `timeout` for every guest thread to unwind. Returns the number still running; zero means RDRAM and the device resources can be released. |

How a guest thread stops:

- A guest thread parked in the scheduler (`osRecvMesg`, `osSendMesg`,
  `osStopThread`) waits on its own semaphore. The shutdown request signals that
  semaphore, the thread wakes, observes the flag, and unwinds.
- A guest thread parked in the runtime's external message queue (`pause_self`,
  `yield_self`) waits with a bounded timeout. It observes the flag within that
  bound and unwinds.
- A guest thread that is running translated code keeps running until its next
  blocking point. This is why the join has to happen before RDRAM is released:
  translated code reads and writes RDRAM.
- The unwind uses the runtime's existing `thread_terminated` exception, the same
  mechanism `osDestroyThread` uses. No new unwinding primitive is introduced.

A guest thread that unwinds must also leave the guest queue it was blocked on.
The message queues live in guest memory, so a stale entry would make a later
send resume a thread whose context has already been released. The runtime
therefore removes the thread from its queue when it finishes, and it never
signals a thread whose context is gone. The queue removal needs a correct list
walk, so the patch also corrects `thread_queue_remove`: upstream, it only ever
compared the head of the queue, so it failed to remove a thread that was not
first in the queue. The blocked threads are ordered by priority, not by
arrival, so the head-only behaviour was not enough.

Two properties are load-bearing and are pinned by the
[shutdown_contract](../../tests/test_shutdown_contract.cpp) host test:

- `request()` releases every registered guest thread exactly once. A second
  release would leave a stray scheduler signal behind, and a guest thread that
  consumed it could resume without being scheduled.
- A guest thread that is registered after the request is released at once. A
  shutdown admits no new guest work.

The runtime never decides how long a shutdown may take. The port owns the
deadline and the fallback: a join that did not finish means the guest threads
are still running, so the port must not release RDRAM, the renderer, or the
audio device, and must use the bounded exit instead.

### The bounded exit

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
