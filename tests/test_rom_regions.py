"""Run both ROMs in one executable, including byte order and save isolation.

Requires the developer's ROMs and a built executable. All generated saves and
ROM copies live in temporary directories, never in the user's game profile.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def run(exe, rom, region, order=1, course=None):
    with tempfile.TemporaryDirectory(prefix="aero-region-") as directory:
        root = Path(directory)
        (root / "portable.txt").touch()
        data = rom.read_bytes()
        assert data[:4] == bytes.fromhex("80371240"), "Supply normalized .z64 test inputs"
        if order != 1:
            data = bytes(byte for word in zip(*(data[i::order] for i in reversed(range(order))))
                         for byte in word)
        path = root / "input.rom"
        path.write_bytes(data)
        env = {key: value for key, value in os.environ.items() if not key.startswith("AERO_")}
        vis = 1300 if course else 60
        env.update(AERO_HEADLESS="1", AERO_MODERN_MAX_VIS=str(vis))
        if course:
            env.update(AERO_INPUT_PULSE="1000:120:12:20:3", AERO_WARP=f"{course}:1",
                       AERO_FORCE_FULL_LOD="1", AERO_EASY_TURBO="1")
            # Correct v2 shape, wrong region: must reject before loading RAM.
            state = root / "wrong-region.astate"
            state.write_bytes(struct.pack("=8sIIIIQ", b"AEROSTAT", 2, 8 * 1024 * 1024,
                                          5, 0 if region == "jp" else 1, 0))
            env["AERO_STATE_LOAD"] = str(state)
            env["AERO_STATE_LOAD_SCENE"] = "5"
        result = subprocess.run([str(exe), str(path)], cwd=root, env=env,
                                capture_output=True, text=True, errors="replace", timeout=90)
        log = result.stdout + result.stderr
        expected = "Japan Rev A" if region == "jp" else "USA"
        assert result.returncode == 0, log
        assert f"rom_hash matches ({expected})" in log, log
        assert f"reached {vis} VIs" in log, log
        game_id = "aerogauge.jp.rev_a" if region == "jp" else "aerogauge.us"
        assert (root / f"{game_id}.z64").is_file(), list(root.iterdir())
        other_id = "aerogauge.us" if region == "jp" else "aerogauge.jp.rev_a"
        assert not (root / f"{other_id}.z64").exists()
        if course:
            assert f"course track={course - 1}:" in log, log
            assert "phase=3 race_mode=4" in log, log
            assert "first NON-SILENT buffer" in log, log
            assert "version/size/region mismatch" in log, log
        print(f"PASS {region}: byte order {order}, course {course or 'boot'}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--usa", type=Path, required=True)
    parser.add_argument("--japan", type=Path, required=True)
    parser.add_argument("--all-courses", action="store_true")
    args = parser.parse_args()
    jobs = []
    for region, rom in (("us", args.usa.resolve()), ("jp", args.japan.resolve())):
        for order in (2, 4):
            jobs.append((args.exe.resolve(), rom, region, order, None))
        for course in (range(1, 7) if args.all_courses else (1,)):
            jobs.append((args.exe.resolve(), rom, region, 1, course))
    with ThreadPoolExecutor(max_workers=2) as pool:
        for result in pool.map(lambda job: run(*job), jobs):
            pass


if __name__ == "__main__":
    main()
