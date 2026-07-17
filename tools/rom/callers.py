#!/usr/bin/env python3
"""List every jal call site of a function (whole-ROM jal-graph scan).

Usage:
    python tools/rom/callers.py func_80015C8C
    python tools/rom/callers.py 0x80015C8C

Prints one line per call site: site vram + containing function. Remember this
only sees DIRECT calls -- jalr dispatch (object handlers, jump tables) is
invisible here; use find_refs.py on the function's address to catch pointer
tables and lui/addiu-constructed callbacks.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from romlib import (load_rom, load_functions, lookup, rom_to_vram,
                    containing_function, word_at, ROM_TEXT_START, ROM_TEXT_END)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("target", help="func_XXXXXXXX name or hex vram address")
    ap.add_argument("--rom", default=None)
    args = ap.parse_args()

    rom = load_rom(args.rom)
    funcs = load_functions()
    fv, fs, fn = lookup(funcs, args.target)
    want = (fv >> 2) & 0x03FFFFFF

    sites = []
    for off in range(ROM_TEXT_START, min(ROM_TEXT_END, len(rom)), 4):
        w = word_at(rom, off)
        if (w >> 26) == 0x03 and (w & 0x03FFFFFF) == want:  # jal
            site = rom_to_vram(off)
            f = containing_function(funcs, site)
            sites.append((site, f[2] if f else "?"))

    print(f"{fn} (0x{fv:08X}): {len(sites)} direct jal site(s)")
    for site, caller in sites:
        print(f"  0x{site:08X}  in {caller}")
    if not sites:
        print("  (none -- likely reached via jalr/pointer table; try find_refs.py)")


if __name__ == "__main__":
    main()
