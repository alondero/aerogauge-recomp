Native PC port of AeroGauge (N64, USA) via *static recompilation* (N64Recomp → C, run on
`ultramodern`+`librecomp`, rendered with RT64). Stack cloned from the proven
[Automobili Lamborghini port](F:/src/automobililamborghini-recomp) — same submodule pins,
same patches, same runtime glue; consult that repo's docs/history when a mechanism here
is unclear.

## Mental model

- **Name-routed static recompilation.** N64Recomp translates ROM functions to C by *name*.
  `ultramodern`+`librecomp` provide the libultra/OS layer — there is NO hand-rolled HLE and
  NO CP0/IRQ kernel emulation to maintain. RT64 is the renderer.
- **No splat project exists for AeroGauge.** `scripts/gen_syms_toml.py` derives function
  boundaries straight from the ROM (jal-target + prologue scan) and auto-stubs CP0/cache
  and branch-outside functions. `force_stub.txt` adds hand-curated stubs on top (the
  recompiler-error iteration loop). The porting loop is: run → it crashes/stalls in some
  primitive → identify it (libultra vs game code), route it natively or fix its boundaries.
- **ROM facts:** entrypoint 0x80000400 (same ROM↔RAM math as Lamborghini:
  rom = vram - 0x80000400 + 0x1000). CPU .text is contiguous at ROM 0x1000..0x7F4C0.
  Boot: the entry trampoline clears the DMA table then `jr $t2` → 0x800653F0 (boot body).
  Game code NAGE, 8 MiB, XXH3-64 0x89ea0690f3e22201.
- **`RecompiledFuncs/` and `src/aspMain.cpp` are ROM-derived and git-ignored** — regenerated
  by BUILDING.md step 3. Never commit them. (aspMain.us.toml does not exist yet — the audio
  ucode location has not been derived; RSP tasks currently hit the no-op scaffold.)
- **Lamborghini carry-overs deliberately disabled, marked `TODO(aerogauge)` in src/:**
  promote_vi_context (private VI-manager globals), state/menu/pace probes (state-machine
  globals), the SI controller-read bridge + __osViInit (libultra_stubs.c). Each needs its
  AeroGauge address/function derived before re-enabling. `lambo_thread_trace_dump` keeps its
  name — it lives inside runtime patch 0001.

## Non-negotiable engineering principles

- **Source is ground truth; session notes are corruptible history.** Before reasoning about
  any function, read its actual body (disassemble the ROM bytes).
- **Recompilation, not game design — NEVER invent mechanics.** No invented timeouts, counter
  seeds, transition values, or idle durations. If behaviour is missing, translate what the ROM
  actually does. A hand-rolled shortcut is *scaffolding*: name it as such, pair it with a
  tracker entry, remove it when the real code lands.
- **Measure before architecture.** measurement → data → decode → fix.
- **Verify empirically, not by name.** Prove it with a breakpoint, watchpoint, or grep for
  the actual primitive constants.
- **Ship code, not notes.** Land the smallest real increment each session.

## Debugging

- **ares is reference.** Use an ares live-debug setup for any live-ROM work (read/write
  RDRAM, "who writes X" watchpoints). VI base = `0xA4400010`; ares RDRAM is big-endian.
- **Live-debug the port** with native gdb on `build/aerogauge_modern`. Count breakpoint
  hits with `ignore N <big>` + `info breakpoints`, NOT printf-in-commands.
- **Success metric = port-vs-ares convergence**, not screenshot diffs.

## Toolchain gotchas

- **MinGW `bin` must be on PATH** or `gcc.exe` silently exits 1 (can't find its own DLLs).
- A `cc` shim may sit ahead of gcc in PATH on this box — pin `-DCMAKE_C_COMPILER`/`CXX_COMPILER`
  explicitly when configuring, or `project()` fails with "C compiler is broken".
- If a rebuild links stale recompiled code, delete stale archives under `build/` named
  `libRecompiledFuncs.a`.

## Test discipline

Gate on what changed. Recompilation-config / runtime-glue changes → build + boot smoke.

## Tracker
Use Github Issues
