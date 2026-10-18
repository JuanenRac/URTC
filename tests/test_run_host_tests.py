"""tools/run_host_tests.py is the one list of host C suites: every tests/test_*.c is in it and every file it names exists."""
import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("run_host_tests", ROOT / "tools" / "run_host_tests.py")
runner = importlib.util.module_from_spec(_spec)
sys.modules["run_host_tests"] = runner
_spec.loader.exec_module(runner)


class SuiteListTests(unittest.TestCase):
    def test_every_c_suite_of_the_tests_folder_is_listed_once(self):
        listed = sorted(suite.test for suite in runner.SUITES)
        on_disk = sorted(path.relative_to(ROOT).as_posix() for path in (ROOT / "tests").glob("test_*.c"))
        self.assertEqual(listed, on_disk)
        self.assertEqual(len({suite.name for suite in runner.SUITES}), len(runner.SUITES))

    def test_every_source_and_include_folder_exists(self):
        for suite in runner.SUITES:
            for source in (suite.test, *suite.sources):
                self.assertTrue((ROOT / source).is_file(), f"{suite.name}: {source}")
            for include in suite.includes:
                self.assertTrue((ROOT / include).is_dir(), f"{suite.name}: {include}")

    def test_the_command_has_the_sanitizers_only_where_the_suite_asks(self):
        for suite in runner.SUITES:
            cmd = runner.command(suite, "cc", Path("out"))
            self.assertEqual("-fsanitize=address,undefined" in cmd, suite.sanitize, suite.name)
            self.assertIn(f"-std={suite.std}", cmd)

    def test_both_build_scripts_call_the_runner(self):
        for script in ("build_firmware.sh", "build_firmware.bat"):
            self.assertIn("run_host_tests.py", (ROOT / script).read_text(encoding="utf-8"), script)


if __name__ == "__main__":
    unittest.main()
