"""Determinism tests for the timestamps written by generate_manifest.py."""
import importlib.util
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

_spec = importlib.util.spec_from_file_location("firmware_generate_manifest", ROOT / "generate_manifest.py")
gm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gm)


class BuildTimestampTests(unittest.TestCase):
    def test_source_date_epoch_is_used(self):
        env = {"SOURCE_DATE_EPOCH": "1700000000"}
        self.assertEqual(gm.build_timestamp(ROOT, env), "2023-11-14T22:13:20Z")

    def test_source_date_epoch_wins_over_ci_mode(self):
        env = {"SOURCE_DATE_EPOCH": "0", "HYDRA_UMC_CI": "1"}
        self.assertEqual(gm.build_timestamp(ROOT, env), "1970-01-01T00:00:00Z")

    def test_ci_mode_uses_last_commit_date_and_is_repeatable(self):
        try:
            commit = subprocess.run(
                ["git", "log", "-1", "--format=%ct"], cwd=ROOT,
                capture_output=True, text=True, check=True,
            ).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("not a git checkout")
        env = {"HYDRA_UMC_CI": "1"}
        first = gm.build_timestamp(ROOT, env)
        self.assertEqual(first, gm.build_timestamp(ROOT, env))
        self.assertEqual(
            first, gm.datetime.fromtimestamp(int(commit), gm.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        )

    def test_invalid_epoch_is_rejected(self):
        with self.assertRaises(SystemExit):
            gm.build_timestamp(ROOT, {"SOURCE_DATE_EPOCH": "not-a-number"})

    def test_local_build_uses_current_time_format(self):
        stamp = gm.build_timestamp(ROOT, {})
        self.assertRegex(stamp, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")


if __name__ == "__main__":
    unittest.main()
