#!/usr/bin/env python3
"""Compute AERO_FT_ZONE_MASK values for the full-track bisector knob.

AERO_FT_ZONE_MASK=<hex u64> (see src/aero_full_track.cpp:103) restricts the
full-track enhancement to zones whose bit is set -- sections AND objects both
filtered by `zone_enabled()`. Bit z (LSB) enables zone z. Anything beyond
zone 63 is always enabled (the game's zones cap at MAX_ZONES=64).

Why a tool: bisecting "which zone contains the offending geometry?" is the
core attribution step (track-artefact-diagnosis skill Phase 2). Computing
the mask by hand is mechanical: 1<<z with z > 31 needs Python-style big
ints, and ranges are common. The output prints both the hex literal and a
ready-to-use env-var line.

Usage:
    python tools/rom/zone_mask.py --zone 13              # single zone
    python tools/rom/zone_mask.py --zone 13,21,42        # multiple zones
    python tools/rom/zone_mask.py --range 13..15         # inclusive range
    python tools/rom/zone_mask.py --all                  # all zones (0..63)
    python tools/rom/zone_mask.py --zone 0               # edge: zone 0 = bit 0
"""
import argparse
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--zone", help="comma-separated zone ids (e.g. 13 or 13,21,42)")
    g.add_argument("--range", dest="rng", help="inclusive zone range (e.g. 13..15)")
    g.add_argument("--all", action="store_true", help="all zones 0..63 (~0x0 mask)")
    args = ap.parse_args()

    if args.all:
        zones = list(range(64))
    elif args.zone:
        zones = []
        for tok in args.zone.split(","):
            tok = tok.strip()
            if not tok:
                continue
            try:
                z = int(tok)
            except ValueError:
                sys.exit(f"bad zone id: {tok!r} (must be integer 0..63)")
            if z < 0 or z > 63:
                sys.exit(f"zone {z} out of range (must be 0..63)")
            zones.append(z)
    else:
        try:
            a, b = args.rng.split("..")
            a, b = int(a), int(b)
        except ValueError:
            sys.exit(f"--range expects 'A..B' (got {args.rng!r})")
        if a > b:
            a, b = b, a
        if a < 0 or b > 63:
            sys.exit(f"range {a}..{b} out of 0..63")
        zones = list(range(a, b + 1))

    if not zones:
        sys.exit("empty zone set")

    mask = 0
    for z in zones:
        mask |= 1 << z

    # Match src/aero_full_track.cpp:106 (strtoull parses base-16 with 0x prefix).
    lit = f"0x{mask:X}"

    print(f"# zones:    {','.join(str(z) for z in zones)}")
    print(f"# bit count: {bin(mask).count('1')}")
    print(f"# mask:     {lit}")
    # Ready-to-paste shell line.
    print(f"AERO_FT_ZONE_MASK={lit} build/aerogauge_modern")


if __name__ == "__main__":
    main()