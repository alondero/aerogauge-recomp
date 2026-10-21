#!/usr/bin/env python3
"""Run the dependency-free host suite and reject missing or unexpected tests."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check_inventory(build_directory):
    expected = set((ROOT / "tests/host_test_names.txt").read_text(encoding="utf-8").split())
    result = subprocess.check_output(
        ["ctest", "--test-dir", str(build_directory), "--show-only=json-v1"], text=True)
    actual = {test["name"] for test in json.loads(result)["tests"]}
    if not expected or actual != expected:
        raise RuntimeError(f"Host test registration mismatch: missing={sorted(expected - actual)}, "
                           f"unexpected={sorted(actual - expected)}")
    print(f"Verified all {len(expected)} expected host tests are registered.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()
    check_inventory(args.build_directory)
    subprocess.run(["ctest", "--test-dir", str(args.build_directory),
                    "-C", args.config, "--output-on-failure", "--no-tests=error"], check=True)


if __name__ == "__main__":
    main()
