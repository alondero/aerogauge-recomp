---
name: ares-debugger
description: Drive the ares emulator headless via its GDB DebugServer — read/write RDRAM, watchpoints to find writers, dump memory from a running game. ares runs the real ROM, so it is ground truth for what the port should reproduce. Use for "who writes address X", "does the real game do Y", or any port-vs-reference comparison.
---

# ares DebugServer — ground-truth reference debugging

ares ships a GDB-compatible TCP server: read/write any RDRAM address, set
watchpoints, dump memory from a running game — no human in the loop.
**Success metric for the port = port-vs-ares convergence.**

## Bootstrap (first use in a fresh clone/worktree)

The harness is NOT checked in (gitignored; see .gitignore's note). Copy it from
the Lamborghini repo, then point it at this ROM:

```powershell
robocopy F:\src\automobililamborghini-recomp\tools\emu_instrumentation `
         tools\emu_instrumentation /E
```

- `ares_session.py` defaults `DEFAULT_ROM` to the Lambo ROM and `ARES_EXE` to
  `<repo>/tools/emulators/...` — pass both explicitly instead of editing:
  `ares_session(rom=Path("AeroGauge (USA).z64"), ares_exe=Path(r"F:\src\automobililamborghini-recomp\tools\emulators\ares-base\ares-v147\ares.exe"), port=9150)`

## Quickstart

```python
import sys; sys.path.insert(0, r'tools\emu_instrumentation')
from pathlib import Path
from ares_session import ares_session
ARES = Path(r"F:\src\automobililamborghini-recomp\tools\emulators\ares-base\ares-v147\ares.exe")

with ares_session(rom=Path("AeroGauge (USA).z64"), ares_exe=ARES, port=9150) as c:
    print(f'VI_CURRENT: 0x{c.read32(0xA4400010):08X}')
# ares is taskkilled on exit even if the block raises.
```

## Recipes

**Find the writer of an address** (the highest-value workflow):
```python
with ares_session(rom=ROM, ares_exe=ARES, port=9150, boot_wait=2.0) as c:
    c.set_watchpoint(0x8013FF80, kind='write')
    stop = c.continue_until_halt(timeout=10)
    info = c.parse_stop_reason(stop)      # {'signal':5,'kind':'watch','addr':...}
```
For EARLY-BOOT one-shot writes pass `await_gdb=True` (CPU frozen at instr 0;
arm the watchpoint, THEN continue). Don't single-step after a hit (spurious
signal-16 halts) — read the pre-store value or just continue.

**Dump memory / poke state**: `c.dump_rdram(addr, n)` (2KB chunks, ~30 KB/s —
fine for DLs/tables; a full 8MB dump takes minutes), `c.write_mem(addr, bytes)`
(bytes land as given — ares RDRAM is big-endian).

**Per-frame VI capture**: `python tools/emu_instrumentation/run_ares_debug.py "AeroGauge (USA).z64" --frames 3 --output ares_vi.json --port 9151` (check the script accepts an ares-exe override; else set ARES_EXE).

## Gotchas (verified the hard way)

- **Software (Z0) AND hardware (Z1) breakpoints are unreliable** — Z0 gives two
  phantom hits at ~0.55s/~0.77s after boot at ANY address (boot code-copy
  artifact). **Watchpoints (Z2/Z3) are the reliable tool.** To confirm a
  function runs, watch its distinctive side-effect address instead.
- **Watchpoint stop registers do NOT give the writer's PC** — exception-handler
  state aliases into plausible-looking values. Confirm/time the write with Z2,
  then find the writer statically: `python tools/rom/find_refs.py <addr> --stores`
  (usually faster than any live method).
- ares RDRAM is big-endian; for a halfword, mask `read32(addr & ~3)` keyed on
  alignment — a blind `& 0xFFFF` returns the adjacent field. No byteswapping.
- `VI_CURRENT` frame detection: large-decrease detector (`prev - cur > 200`),
  never `cur == 0`.
- DPC/RDP registers (`0xA3Cxxxxx`) are unreachable (return 0).
- No wall-clock timing under instrumentation — report event ORDER, or anchor to
  a per-VI game counter.
- Unique port per concurrent session (9150, 9151, …); ~2.5s spin-up — batch
  probes into one block. ares opens a visible window; the helper kills it.

## USF-rip cross-check (audio work)

The Zophar USF rip's `NUS-NAGE-USA.usflib` SR64 chunks enumerate the exact ROM
ranges the music engine reads (blocks `[len][addr][data]`, RDRAM at
addr−0x2660). Fastest sanity check that you're staring at the right engine;
anything absent from the savestate is not music.
