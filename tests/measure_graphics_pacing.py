"""Capture Windows D3D12 display timing and guest progress in a portable profile."""

import argparse
from collections import Counter
import csv
import ctypes
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import threading
import time
import uuid


LIVE_RACE = "scene=5 req=5 phase=3 race_mode=4"
SWAP = re.compile(r"fb swap #(\d+) -> VI_ORIGIN=0x[0-9a-f]+ \(vi=(\d+)\)")


def query_qpc(function):
    value = ctypes.c_longlong()
    getattr(ctypes.windll.kernel32, function)(ctypes.byref(value))
    return value.value


def distribution(values):
    if not values:
        return {"samples": 0, "p50_ms": None, "p95_ms": None, "p99_ms": None, "max_ms": None}
    values.sort()
    return {
        "samples": len(values),
        "p50_ms": round(statistics.median(values), 3),
        "p95_ms": round(values[math.ceil(len(values) * 0.95) - 1], 3),
        "p99_ms": round(values[math.ceil(len(values) * 0.99) - 1], 3),
        "max_ms": round(values[-1], 3),
    }


def summarize(rows, frequency, start, end):
    presents = []
    displayed = []
    dropped = 0
    tearing = 0
    modes = Counter()
    for row in rows:
        present = int(row["QPCTime"]) / frequency
        present_ms = float(row["msBetweenPresents"])
        if start <= present < end:
            dropped += row["Dropped"] == "1"
            tearing += row["AllowsTearing"] == "1"
            modes[row["PresentMode"]] += 1
            if present_ms > 0 and present - present_ms / 1000 >= start:
                presents.append(present_ms)
        if row["Dropped"] == "0":
            display = present + float(row["msUntilDisplayed"]) / 1000
            display_ms = float(row["msBetweenDisplayChange"])
            if start <= display < end and display_ms > 0 and display - display_ms / 1000 >= start:
                displayed.append(display_ms)
    return {
        "duration_s": round(end - start, 3),
        "present_intervals": distribution(presents),
        "display_intervals": distribution(displayed),
        "dropped_presents": dropped,
        "allows_tearing_presents": tearing,
        "present_modes": dict(modes),
    }


def capture(args, root, frequency):
    env = {key: value for key, value in os.environ.items() if not key.startswith("AERO_")}
    env.update(AERO_HEADLESS="0", AERO_HARNESS_LOG="1", AERO_INPUT_PULSE="1000:120:12:20:3",
               AERO_WARP=f"{args.track}:{args.craft}", AERO_MODERN_MAX_VIS=str(args.max_vis))
    trace_seconds = math.ceil(args.max_vis / 60) + 17
    # Use a unique ETW session and retain the PID so any other AeroGauge instance
    # recorded by the process-name filter can be excluded later.
    cmd = [
        str(args.presentmon), "--process_name", args.exe.name, "--session_name",
        f"AeroGauge-{uuid.uuid4().hex}", "--v1_metrics", "--qpc_time",
        "--no_console_stats", "--no_track_input",
        "--timed", str(trace_seconds), "--terminate_after_timed", "--terminate_on_proc_exit",
        "--output_file", str(root / "presents.csv"),
    ]
    with (root / "presentmon.log").open("wb") as trace_log, (
        root / "game.log"
    ).open("w", encoding="utf-8") as game_log, (root / "game-stdout.log").open("wb") as stdout:
        trace = subprocess.Popen(cmd, stdout=trace_log, stderr=subprocess.STDOUT,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            time.sleep(2)
            if trace.poll() is not None:
                raise ValueError("PresentMon stopped before launch. See presentmon.log.")
            game = subprocess.Popen([str(args.exe), str(args.rom)], cwd=root, env=env,
                                    stdout=stdout, stderr=subprocess.PIPE)
            try:
                # Bound the blocking log read if the game stalls before closing stderr.
                watchdog = threading.Timer(trace_seconds + 5, game.kill)
                watchdog.start()
                try:
                    records = []
                    for raw_line in game.stderr:
                        stamp = query_qpc("QueryPerformanceCounter") / frequency
                        line = raw_line.decode("utf-8", errors="replace").rstrip()
                        records.append((stamp, line))
                        game_log.write(f"{stamp:.6f} {line}\n")
                    if game.wait() != 0:
                        raise ValueError("Game did not exit normally. See game.log.")
                finally:
                    watchdog.cancel()
            finally:
                if game.poll() is None:
                    game.kill()
                    game.wait()
                game.stderr.close()
            if trace.wait(timeout=trace_seconds) != 0:
                raise ValueError("PresentMon capture failed. See presentmon.log.")
        finally:
            if trace.poll() is None:
                trace.kill()
                trace.wait()
    return game.pid, records


def summarize_run(root, pid, records, frequency):
    with (root / "presents.csv").open(newline="", encoding="utf-8-sig") as file:
        rows = [row for row in csv.DictReader(file)
                if int(row["ProcessID"]) == pid and row["Runtime"] == "DXGI"]
    if not rows:
        raise ValueError("No DXGI presents from the game PID. See presentmon.log.")
    chains = Counter(row["SwapChainAddress"] for row in rows if row["Dropped"] == "0")
    if not chains:
        chains = Counter(row["SwapChainAddress"] for row in rows)
    chain = chains.most_common(1)[0][0]
    rows = sorted((row for row in rows if row["SwapChainAddress"] == chain),
                  key=lambda row: int(row["QPCTime"]))
    first = int(rows[0]["QPCTime"]) / frequency
    last = int(rows[-1]["QPCTime"]) / frequency

    swaps = []
    live_race = False
    for stamp, line in records:
        if "[state]" in line:
            live_race = LIVE_RACE in line
            if not live_race:
                swaps.clear()
        if live_race and (match := SWAP.search(line)):
            if first + 15 <= stamp <= last:
                swaps.append((stamp, *map(int, match.groups())))
    log = "\n".join(line for _, line in records)
    if (len(swaps) < 2 or "game-mode 0 race" not in log or "first send_dl" not in log
            or "RT64 renderer initialised (api=1)" not in log):
        raise ValueError("Capture did not contain a stable D3D12 race. See game.log.")
    start, start_swap, start_vi = swaps[0]
    end, end_swap, end_vi = swaps[-1]
    return {
        "process_id": pid,
        "swapchain": chain,
        "cold_first_10_s": summarize(rows, frequency, first, first + 10),
        "warm_live_race": {
            **summarize(rows, frequency, start, end),
            "guest_vi_range": [start_vi, end_vi],
            "guest_swap_range": [start_swap, end_swap],
            "guest_vi_per_s": round((end_vi - start_vi) / (end - start), 2),
            "guest_swaps_per_s": round((end_swap - start_swap) / (end - start), 2),
            "guest_swaps_per_vi": round((end_swap - start_swap) / (end_vi - start_vi), 4),
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("presentmon", "exe", "rom", "settings", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--track", choices=range(1, 7), type=int, default=1)
    parser.add_argument("--craft", choices=range(1, 11), type=int, default=1)
    parser.add_argument("--max-vis", type=int, default=2580, metavar="VI",
                        help="guest VI limit, from 2580 to 5400 (default: 2580)")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This capture uses Windows PresentMon and D3D12")
    if not 2580 <= args.max_vis <= 5400:
        parser.error("--max-vis must be from 2580 to 5400")
    root = args.output.resolve()
    try:
        for name in ("presentmon", "exe", "rom", "settings"):
            setattr(args, name, getattr(args, name).resolve(strict=True))
        root.mkdir(parents=True)
        (root / "portable.txt").touch()
        shutil.copyfile(args.settings, root / "graphics.json")
        frequency = query_qpc("QueryPerformanceFrequency")
        pid, records = capture(args, root, frequency)
        result = summarize_run(root, pid, records, frequency)
        result.update(track=args.track, craft=args.craft, max_vis=args.max_vis)
        output = json.dumps(result, indent=2) + "\n"
        (root / "summary.json").write_text(output, encoding="utf-8")
        print(output, end="")
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"{error}\nCapture directory: {root}\n")


if __name__ == "__main__":
    main()
