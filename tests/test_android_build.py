"""Android dry-run versions must remain valid Gradle inputs."""
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DryRunVersionTests(unittest.TestCase):
    def test_workflow_dry_run_has_positive_android_version_code(self):
        workflow = (ROOT / ".github/workflows/build-release.yml").read_text(encoding="utf-8")
        # Read the workflow input to Gradle; the Android SDK is not needed here.
        match = re.search(r"format\('([^']*-dryrun-\{0\})', github\.sha\)", workflow)
        self.assertIsNotNone(match, "Android dry-run version must identify the dispatched commit")
        version = match.group(1).format("a" * 40)
        parts = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)(?:[-+].*)?", version)
        self.assertIsNotNone(parts, "Dry-run version must satisfy Gradle's semantic version parser")
        major, minor, patch = map(int, parts.groups())
        # Mirrors android/app/build.gradle; Android requires a positive code.
        version_code = major * 1_000_000 + minor * 1_000 + patch
        self.assertGreater(version_code, 0, f"Invalid Android versionCode for {version}")


if __name__ == "__main__":
    unittest.main()
