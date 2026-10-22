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

    def test_the_analysis_covers_each_firmware_source_once_and_never_a_test_or_the_fake_hal(self):
        pairs = runner.analysis_sources(list(runner.SUITES))
        sources = [source for _, source in pairs]
        self.assertEqual(len(sources), len(set(sources)))
        self.assertTrue(sources)
        for source in sources:
            self.assertFalse(source.startswith("tests/"), source)
            self.assertTrue((ROOT / source).is_file(), source)
        self.assertIn("src/F303-master/multinode.c", sources)

    def test_the_analysis_command_compiles_without_linking_and_keeps_the_suite_flags(self):
        for suite, source in runner.analysis_sources(list(runner.SUITES)):
            cmd = runner.analyze_command(suite, "gcc", source)
            self.assertIn("-fanalyzer", cmd)
            self.assertIn("-c", cmd)
            self.assertIn(f"-std={suite.std}", cmd)
            self.assertEqual(cmd[-1], str(ROOT / source))
            self.assertNotIn("-fsanitize=address,undefined", cmd)

    def test_only_gcc_ten_or_newer_gets_the_analysis(self):
        self.assertFalse(runner.is_gcc_with_analyzer("this-compiler-does-not-exist"))

    def test_both_build_scripts_call_the_runner(self):
        for script in ("build_firmware.sh", "build_firmware.bat"):
            self.assertIn("run_host_tests.py", (ROOT / script).read_text(encoding="utf-8"), script)


if __name__ == "__main__":
    unittest.main()
