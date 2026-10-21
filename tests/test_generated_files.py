"""The editor hook must protect each regional generator output."""
import json
import importlib.util
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("check_docs", ROOT / "scripts/check_docs.py")
check_docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_docs)


class GeneratedFileGuardTests(unittest.TestCase):
    def test_both_regions_are_blocked_with_native_and_foreign_path_separators(self):
        for path in (
            "aerogauge.us.toml", "aerogauge.syms.toml",
            "aerogauge.jp.toml", "aerogauge.jp.syms.toml",
            "RecompiledFuncs/func.c", "RecompiledFuncsJP/jp_func.c",
            "src/aspMain.cpp",
        ):
            for absolute in ("/repo/" + path, "C:\\repo\\" + path.replace("/", "\\")):
                with self.subTest(path=absolute):
                    result = subprocess.run(
                        [sys.executable, "-B", str(ROOT / "scripts/hooks/guard_generated_files.py")],
                        input=json.dumps({"tool_input": {"file_path": absolute}}),
                        text=True, capture_output=True,
                    )
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn("BLOCKED", result.stderr)

    def test_generator_inputs_remain_editable(self):
        for path in ("scripts/gen_syms_toml.py", "scripts/japan_rev_a.py", "force_stub.jp.txt"):
            with self.subTest(path=path):
                result = subprocess.run(
                    [sys.executable, "-B", str(ROOT / "scripts/hooks/guard_generated_files.py")],
                    input=json.dumps({"tool_input": {"file_path": path}}),
                    text=True, capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)


class GeneratedConsistencyTests(unittest.TestCase):
    def test_windows_checkout_keeps_generated_toml_line_endings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def git(*args):
                return subprocess.run(["git", "-C", str(root), *args],
                                      check=True, capture_output=True)
            git("init", "-q")
            git("config", "core.autocrlf", "true")
            (root / ".gitattributes").write_bytes((ROOT / ".gitattributes").read_bytes())
            names = ("aerogauge.syms.toml", "aerogauge.us.toml",
                     "aerogauge.jp.syms.toml", "aerogauge.jp.toml")
            for name in names:
                (root / name).write_bytes(b"generated\n")
            git("add", ".")
            for name in names:
                (root / name).unlink()
            git("checkout-index", "--all", "--force")
            for name in names:
                self.assertEqual((root / name).read_bytes(), b"generated\n", name)

    def test_release_checks_run_before_builds_can_overwrite_tracked_outputs(self):
        workflow = (ROOT / ".github/workflows/build-release.yml").read_text(encoding="utf-8")
        checks = [match.start() for match in re.finditer(
            r"- name: Verify both regional generated inputs", workflow)]
        builds = [match.start() for match in re.finditer(r"- name: Build via shared script", workflow)]
        self.assertEqual(len(checks), 2)
        self.assertEqual(len(builds), 2)
        for check, build in zip(checks, builds):
            self.assertLess(check, build, "Japan regeneration during the build would hide stale tracked output")

    def test_required_regions_cannot_silently_skip(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(check_docs, "ROOT", Path(temporary)):
                errors = check_docs.check_generated_syms(required_regions=("us", "jp"))
        self.assertEqual(len(errors), 2)
        self.assertTrue(any("us" in error for error in errors))
        self.assertTrue(any("jp" in error for error in errors))

    def test_each_region_is_compared_and_working_outputs_are_restored(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            # A stand-in generator lets this filesystem contract run without ROM data.
            (root / "scripts/gen_syms_toml.py").write_text(
                "import argparse\nfrom pathlib import Path\n"
                "p=argparse.ArgumentParser()\np.add_argument('--region')\np.add_argument('--rom')\n"
                "a=p.parse_args()\n"
                "names=['aerogauge.syms.toml','aerogauge.us.toml'] if a.region=='us' else "
                "['aerogauge.jp.syms.toml','aerogauge.jp.toml']\n"
                "for name in names: Path(name).write_text('generated')\n", encoding="utf-8")
            for name in ("AeroGauge (USA).z64", "AeroGauge (Japan) (Rev A).n64"):
                (root / name).write_bytes(b"fixture")
            names = ("aerogauge.syms.toml", "aerogauge.us.toml",
                     "aerogauge.jp.syms.toml", "aerogauge.jp.toml")
            for name in names:
                (root / name).write_bytes(b"working copy")
            with patch.object(check_docs, "ROOT", root), patch.dict("os.environ", {"ROM_FILENAME": "", "JAPAN_ROM_FILENAME": ""}):
                errors = check_docs.check_generated_syms(required_regions=("us", "jp"))
            self.assertEqual(len(errors), 4)
            for name in names:
                self.assertTrue(any(name + ":" in error for error in errors), name)
                self.assertEqual((root / name).read_bytes(), b"working copy")


if __name__ == "__main__":
    unittest.main()
