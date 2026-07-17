---
name: rom-analysis
description: Static analysis of the AeroGauge ROM — disassemble a function, find callers, find who reads/writes a global, decode tables, identify libultra/ucode blobs. Use BEFORE reasoning about any game function (source is ground truth) and whenever a task says "find the function that does X".
---

# Static ROM analysis

Ground rule (CLAUDE.md): **read the actual ROM bytes before reasoning about a
function.** These tools make that cheap — never hand-roll capstone or hexdump
the ROM into context.

Check `docs/notes/rom-map.md` FIRST — the address you're hunting is likely
already derived (scene manager, race params, music, HUD dispatch, zones,
libultra globals). Only derive what the map doesn't have, then add it there.

## The three tools (`tools/rom/`, need the ROM at the repo root)

```bash
# 1. Disassemble a function (bounds from aerogauge.syms.toml). Annotates jal
#    targets with func names and lui/%lo pairs with the composed address, so
#    global references read straight off the listing.
python tools/rom/disasm.py func_80015C8C
python tools/rom/disasm.py 0x80015CD4 --count 16     # window inside a function

# 2. Who calls this function? (whole-ROM jal scan)
python tools/rom/callers.py func_80015F2C

# 3. Who references / WRITES this global or function pointer?
python tools/rom/find_refs.py 0x8013FF80 --stores    # writers only
python tools/rom/find_refs.py func_80022408          # pointer tables holding it
```

`callers.py` sees only direct `jal`. If it prints none, the function is reached
via `jalr` (object handlers, jump tables) — run `find_refs.py` on its address to
find the pointer table or the `lui/addiu` constructor site.

## Typical hunts

- **"What does the game do at event X?"** — find a global that changes at X
  (rom-map, or ares watchpoint via the `ares-debugger` skill), then
  `find_refs.py --stores` → disassemble each writer.
- **Decode a table**: get the base from a `; = 0x...` annotation, then read raw
  bytes with a 5-line Python snippet using `tools/rom/romlib.py`
  (`load_rom()`, `word_at()`, `vram_to_rom()`).
- **Walk a call chain down**: disasm → follow the `; -> func_...` annotations.
  Walk it **up** with `callers.py` repeatedly.

## Identification lessons (paid for in wasted sessions)

- **Classify a ucode/blob over its WHOLE length, never the first words.** The
  aspMain mixer's scalar DMA prologue was mistaken for CPU code once; the
  proof was 42% COP2/LWC2/SWC2 density across all 0xE1C bytes. Cross-ROM byte
  match against the Lamborghini port's known blobs is a legit derivation —
  SDK blobs are shared across same-era titles.
- **Jump-table case labels are not functions.** Reject an INDIRECT_STARTS
  candidate if it is branch-reachable from earlier inside its containing span.
- **Boundary errors decode as:** `Failed to find function at 0x...` = a
  prologue-less leaf reached via function pointer → add to INDIRECT_STARTS in
  `scripts/gen_syms_toml.py`. A recompiler error on an odd instruction
  (e.g. `trunc.l.d`) = an absorbed float leaf → force_stub.txt or boundary fix.
- Function names are `func_<vram>`; there is no splat project. The
  per-function byte evidence for every libultra identification lives as
  comments in `scripts/gen_syms_toml.py` — treat those as the authoritative
  record and follow the same evidence-comment convention when adding.

## After deriving something new

Add it to `docs/notes/rom-map.md` with a one-line method note. An address
without recorded derivation method will (correctly) be re-derived by the next
session — wasted work.
