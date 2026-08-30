#!/usr/bin/env python3
"""Attribute a display-list address to its course row + zone + section entry.

AeroGauge's course table (see src/aero_full_track.cpp header comment and
docs/notes/rom-map.md "Course zone/visibility model"):
    row = 0x8008B290 + 0x14 * track
    row[0]    section -> zone byte map
    row[1]    zone visibility rows (3 bytes per zone, hand-authored PVS)
    row[2]    zone -> object-list table (also reachable as *(0x8013FF44))
    row[+0x10] zone -> section-DL group table

Section groups are 8-byte entries {dl, hw4, hw6}. This tool walks every
track's section-DL table and reports the (track, zone, section_index,
hw4, hw6) for the requested DL pointer.

**The section-DL tables live in BSS at runtime, not in ROM .text** -- the
course blob is loaded at boot into a region the ROM doesn't directly
mirror (file offsets around 0x35.... in the 8 MiB image). So this tool
operates on the live RDRAM dump produced by AERO_RACE_DL_DUMP, not on the
ROM file directly. The course row table itself IS in ROM and is used to
walk every track; the section tables themselves come from RDRAM.

Why a tool: a track-artefact investigation (see the
track-artefact-diagnosis skill) attributes a stale-bank or skipped DL to
its zone and section in seconds instead of minutes. The display-list
address is the stable ROM fact -- section index is corroboration only,
because a table reorder cannot retarget an address-based policy rule.

Usage:
    python tools/rom/full_track_attr.py 0x803903B8 --rdram dump.bin
    python tools/rom/full_track_attr.py 0x803903B8 --rdram dump.bin --rom game.z64
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from romlib import word_at

COURSE_ROWS = 0x8008B290
COURSE_ROW_STRIDE = 0x14
MAX_TRACK = 16           # user-facing tracks 0..5; the row table is wider
                         # (gen_syms_toml.py derives it), but rows 6..15 hold
                         # invalid pointers that vptr() rejects. 16 covers all
                         # real ones with a small safety margin.
MAX_SECTION_ENTRIES = 512
HW4_SHELL_MASK = 0x10    # hw4 bit the registrar maps to render-flag 0x100 --
                         # general enclosed-shell marker.

RDRAM_SIZE = 0x800000
PHYS_BASE = 0x80000000   # KSEG0 vram -> phys = vram - 0x80000000


def kseg0_to_phys(v):
    return v - PHYS_BASE


def vptr(v):
    """Plausible KSEG0 pointer below the reserved full-track scratch region."""
    return (v & 0xFF000003) == 0x80000000 and (v & 0x00FFFFFF) < 0x00700000


def row_pointers(rdram, track):
    """Course row pointers -- the row table itself is in ROM .text and is
    therefore byte-identical to the loaded RDRAM view at boot."""
    row = COURSE_ROWS + track * COURSE_ROW_STRIDE
    base = kseg0_to_phys(row)
    return (
        word_at(rdram, base + 0x00),
        word_at(rdram, base + 0x04),
        word_at(rdram, base + 0x08),
        word_at(rdram, base + 0x10),
    )


def zone_count(vis, zoneobj):
    """Same adjacency check as aero_full_track.cpp:172 (zone_count_adjacent)."""
    if zoneobj <= vis:
        return -1
    gap = zoneobj - vis
    if gap % 4 != 0:
        return -1
    return gap // 4


def find_dl_in_track(rdram, target_dl, track):
    """Scan one track's full section-DL tree for the target DL pointer.

    Returns the first (zone, section_index, hw4, hw6) tuple whose dl
    matches, or None. Section tables are walked in RDRAM (live BSS)."""
    _, vis, zoneobj, dlgroups = row_pointers(rdram, track)
    if not vptr(dlgroups):
        return None
    n = zone_count(vis, zoneobj) if vptr(vis) and vptr(zoneobj) else -1
    if n < 1:
        return None
    for z in range(n):
        g = word_at(rdram, kseg0_to_phys(dlgroups) + z * 4)
        if not vptr(g):
            continue
        g_base = kseg0_to_phys(g)
        for idx in range(MAX_SECTION_ENTRIES):
            e = g_base + idx * 8
            dl = word_at(rdram, e)
            if idx > 0 and dl == 0:
                break
            if dl == target_dl:
                hw4 = struct.unpack_from(">H", rdram, e + 4)[0]
                hw6 = struct.unpack_from(">H", rdram, e + 6)[0]
                return z, idx, hw4, hw6
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dl", help="hex display-list vram address (e.g. 0x803903B8)")
    ap.add_argument("--rdram", required=True,
                    help="8 MiB RDRAM dump from AERO_RACE_DL_DUMP (REQUIRED: "
                         "section tables live in BSS at runtime)")
    args = ap.parse_args()

    target = int(args.dl, 16)
    if target < 0x80000000:
        target |= 0x80000000

    rd = Path(args.rdram).read_bytes()
    if len(rd) < RDRAM_SIZE:
        sys.exit(f"RDRAM dump too small: {len(rd):#x} bytes (need 0x{RDRAM_SIZE:X})")

    track_byte = struct.unpack_from(">b", rd, 0x8013FF9B - PHYS_BASE)[0]

    print(f"# DL attribute: target=0x{target:08X}")
    print(f"# course row table @ 0x{COURSE_ROWS:08X} (stride 0x{COURSE_ROW_STRIDE:X})")
    print(f"# live track byte @ 0x8013FF9B = {track_byte}")

    found = False
    for track in range(MAX_TRACK):
        hit = find_dl_in_track(rd, target, track)
        if hit is None:
            continue
        zone, idx, hw4, hw6 = hit
        marker = ""
        if hw4 & HW4_SHELL_MASK:
            marker = "  # hw4 & 0x10 -> PVS-gated shell rule"
        live = ""
        if track == track_byte:
            live = "  # <-- current track (RDRAM)"
        print(f"track={track:2d} zone={zone:2d} section_index={idx:3d} "
              f"hw4=0x{hw4:04X} hw6=0x{hw6:04X} dl=0x{target:08X}{marker}{live}")
        found = True

    if not found:
        print(f"# no match: DL 0x{target:08X} is not in any track's section-DL table")
        print(f"# (was the dump captured AFTER the course was loaded?)")
        sys.exit(1)


if __name__ == "__main__":
    main()