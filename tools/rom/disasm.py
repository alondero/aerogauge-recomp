#!/usr/bin/env python3
"""Disassemble a ROM function (or address range) with symbol annotations.

The project's ground-truth rule is "read the actual ROM bytes before reasoning
about a function" -- this is the tool that does it without hand-rolling capstone.

Usage:
    python tools/rom/disasm.py func_80015C8C
    python tools/rom/disasm.py 0x80015C8C                 # containing function
    python tools/rom/disasm.py 0x80015C8C --count 12      # 12 instrs from addr
    python tools/rom/disasm.py func_80015C8C --rom path/to.z64

Output columns: vram, raw word, mnemonic + operands, and `; -> func_XXXXXXXX`
annotations on jal/j targets. lui/lo16 pairs are annotated with the composed
address (`; = 0x8013FF80`) so global references read off directly.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from romlib import load_rom, load_functions, lookup, vram_to_rom, containing_function

try:
    from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS64, CS_MODE_BIG_ENDIAN
except ImportError:
    sys.exit("capstone missing: pip install capstone")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("target", help="func_XXXXXXXX name or hex vram address")
    ap.add_argument("--count", type=int, help="instruction count (default: whole function)")
    ap.add_argument("--rom", default=None)
    args = ap.parse_args()

    rom = load_rom(args.rom)
    funcs = load_functions()
    fv, fs, fn = lookup(funcs, args.target)

    try:
        start = int(args.target, 16)
        if start < 0x80000000:
            start |= 0x80000000
    except ValueError:
        start = fv
    if args.count:
        size = args.count * 4
    else:
        size = fv + fs - start

    print(f"; {fn} vram=0x{fv:08X} size=0x{fs:X} rom=0x{vram_to_rom(fv):X}"
          + (f"  (window 0x{start:08X}+{size:#x})" if start != fv or args.count else ""))

    md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_BIG_ENDIAN)
    code = rom[vram_to_rom(start):vram_to_rom(start) + size]
    lui = {}  # reg -> hi16 value, tracked linearly for %hi/%lo pairing
    for insn in md.disasm(code, start):
        raw = int.from_bytes(code[insn.address - start:insn.address - start + 4], "big")
        note = ""
        ops = insn.op_str.replace("$", "")
        if insn.mnemonic in ("jal", "j"):
            tgt = int(insn.op_str, 16)
            f = containing_function(funcs, tgt)
            if f:
                note = f" ; -> {f[2]}" + ("" if f[0] == tgt else f"+0x{tgt - f[0]:x}")
        elif insn.mnemonic == "lui":
            reg, imm = [x.strip() for x in ops.split(",")]
            lui[reg] = int(imm, 16) << 16
        else:
            # pair an addiu/memop lo16 with the last lui into the same base reg
            parts = [x.strip() for x in ops.split(",")]
            base = None
            imm = None
            if insn.mnemonic == "addiu" and len(parts) == 3 and parts[1] in lui:
                base, imm = parts[1], parts[2]
            elif "(" in ops:
                mem = ops[ops.index("(") + 1:ops.index(")")]
                off = ops.split(",")[1].split("(")[0].strip()
                if mem in lui:
                    base, imm = mem, off
            if base is not None:
                try:
                    lo = int(imm, 16) if imm.lower().startswith(("0x", "-0x")) else int(imm)
                    if lo >= 0x8000:
                        lo -= 0x10000
                    note = f" ; = 0x{(lui[base] + lo) & 0xFFFFFFFF:08X}"
                except ValueError:
                    pass
            # any non-store that writes a tracked reg invalidates its %hi
            no_reg_write = {"sb", "sh", "sw", "sd", "swl", "swr", "sdl", "sdr",
                            "swc1", "sdc1", "beq", "bne", "beqz", "bnez", "blez",
                            "bgtz", "bltz", "bgez", "j", "jr", "nop", "mtc0",
                            "mtc1", "break", "sync", "cache", "teq", "tne"}
            if (parts and parts[0] in lui and insn.mnemonic not in no_reg_write
                    and not (insn.mnemonic == "addiu" and base == parts[0] == parts[1])):
                lui.pop(parts[0], None)
        print(f"0x{insn.address:08X}  {raw:08X}  {insn.mnemonic:<8} {insn.op_str}{note}")


if __name__ == "__main__":
    main()
