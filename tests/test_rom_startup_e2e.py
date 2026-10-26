"""Verify real startup validation/cache behavior in isolated portable profiles.

Needs a built executable and the developer's normalized USA ROM. Optionally
pass Japan Rev A for regional precedence/rejection checks. No GPU is needed.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def run(exe, root, path=None, region=None):
    env = {key: value for key, value in os.environ.items() if not key.startswith("AERO_")}
    env.update(AERO_HEADLESS="1", AERO_MODERN_MAX_VIS="60", SDL_AUDIODRIVER="dummy")
    command = [str(exe)] + ([str(path)] if path is not None else [])
    result = subprocess.run(command, cwd=root, env=env, capture_output=True,
                            text=True, errors="replace", timeout=40)
    log = result.stdout + result.stderr
    if region is None:
        assert result.returncode != 0, log
        assert "[rom]" in log and "calling start_game" not in log, log
    else:
        assert result.returncode == 0, log
        assert f"rom_hash matches ({region})" in log and "reached 60 VIs" in log, log
    return log


def check(exe, usa, japan=None):
    usa_data = usa.read_bytes()
    assert len(usa_data) == 8 * 1024 * 1024 and usa_data[:4] == bytes.fromhex("80371240")
    with tempfile.TemporaryDirectory(prefix="aero-rom-startup-") as directory:
        root = Path(directory)
        (root / "portable.txt").touch()
        run(exe, root)  # headless with no import must fail without a dialog
        for size in (0, 4, 64, len(usa_data) + 4):
            path = root / "bad.rom"
            path.write_bytes(bytes(size))
            assert "exactly 8 MiB" in run(exe, root, path)

        for order in (1, 2, 4):
            data = usa_data if order == 1 else bytes(
                byte for word in zip(*(usa_data[i::order] for i in reversed(range(order)))) for byte in word)
            path = root / "arbitrary-name.rom"
            path.write_bytes(data)
            run(exe, root, path, "USA")
            assert (root / "aerogauge.us.z64").read_bytes() == usa_data
            path.unlink()
            cached_time = (root / "aerogauge.us.z64").stat().st_mtime_ns
            run(exe, root, region="USA")  # source was removed; cached import must work
            assert (root / "aerogauge.us.z64").stat().st_mtime_ns == cached_time
            # An explicit bad argument must not start the valid cached USA copy.
            run(exe, root, root / "missing.rom")

        cache = root / "aerogauge.us.z64"
        corrupt = bytearray(usa_data)
        corrupt[-1] ^= 1
        bad_external = root / "bad-external.rom"
        bad_external.write_bytes(corrupt)
        run(exe, root, bad_external)
        assert cache.read_bytes() == usa_data  # rejected input preserves a valid import
        cache.write_bytes(corrupt)
        run(exe, root)  # same header/size, wrong hash
        assert not cache.exists()  # runtime removes an invalid normalized cache
        # Cache destination is unwritable as a file. Validation must not claim
        # success simply because librecomp's select_rom ignores write failures.
        cache.mkdir()
        run(exe, root, usa)
        cache.rmdir()

        if japan:
            jp_data = japan.read_bytes()
            jp_source = root / "japan.rom"
            jp_source.write_bytes(jp_data)
            run(exe, root, jp_source, "Japan Rev A")
            jp_source.unlink()
            run(exe, root, region="Japan Rev A")
            # A local USA ROM wins over cached Japan.
            local_us = root / "AeroGauge (USA).n64"
            local_us.write_bytes(bytes(byte for word in zip(usa_data[3::4], usa_data[2::4],
                                                           usa_data[1::4], usa_data[0::4]) for byte in word))
            run(exe, root, region="USA")
            local_us.unlink()
            # A local Japan ROM wins over a cached USA ROM.
            local_jp = root / "AeroGauge (Japan) (Rev A).v64"
            local_jp.write_bytes(bytes(byte for word in zip(jp_data[1::2], jp_data[0::2]) for byte in word))
            run(exe, root, region="Japan Rev A")
    print("PASS: real startup validation, byte orders, cache reuse, explicit precedence and cache-write failure")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--usa", type=Path, required=True)
    parser.add_argument("--japan", type=Path)
    args = parser.parse_args()
    check(args.exe.resolve(), args.usa.resolve(), args.japan.resolve() if args.japan else None)
