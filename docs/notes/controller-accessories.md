# Controller accessories

The port keeps the ROM's PFS filesystem recompiled. `src/aero_pak.cpp` replaces
only `__osPfsGetStatus`, `__osContRamRead` and `__osContRamWrite`, using the
generator's `NATIVE_NAMES` routing. Removing `osPfsInitPak` from `LIBULTRA_NAMES`
is essential: librecomp's implementation always returns `PFS_ERR_NOPACK`.

The single controller-0 image is 32 KiB, with the same SDK-compatible fresh ID
blocks and inode layout as Automobili Lamborghini. Every successful block write
publishes a complete image through a temporary file and atomic replacement.
This costs disk I/O during saves but gives the guest synchronous error reporting
and keeps saves safe across the port's deliberate `_Exit`. Failed publication
retains the previous disk and memory image. Wrong-sized/unreadable files expose
no Pak; correctly sized images remain subject to the original game's checking
and repair logic. The format is raw MPK only; there is no emulator-container import UI.

EEPROM remains the existing runtime 4 Kbit save. Patch 0014 adds a saving-thread
barrier before `_Exit`, including concurrent close/watchdog calls and a stopped
worker. The runtime's existing `.bin`/backup format and filename stay intact.
The barrier also handles normal runtime teardown without waiting on a dead worker.

## ROM evidence (USA)

Derived using `py -3 tools/rom/disasm.py` against ROM bytes, 2026-09-12:

| Address | Evidence / role |
|---|---|
| `0x8006B440` | InitPak fills OSPfs, reads ID block 1, checks the two ID sums, derives bank/inode/directory offsets and calls Checker. |
| `0x800742F0` | GetStatus stages a channel query; returns 1 for missing Pak, 2 for changed Pak, 4 for a controller error. |
| `0x80075290` | ContRamRead stages command 2, uses a 16-bit block index, copies 32 bytes to a3. |
| `0x80077260` | ContRamWrite stages command 3; fifth o32 argument is force. Silently skips blocks 1..6 unless force=1. |
| `0x8007521C` | SelectBank writes a repeated bank byte to block 0x400 (byte address 0x8000). |
| `0x800740F0/0x80074134` | SI access lock/unlock use ordinary message queues; retained as recompiled code. GetAccess lazily creates the queue. |
| `0x8006F040/0x8006CDE0` | AllocateFile / FindFile. |
| `0x8006EC1C/0x8006D0F0` | ReadWriteFile / FreeBlocks. |
| `0x8006CFA0` | NumFiles (three arguments, not FreeBlocks). |
| `0x80063930` | Uncalled motor test checks controller D-pad bits 0x800/0x400 and invokes MotorStart/Stop. Neither it nor MotorInit has direct callers or discovered pointer references. |
| `0x800588F4..0x80058AD8` | Collision vectors and speed determine this tick's damage at craft+0x24. All branches converge at 0x80058AD8; positive damage accumulates at +0x28 in 0x80058B00 and becomes a percentage at +0x2C. |
| `0x8005AE00..0x8005AE68` | Turbo timer at craft+0x55 controls thrust and heat; ROM decrements/cancels it. |
| `0x800581BC..0x800581CC` | Craft+4 is its input callback; 0x8005C750 identifies P1. |

Race haptics are an explicitly requested port enhancement (impacts plus turbo),
not a restoration of the unused motor test. A hook at `0x80058AD8` observes
positive collision damage and the turbo timer for P1 only. It never writes guest
state. Impact pulses last 120 ms and override the lighter turbo effect; turbo
refreshes with a 150 ms expiry while active. These are host feedback durations,
not game-mechanic timers. A frame hook clears effects outside race scene 5,
phase 3, and excludes attract mode 7. SDL owns the physical device on the main
thread and receives only atomic requests; detachment clears outstanding effects.

## Verification

- `controller_accessories`: block boundaries, byte order, reopen, failed atomic
  publication, protected ID writes, disabled/absent channels, malformed files;
  impact/turbo priority, expiry, P1 filtering, pause and disable behavior.
- `controller_pak_rom_filesystem`: extracts the actual recompiled SDK call closure
  into the build directory; allocates/writes a 0x2DE0-byte note, resets guest RAM
  and SDK caches, then reopens/finds/reads every byte and checks remaining capacity.
  Only SI-lock queue primitives and the hardware clock are stubbed.
- `eeprom_exit_flush`: uses the actual runtime saver and file helpers to verify
  immediate flush after 64 EEPROM block writes, concurrent flushes, worker shutdown
  racing a flush and flush after join.
- Live headless race: 2,400 VIs, Canyon Rush, P1 acceleration, then turbo plus
  steering into a wall. GDB observed both events at the shipping haptic hook;
  the game exited normally. `tests/haptics_watch.gdb` captures the assertions.

The SDK round trip verifies ghost storage capacity/format, not the full in-game
Time Attack menu workflow. Physical gamepad vibration strength still needs a
human hardware check; headless tests verify event routing and motor requests.
