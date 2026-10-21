"""The shared patch plan must retain the fixes previously missed on a platform."""
from pathlib import Path
import json
import shutil
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import dependency_patches


class PatchManifestTests(unittest.TestCase):
    def test_inventory_rejects_an_unlisted_patch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "patches").mkdir()
            (root / "patches/series.json").write_text("[]", encoding="utf-8")
            (root / "patches/new.patch").write_text("", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unlisted=.*new.patch"):
                dependency_patches.patch_plan(root, "linux")

    def test_runtime_and_focus_fixes_reach_every_platform(self):
        for platform in ("windows", "linux", "android"):
            with self.subTest(platform=platform):
                plan = dependency_patches.patch_plan(ROOT, platform)
                names = [patch.name[:4] for patches in plan.values() for patch in patches]
                self.assertIn("0013", names)  # Steam Deck audio crash, PR 51.
                self.assertIn("0027", names)  # Android focus recovery, PR 74.
                self.assertLess(names.index("0027"), names.index("0028"))

    def test_platform_specific_patches_are_excluded_from_other_hosts(self):
        for platform in ("windows", "linux", "android"):
            plan = dependency_patches.patch_plan(ROOT, platform)
            names = {patch.name[:4] for patches in plan.values() for patch in patches}
            self.assertEqual("0004" in names, platform == "windows")
            self.assertEqual("0005" in names, platform == "windows")
            self.assertEqual("0021" in names, platform == "android")


class PatchSeriesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="aero-android-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-q")
        self.git("config", "core.autocrlf", "false")
        self.file = self.repo / "build.txt"
        self.file.write_text("original\n")
        self.git("add", ".")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "base")
        self.patches = []
        for index, value in enumerate(("desktop\n", "android\n")):
            self.file.write_text(value)
            patch = self.root / f"{index}.patch"
            patch.write_bytes(self.git("diff"))
            self.patches.append(patch)
            self.git("add", ".")
        self.git("reset", "--hard", "HEAD")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], stderr=subprocess.STDOUT)

    def test_clean_then_repeat(self):
        dependency_patches.apply_patch_series(self.repo, self.patches)
        dependency_patches.apply_patch_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "android\n")
        self.assertEqual(self.git("diff", "--cached"), b"")

    def test_crlf_checkout_can_be_patched_and_repeated(self):
        self.file.write_bytes(b"original\r\n")
        dependency_patches.apply_patch_series(self.repo, self.patches)
        dependency_patches.apply_patch_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "android\n")

    def test_applied_prefix_and_unrelated_edit(self):
        self.git("apply", str(self.patches[0]))
        personal = self.repo / "personal.txt"
        personal.write_text("keep me\n")
        dependency_patches.apply_patch_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "android\n")
        self.assertEqual(personal.read_text(), "keep me\n")
        self.assertEqual(self.git("diff", "--cached"), b"")

    def test_conflict_is_reported_without_modifying_index_or_files(self):
        self.file.write_text("my conflicting edit\n")
        self.git("add", ".")
        before = self.git("diff", "--cached")
        with self.assertRaises(RuntimeError):
            dependency_patches.apply_patch_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "my conflicting edit\n")
        self.assertEqual(self.git("diff", "--cached"), before)

    def test_later_dependency_conflict_leaves_earlier_dependencies_untouched(self):
        other = self.root / "other"
        subprocess.run(["git", "clone", "-q", str(self.repo), str(other)], check=True)
        (other / "build.txt").write_text("conflicting personal edit\n")
        patch_dir = self.root / "patches"
        patch_dir.mkdir()
        for name in ("first.patch", "second.patch"):
            shutil.copyfile(self.patches[0], patch_dir / name)
        (patch_dir / "series.json").write_text(json.dumps([
            {"repository": "repo", "patch": "first.patch", "platforms": ["linux"]},
            {"repository": "other", "patch": "second.patch", "platforms": ["linux"]},
        ]), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "conflicts with local changes"):
            dependency_patches.apply_platform_patches(self.root, "linux")
        self.assertEqual(self.file.read_text(), "original\n")
        self.assertEqual((other / "build.txt").read_text(), "conflicting personal edit\n")
        self.assertEqual(self.git("diff", "--cached"), b"")


if __name__ == "__main__":
    unittest.main()
