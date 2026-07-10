#!/usr/bin/env python3
"""Generate the runtime-addressed whole-ROM syms.toml + us.toml for AeroGauge (USA).

Model: same ROM+syms N64Recomp mode as the Automobili Lamborghini port this stack was
cloned from (the drmario64 template model) — ONE contiguous .text section at
rom=0x1000 / vram=0x80000400 (the ROM-header entrypoint), functions listed with
name/vram/size, absolute addressing, no relocs.

Unlike the Lamborghini port there is NO pre-existing splat disassembly to source
function boundaries from, so this script derives them from the ROM itself:

  * Measured code extent (2026-07-10 density scan, scripts history): CPU text is
    contiguous at ROM 0x1000..~0x7F4C0. Every jal located inside that window targets
    inside it (zero out-of-window targets), after which instruction decoding turns to
    data/RSP-ucode noise. The tail (last ~0x100) is the CP0 exception handler.
  * Function STARTS = the real entrypoint trampoline (0x80000400), its static jr
    targets (see BOOT_EXTRA below), every in-window jal target, plus every
    `addiu $sp,$sp,-N` prologue that immediately follows a function terminator
    (jr + delay slot, or nop padding) — the standard IDO function-boundary signal.
    Sizes span to the next start (oversize is harmless; branches stay internal).
  * PRE-STUBS: functions containing CP0/cache instructions (the libultra kernel layer
    ultramodern replaces wholesale) and functions whose branches escape their derived
    range (mis-split shared-tail code — the Lamborghini SPLIT_MERGES class, to be
    grown case-by-case as the port needs them) are emitted as `stubs` so the whole-ROM
    recompile succeeds. force_stub.txt adds hand-curated entries on top (one name per
    line, '#' comments) — the iteration loop for recompiler errors.

Usage:  python scripts/gen_syms_toml.py    (from the repo root; reads the ROM +
        force_stub.txt, writes aerogauge.syms.toml + aerogauge.us.toml)
"""
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROM_FILE = REPO / "AeroGauge (USA).z64"
OUT_SYMS = REPO / "aerogauge.syms.toml"
OUT_CFG = REPO / "aerogauge.us.toml"
FORCE_STUB = REPO / "force_stub.txt"

ENTRY = 0x80000400           # ROM header bytes 0x08-0x0B
SECTION_ROM = 0x1000
CODE_ROM_END = 0x7F4C0       # end of contiguous CPU text (density scan; see docstring)


def rom_to_vram(off):
    return ENTRY + (off - SECTION_ROM)


def vram_to_rom(v):
    return SECTION_ROM + (v - ENTRY)


# Boot-chain starts that are NOT jal targets (verified from the entry disassembly):
#   0x80000400  entry trampoline: clears the DMA table then `jr $t2` -> 0x800653f0
#   0x80000450  own `addiu $sp,-0x38` prologue right after the trampoline pad
#               (also caught by the prologue scan; listed for explicitness)
#   0x800653f0  the static jr target -- N64Recomp emits the trampoline's `jr $t2` as a
#               runtime get_function() lookup, so this MUST be a registered entry.
BOOT_EXTRA = [0x80000400, 0x80000450, 0x800653F0]


def main():
    if not ROM_FILE.exists():
        sys.exit(f"missing ROM: {ROM_FILE}")
    rom = ROM_FILE.read_bytes()

    def word(off):
        return struct.unpack(">I", rom[off:off + 4])[0]

    lo, hi = SECTION_ROM, CODE_ROM_END
    vlo, vhi = rom_to_vram(lo), rom_to_vram(hi)

    # --- pass 1: function starts ------------------------------------------------
    starts = set(BOOT_EXTRA)
    for off in range(lo, hi, 4):
        w = word(off)
        if (w >> 26) == 3:  # jal
            t = 0x80000000 | ((w & 0x03FFFFFF) << 2)
            if vlo <= t < vhi:
                starts.add(t)
    # prologue after a terminator: addiu $sp,$sp,-N preceded by (jr xx + delay) or nop pad
    def is_jr(w):
        return (w >> 26) == 0 and (w & 0x3F) == 8
    for off in range(lo + 8, hi, 4):
        w = word(off)
        if (w >> 16) == 0x27BD and (w & 0x8000):  # addiu $sp,$sp,-N
            if word(off - 4) == 0 or is_jr(word(off - 8)):
                starts.add(rom_to_vram(off))

    starts = sorted(starts)

    # --- pass 2: sizes + pre-stub analysis ---------------------------------------
    funcs = []          # (name, vram, size)
    auto_stubs = set()
    for i, v in enumerate(starts):
        end_v = starts[i + 1] if i + 1 < len(starts) else vhi
        size = end_v - v
        name = "func_%08X" % v
        cop0 = False
        branch_out = False
        for off in range(vram_to_rom(v), vram_to_rom(end_v), 4):
            w = word(off)
            op = w >> 26
            if op == 0x10 or op == 0x2F:      # COP0 (mfc0/mtc0/eret/tlb*) or CACHE
                cop0 = True
            # relative branches: beq/bne/blez/bgtz + likely forms + REGIMM bltz/bgez(al)(l)
            is_branch = op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17)
            if op == 1 and ((w >> 16) & 0x1F) in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13):
                is_branch = True
            if is_branch:
                imm = struct.unpack(">h", struct.pack(">H", w & 0xFFFF))[0]
                tgt = rom_to_vram(off) + 4 + imm * 4
                if not (v <= tgt < end_v):
                    branch_out = True
        if cop0 or branch_out:
            auto_stubs.add(name)
        funcs.append((name, v, size))

    # entrypoint gets renamed by N64Recomp itself (vram==ENTRY && rom==SECTION_ROM)

    # --- force_stub.txt (hand-curated error-loop additions) ----------------------
    force = set()
    if FORCE_STUB.exists():
        for line in FORCE_STUB.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                force.add(line)
    known = {n for n, _, _ in funcs}
    unknown_force = force - known
    if unknown_force:
        print(f"WARNING: force_stub.txt names not in the function map: {sorted(unknown_force)}")
    stubs = sorted((auto_stubs | force) & known)

    # --- emit syms.toml -----------------------------------------------------------
    with OUT_SYMS.open("w", newline="\n") as f:
        f.write("# Autogenerated by scripts/gen_syms_toml.py -- DO NOT EDIT BY HAND.\n")
        f.write("# Runtime-addressed whole-ROM symbols for ultramodern/librecomp.\n")
        f.write("# Source: jal-target + prologue scan of the ROM (no splat project exists).\n")
        f.write("[[section]]\n")
        f.write('name = ".text"\n')
        f.write(f"rom = 0x{SECTION_ROM:X}\n")
        f.write(f"vram = 0x{ENTRY:08X}\n")
        f.write(f"size = 0x{hi - lo:X}\n\n")
        f.write("functions = [\n")
        for name, v, size in funcs:
            f.write(f'    {{ name = "{name}", vram = 0x{v:08x}, size = 0x{size:x} }},\n')
        f.write("]\n")

    # --- emit us.toml ---------------------------------------------------------------
    with OUT_CFG.open("w", newline="\n") as f:
        f.write("# AUTOGENERATED by scripts/gen_syms_toml.py -- DO NOT EDIT BY HAND.\n")
        f.write("# Whole-ROM recompile config (ROM+syms mode; see the script docstring).\n")
        f.write("# Run (see BUILDING.md step 3):\n")
        f.write("#   cmake --build build --target N64RecompCLI\n")
        f.write("#   ./build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp aerogauge.us.toml\n\n")
        f.write("[input]\n")
        f.write(f"entrypoint = 0x{ENTRY:08X}\n")
        f.write('output_func_path = "RecompiledFuncs"\n')
        f.write('symbols_file_path = "aerogauge.syms.toml"\n')
        f.write('rom_file_path = "AeroGauge (USA).z64"\n\n')
        f.write("[patches]\n")
        f.write("stubs = [\n")
        for n in stubs:
            f.write(f'    "{n}",\n')
        f.write("]\n")

    n_auto = len(auto_stubs & known)
    print(f"functions: {len(funcs)}  (jal+prologue-derived)")
    print(f"stubs: {len(stubs)}  (auto CP0/branch-out: {n_auto}, force_stub.txt: {len(force & known)})")
    print(f"wrote {OUT_SYMS.name} + {OUT_CFG.name}")


if __name__ == "__main__":
    main()
