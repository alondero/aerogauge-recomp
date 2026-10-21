"""A green CTest run must not conceal missing host-test registration."""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import check_host_tests


class HostInventoryTests(unittest.TestCase):
    def setUp(self):
        self.names = (ROOT / "tests/host_test_names.txt").read_text(encoding="utf-8").split()

    def check(self, names):
        response = json.dumps({"tests": [{"name": name} for name in names]})
        with patch("check_host_tests.subprocess.check_output", return_value=response):
            check_host_tests.check_inventory(Path("build"))

    def test_expected_suite_passes(self):
        self.check(self.names)

    def test_silent_omission_is_an_error(self):
        with self.assertRaisesRegex(RuntimeError, "missing=.*" + self.names[-1]):
            self.check(self.names[:-1])

    def test_empty_suite_is_an_error(self):
        with self.assertRaisesRegex(RuntimeError, "registration mismatch"):
            self.check([])

    def test_new_tests_require_an_explicit_roster_update(self):
        with self.assertRaisesRegex(RuntimeError, "unexpected=.*unlisted_test"):
            self.check(self.names + ["unlisted_test"])


if __name__ == "__main__":
    unittest.main()
