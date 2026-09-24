#!/usr/bin/env python3
"""Add Japanese Rev A code to a build when its ROM is supplied locally."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recompiler", type=Path)
    parser.add_argument("--rom", type=Path, default=os.environ.get("JAPAN_ROM_FILENAME"))
    args = parser.parse_args()
    rom = args.rom
    if rom is None:
        rom = next((p for extension in ("z64", "n64", "v64")
                    if (p := ROOT / f"AeroGauge (Japan) (Rev A).{extension}").is_file()), None)
    if rom is None:
        if (ROOT / "RecompiledFuncsJP").exists():
            parser.error("Japanese generated code exists; supply its ROM to regenerate it")
        print("Japanese Rev A ROM absent: building USA support only")
        return
    if not rom.is_file():
        parser.error(f"Japanese ROM not found: {rom}")
    subprocess.run([sys.executable, "-B", str(ROOT / "scripts/gen_syms_toml.py"),
                    "--region", "jp", "--rom", str(rom.resolve())], cwd=ROOT, check=True)
    subprocess.run([str(args.recompiler.resolve()), "aerogauge.jp.toml"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
