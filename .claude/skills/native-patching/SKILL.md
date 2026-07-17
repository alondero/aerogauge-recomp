---
name: native-patching
description: Make a surgical change to the recompiled game — route a function natively, hook/bracket a game function, stub a recompiler error, or patch a submodule (RT64/N64ModernRuntime). Use whenever a fix or enhancement must touch recompilation config, add a native replacement in src/, or change lib/ submodule code.
---

# Surgical patching of the recompilation

There are exactly four mechanisms. Pick the narrowest one that works, and
remember the iron rule: **recompilation, not game design** — translate what the
ROM does; never invent timeouts, seeds, or transition values. A shortcut is
scaffolding: name it as such and pair it with a tracker issue.

## Mechanism 1 — LIBULTRA_NAMES (route an OS function to librecomp/ultramodern)

For libultra functions only. Add `vram: "osName"` to `LIBULTRA_NAMES` in
`scripts/gen_syms_toml.py` **with a byte-evidence comment** (the dict comments
are the authoritative identification record). N64Recomp then renames call sites
`<name>_recomp` and the runtime provides it.

## Mechanism 2 — HOOK_NAMES (replace a GAME function with a native)

For enhancements that swap a whole function body (e.g. `aero_draw_distance.cpp`
replacing guPerspectiveF). Add to `HOOK_NAMES` in `gen_syms_toml.py` → emitted
as `[patches] ignored`. N64Recomp skips the body but does NOT rename call
sites, so `src/` must export the bare name, `extern "C"`, with signature
`void name(uint8_t* rdram, recomp_context* ctx)`.

**Guest ABI (o32):** args in `ctx->r4..r7`, further args on the stack via
`MEM_W(0x10 + 4*n, ctx->r29)`; floats arrive as bit patterns in the low 32 bits
of the gpr; return in `ctx->r4`-equivalent v0 (`ctx->r2`). Implicit-decl
warnings in the generated C are expected and harmless.

## Mechanism 3 — PATCH_BLOCKS ([[patches.hook]] brackets around a game function)

For observing/augmenting without replacing (e.g. the HUD widescreen retag pass
brackets `func_80022408`). Add the `[[patches.hook]]` toml text to
`PATCH_BLOCKS` in `gen_syms_toml.py` — **never edit aerogauge.us.toml directly;
it is generator output and hand edits are silently dropped on regen** (a hook
also enforces this). `func = "func_X"`, `text = "native_name"`, at entry or a
`before_vram` epilogue site.

Hook-native conventions proven here: read the DL cursor via the holder pointer
in `ctx->r4` (AeroGauge has no global cursor — see rom-map.md); gate on
scene/phase globals, never on frame counts; give every behavioral hook an
`AERO_*` env kill-switch; read game state INSIDE the hook (offline RDRAM dumps
race the next frame's rebuild).

## Mechanism 4 — patches/*.patch (submodule fixes: RT64, N64ModernRuntime)

For runtime/renderer bugs (e.g. 0012 PI DMA OSIoMesg). Workflow:
1. Edit `lib/rt64` or `lib/N64ModernRuntime` in place; build + verify.
2. Generate the patch with `git diff --no-index` against a pristine snapshot —
   plain `git -C lib/rt64 diff` bundles the already-applied patches' hunks.
3. Number it after the last patch; **apply order matters** (0011 must follow
   0008/0009 — its context contains their lines). build.ps1 replays the series
   onto a reset submodule; it fails on an already-patched tree.
4. If RT64 has duplicated logic, patch every copy (`coversWholeWidth` exists in
   both rt64_projection_processor.cpp and rt64_framebuffer_renderer.cpp).

## Recompiler-error loop (force_stub.txt)

Whole-ROM recompile fails on a function? `force_stub.txt` (one name per line)
stubs it. Only for functions the port never needs (CP0/kernel); if game code
lands here, fix its boundaries instead (see rom-analysis skill).

## THE build-flow gotcha (has silently eaten fixes twice)

`build.ps1` CONSUMES the checked-in `aerogauge.us.toml`; it does NOT run the
generator. After ANY change to gen_syms_toml.py you MUST:

```powershell
python scripts/gen_syms_toml.py     # regenerates aerogauge.{syms,us}.toml (UTF-8!)
# then re-run N64Recomp + rebuild (build.ps1, or on an already-patched tree:
# cmake --build build --target aerogauge_modern)
```

Otherwise the hooks silently don't land. Also: write tomls as UTF-8 — cp1252
em-dashes break N64Recomp's toml parser.

Every mechanism ends with: host test where the logic is pure (see `tests/`,
compile line in each file's header; `#undef NDEBUG` or Release vacuously
passes), then a boot smoke (build-and-verify skill).
