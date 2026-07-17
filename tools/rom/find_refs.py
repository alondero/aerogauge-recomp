#!/usr/bin/env python3
"""Find references to a RAM address: pointer words + lui/lo16 code references.

The two reference classes that matter in this ROM (see gen_syms_toml.py's
INDIRECT_STARTS derivation): (a) 32-bit big-endian data words holding the
address anywhere in the ROM (pointer/jump/object tables), and (b) code that
constructs the address with `lui %hi` followed by an addiu or load/store with
the matching signed %lo -- the standard IDO global-access idiom.

Usage:
    python tools/rom/find_refs.py 0x8013FF80            # who touches the scene word?
    python tools/rom/find_refs.py 0x8013FF80 --stores   # only sw/sh/sb (who WRITES it)
    python tools/rom/find_refs.py func_80022408         # pointer tables holding a func

Code hits show the composing instruction + containing function; data hits show
the ROM offset (and vram if it falls inside .text).
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from romlib import (load_rom, load_functions, lookup, rom_to_vram,
                    containing_function, word_at, ROM_TEXT_START, ROM_TEXT_END)

STORES = {0x28: "sb", 0x29: "sh", 0x2B: "sw", 0x2D: "sd", 0x3D: "sdc1", 0x39: "swc1"}
LOADS = {0x20: "lb", 0x24: "lbu", 0x21: "lh", 0x25: "lhu", 0x23: "lw", 0x27: "lwu",
         0x37: "ld", 0x31: "lwc1", 0x35: "ldc1"}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("target", help="hex vram address or func_XXXXXXXX name")
    ap.add_argument("--stores", action="store_true", help="code hits: stores only")
    ap.add_argument("--rom", default=None)
    args = ap.parse_args()

    funcs = load_functions()
    try:
        addr = int(args.target, 16)
        if addr < 0x80000000:
            addr |= 0x80000000
    except ValueError:
        addr = lookup(funcs, args.target)[0]

    rom = load_rom(args.rom)
    hi = ((addr + 0x8000) >> 16) & 0xFFFF          # %hi with lo16 sign carry
    lo = addr & 0xFFFF
    lo_signed = lo - 0x10000 if lo >= 0x8000 else lo

    print(f"references to 0x{addr:08X}  (%hi=0x{hi:04X} %lo={lo_signed:#x})")

    data_hits = 0
    print("\n-- data words (pointer tables) --")
    for off in range(0, len(rom) - 3, 4):
        if word_at(rom, off) == addr:
            data_hits += 1
            loc = f"ROM 0x{off:X}"
            if ROM_TEXT_START <= off < ROM_TEXT_END:
                v = rom_to_vram(off)
                f = containing_function(funcs, v)
                loc += f" (vram 0x{v:08X}, inside {f[2] if f else '.text gap'})"
            if data_hits <= 40:
                print(f"  {loc}")
    if data_hits > 40:
        print(f"  ... {data_hits - 40} more (total {data_hits})")
    if not data_hits:
        print("  (none)")

    print("\n-- code (lui %hi + matching %lo within 24 instrs, same base reg) --")
    code_hits = 0
    lui_reg = {}  # reg -> (vram of lui, age)
    for off in range(ROM_TEXT_START, min(ROM_TEXT_END, len(rom)), 4):
        w = word_at(rom, off)
        op = w >> 26
        rt = (w >> 16) & 31
        rs = (w >> 21) & 31
        imm = w & 0xFFFF
        vram = rom_to_vram(off)
        for r in list(lui_reg):
            if vram - lui_reg[r] > 24 * 4:
                del lui_reg[r]
        if op == 0x0F:  # lui
            if imm == hi:
                lui_reg[rt] = vram
            else:
                lui_reg.pop(rt, None)
            continue
        mnem = None
        if imm == lo:
            if op == 0x09 and rs in lui_reg and not args.stores:      # addiu
                mnem = "addiu"
            elif op in STORES and rs in lui_reg:
                mnem = STORES[op]
            elif op in LOADS and rs in lui_reg and not args.stores:
                mnem = LOADS[op]
        if mnem:
            f = containing_function(funcs, vram)
            code_hits += 1
            print(f"  0x{vram:08X}  {mnem:<6} (lui at 0x{lui_reg[rs]:08X})  in {f[2] if f else '?'}")
        # a non-lui write into a tracked reg invalidates it
        if op == 0x09 and rt in lui_reg and rs != rt and imm != lo:
            lui_reg.pop(rt, None)
    if not code_hits:
        print("  (none -- address may be reached via a struct base pointer instead)")


if __name__ == "__main__":
    main()
