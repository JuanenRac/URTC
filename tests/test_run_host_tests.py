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


class CoverageTests(unittest.TestCase):
    GCOV = (
        "File '" + ROOT.as_posix() + "/src/F303-master/a.c'\nLines executed:50.00% of 10\nCreating 'a.c.gcov'\n\n"
        "File '" + ROOT.as_posix() + "/tests/host_hal/fake_hal.c'\nLines executed:28.97% of 107\n\n"
        "File '" + ROOT.as_posix() + "/src/F303-master/a.h'\nLines executed:100.00% of 3\n\n"
        "File '" + ROOT.as_posix() + "/src/F303-master/b.c'\nLines executed:100.00% of 4\n"
    )

    def test_only_the_c_files_of_the_firmware_sources_are_counted(self):
        self.assertEqual(runner.parse_gcov(self.GCOV), {"src/F303-master/a.c": (5, 10), "src/F303-master/b.c": (4, 4)})

    def test_the_best_of_the_suites_counts_for_a_file_and_the_percentage_is_over_all_lines(self):
        merged = runner.merge_coverage([{"src/x.c": (2, 10)}, {"src/x.c": (7, 10), "src/y.c": (5, 5)}])
        self.assertEqual(merged, {"src/x.c": (7, 10), "src/y.c": (5, 5)})
        self.assertAlmostEqual(runner.coverage_percent(merged), 80.0)
        self.assertEqual(runner.coverage_percent({}), 0.0)

    def test_the_sources_listed_for_the_report_are_the_c_files_under_src(self):
        sources = runner.all_firmware_sources()
        self.assertIn("src/F303-master/firmware_blackbox.c", sources)
        self.assertTrue(all(name.startswith("src/") and name.endswith(".c") for name in sources))

    def test_the_build_scripts_and_the_ci_pass_the_floor_to_the_runner(self):
        self.assertIn("--coverage-floor", (ROOT / "build_firmware.sh").read_text(encoding="utf-8"))
        self.assertIn("--coverage-floor", (ROOT / "build_firmware.bat").read_text(encoding="utf-8"))
        self.assertIn("URTC_COVERAGE_FLOOR", (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8"))

    def test_the_sources_the_new_suites_link_are_listed_for_the_analysis(self):
        pairs = {source for _suite, source in runner.analysis_sources(list(runner.SUITES))}
        for name in ("firmware_can_soldering.c", "firmware_control_thermal.c", "firmware_blackbox.c", "firmware_identify.c", "firmware_can_global_post.c",
                     "firmware_can_thermalinspection.c", "firmware_control_sensors.c", "firmware_thermal_stats.c"):
            self.assertIn(f"src/F303-master/{name}", pairs)
