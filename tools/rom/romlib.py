"""Shared helpers for AeroGauge static-ROM analysis tools.

ROM model (see CLAUDE.md / scripts/gen_syms_toml.py): one contiguous CPU .text
section at ROM 0x1000, vram 0x80000400. rom = vram - 0x80000400 + 0x1000.
Function boundaries come from the generated aerogauge.syms.toml (jal-target +
prologue scan) -- regenerate with `python scripts/gen_syms_toml.py` if missing.
"""
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_ROM = REPO / "AeroGauge (USA).z64"
SYMS_TOML = REPO / "aerogauge.syms.toml"

VRAM_BASE = 0x80000400
ROM_TEXT_START = 0x1000
ROM_TEXT_END = 0x7F4C0  # measured code extent; tail is the CP0 exception handler


def vram_to_rom(v):
    return v - VRAM_BASE + ROM_TEXT_START


def rom_to_vram(off):
    return off - ROM_TEXT_START + VRAM_BASE


def load_rom(path=None):
    p = Path(path) if path else DEFAULT_ROM
    if not p.exists():
        sys.exit(f"ROM not found: {p} (supply the AeroGauge (USA) .z64 at the repo root)")
    data = p.read_bytes()
    # .z64 is big-endian; sanity-check the PI header word.
    if data[:4] != b"\x80\x37\x12\x40":
        sys.exit(f"{p} does not look like a big-endian .z64 (header {data[:4].hex()})")
    return data


_FUNC_RE = re.compile(
    r'\{\s*name\s*=\s*"([^"]+)"\s*,\s*vram\s*=\s*(0x[0-9a-fA-F]+)\s*,\s*size\s*=\s*(0x[0-9a-fA-F]+)'
)


def load_functions():
    """Return sorted list of (vram, size, name) from aerogauge.syms.toml."""
    if not SYMS_TOML.exists():
        sys.exit(f"{SYMS_TOML} missing -- run `python scripts/gen_syms_toml.py` first")
    funcs = []
    for m in _FUNC_RE.finditer(SYMS_TOML.read_text()):
        funcs.append((int(m.group(2), 16), int(m.group(3), 16), m.group(1)))
    funcs.sort()
    return funcs


def containing_function(funcs, vram):
    """Binary-search the function whose span contains vram; None if outside."""
    lo, hi = 0, len(funcs) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        fv, fs, fn = funcs[mid]
        if vram < fv:
            hi = mid - 1
        elif vram >= fv + fs:
            lo = mid + 1
        else:
            return funcs[mid]
    return None


def lookup(funcs, name_or_addr):
    """Resolve 'func_80015C8C' or '0x80015C8C' / '80015C8C' to (vram, size, name)."""
    s = name_or_addr.strip()
    by_name = {f[2]: f for f in funcs}
    if s in by_name:
        return by_name[s]
    try:
        addr = int(s, 16)
    except ValueError:
        sys.exit(f"unknown function name and not a hex address: {s}")
    if addr < VRAM_BASE:
        addr |= 0x80000000
    f = containing_function(funcs, addr)
    if f is None:
        sys.exit(f"0x{addr:08X} is outside every known function span "
                 f"(text is 0x{VRAM_BASE:08X}..0x{rom_to_vram(ROM_TEXT_END):08X})")
    return f


def word_at(rom, rom_off):
    return struct.unpack_from(">I", rom, rom_off)[0]
