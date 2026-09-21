"""ROM-backed RT64 regression for the game's osViSetSpecialFeatures call.

Requires a working desktop graphics device. The existing renderer heartbeat
observes the runtime-owned VI registers; the software renderer cannot prove this
contract. Exercise startup and a race transition without changing user settings.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    args = parser.parse_args()
    exe, repo = args.exe.resolve(), args.repo.resolve()
    with tempfile.TemporaryDirectory(prefix="aero-vi-features-") as directory:
        work = Path(directory)
        # Portable storage keeps saves and all configuration in this test's temp
        # directory. Pass the ROM explicitly because cwd is intentionally isolated.
        (work / "portable.txt").touch()
        env = {k: v for k, v in os.environ.items() if not k.startswith("AERO_")}
        env.update(AERO_HEADLESS="0", AERO_HARNESS_LOG="1",
                   AERO_MODERN_MAX_VIS="1800", AERO_WARP_AT="1200:1:1")
        result = subprocess.run([str(exe), str(repo / "AeroGauge (USA).z64")],
                                cwd=work, env=env, capture_output=True,
                                text=True, errors="replace", timeout=90)
        log = result.stderr
        assert result.returncode == 0, log
        assert "[rt64] RT64 renderer initialised" in log, log
        assert "reached 1800 VIs" in log, log
        before, separator, after = log.partition("scene=5 req=5")
        assert separator, "Race transition was not exercised:\n" + log
        for label, section in (("startup", before), ("race", after)):
            statuses = [int(v, 16) for v in re.findall(
                r"\[rt64\] send_dl .*?VI_STATUS=0x([0-9a-fA-F]+)", section)]
            assert len(statuses) >= 2, f"Insufficient {label} samples:\n{log}"
            # ROM startup requests 0x5a: gamma and gamma dither off, divot and
            # dither filter on. From the ROM's 0x311e mode this yields 0x13012.
            assert set(statuses) == {0x13012}, (
                f"{label}: expected VI_STATUS=0x13012; got "
                f"{', '.join(hex(v) for v in sorted(set(statuses)))}\n{log}")
            print(f"PASS {label}: {len(statuses)} RT64 samples, VI_STATUS=0x13012")


if __name__ == "__main__":
    main()
