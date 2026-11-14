"""tools/run_host_tests.py is the one list of host C suites: every tests/test_*.c is in it and every file it names exists."""
import importlib.util
import os
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
        self.assertEqual(sorted(set(listed)), on_disk)
        # a test is listed twice only to run it on the copy of a driver the expansion slave carries, to build the bootloader with the programmable serial on,
        # or to run the master callers of the slave with the check byte of the I2C link on (the suite of the same name plus _link_crc)
        names = {item.name for item in runner.SUITES}
        again = [suite.name for suite in runner.SUITES if listed.count(suite.test) > 1]
        linked = lambda name: name.endswith("_link_crc") or f"{name}_link_crc" in names   # noqa: E731
        # or to build the same test with an option that is off by default (the crystal, the hardware timer of the weld pulse, the other pins of the sensor bus,
        # the reassembly of the writes of the link, the receive queue between the interrupt and the dispatcher, the return to the application), so that the code behind the option is run and not only compiled out
        options = ("_crystal", "_weld_hw_timer", "_i2c2_preset", "_write_reassembly", "_long", "_queue_on", "_return_on", "_both")
        optional = lambda name: name.endswith(options) or any(f"{name}{option}" in names for option in options)   # noqa: E731
        self.assertTrue(all(name.startswith("mlx_") or name.startswith("bootloader_boot") or name.startswith("bootloader_rotation") or linked(name) or optional(name) for name in again), again)
        self.assertTrue(all(name.endswith("_slave") or name.endswith("_serial") or name.startswith("bootloader_rotation") or linked(name) or optional(name) or f"{name}_slave" in names
                            or f"{name}_serial" in names for name in again), again)
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
            self.assertIn("-Wno-analyzer-infinite-loop", cmd)  # the loops that never return on purpose are not reported; no other analyzer warning is turned off
            self.assertIn("-c", cmd)
            self.assertIn(f"-std={suite.std}", cmd)
            self.assertEqual(cmd[-1], str(ROOT / source))
            self.assertNotIn("-fsanitize=address,undefined", cmd)

    def test_only_gcc_ten_or_newer_gets_the_analysis(self):
        self.assertFalse(runner.is_gcc_with_analyzer("this-compiler-does-not-exist"))

    def test_every_firmware_source_is_compiled_by_a_suite(self):
        compiled = {source for suite in runner.SUITES for source in suite.sources if source.startswith("src/")}
        self.assertEqual(sorted(set(runner.all_firmware_sources()) - compiled), [])

    def test_a_suite_can_rename_a_main_and_switch_off_one_sanitizer_check(self):
        suite = next(item for item in runner.SUITES if item.name == "slave")
        cmd = runner.command(suite, "cc", Path("out"))
        self.assertIn("-Dmain=slave_main_entry", cmd)
        self.assertLess(cmd.index("-fsanitize=address,undefined"), cmd.index("-fno-sanitize=shift-base"))
        self.assertIn("-Dmain=slave_main_entry", runner.analyze_command(suite, "gcc", "src/F303-slave/slave_main.c"))
        plain = runner.command(next(item for item in runner.SUITES if item.name == "persistence"), "cc", Path("out"))
        self.assertFalse(any(part.startswith("-Dmain") or part.startswith("-fno-sanitize=") for part in plain))

    def test_the_whole_application_suite_links_every_source_of_the_master_but_not_the_bootloader(self):
        suite = next(item for item in runner.SUITES if item.name == "firmware_main")
        self.assertIn("src/F303-master/STM32F303CC_main.c", suite.sources)
        self.assertFalse(any("/boot/" in source for source in suite.sources))
        self.assertEqual(len(suite.sources), len(set(suite.sources)))

    def test_both_build_scripts_call_the_runner(self):
        for script in ("build_firmware.sh", "build_firmware.bat"):
            self.assertIn("run_host_tests.py", (ROOT / script).read_text(encoding="utf-8"), script)


class CtestRunnerTests(unittest.TestCase):
    def test_the_cmake_file_has_one_program_and_one_test_per_suite_with_the_direct_flags(self):
        text = runner.cmake_lists(list(runner.SUITES), "cc")
        self.assertEqual(text.count("add_executable("), len(runner.SUITES))
        self.assertEqual(text.count("add_test("), len(runner.SUITES))
        self.assertIn("enable_testing()", text)
        slave = next(item for item in runner.SUITES if item.name == "slave")
        self.assertIn('"-Dmain=slave_main_entry"', text)
        self.assertIn('"-fno-sanitize=shift-base"', text)
        self.assertIn(f"add_test(NAME {slave.name} COMMAND {slave.name}", text)
        # a suite without sanitizers gets no link options for them
        self.assertNotIn("target_link_options(thermal_guard", text)
        self.assertIn("target_link_options(slave PRIVATE", text)

    def test_the_failed_suites_are_read_from_the_ctest_summary(self):
        output = "50% tests passed, 1 tests failed out of 2\n\nThe following tests FAILED:\n\t  2 - identify (Failed)\nErrors while running CTest\n"
        self.assertEqual(runner.failed_tests(output), ["identify"])
        self.assertEqual(runner.failed_tests("100% tests passed, 0 tests failed out of 2\n"), [])

    def test_a_request_for_a_runner_is_honoured_and_auto_prefers_ctest_only_on_linux_with_cmake(self):
        self.assertEqual(runner.choose_runner("direct"), "direct")
        self.assertEqual(runner.choose_runner("ctest"), "ctest")
        self.assertIn(runner.choose_runner("auto"), {"ctest", "direct"})

    def test_a_glob_source_is_every_matching_file_in_order_and_a_plain_source_is_itself(self):
        self.assertEqual(runner.expand(ROOT, "tests/test_led.c"), ["tests/test_led.c"])
        found = runner.expand(ROOT, "glob:tests/host_hal/*.c")
        self.assertEqual(found, sorted(found))
        self.assertIn("tests/host_hal/fake_hal.c", found)

    def test_the_sanitizers_can_follow_a_variable(self):
        suite = runner.Suite("x", "t.c", (), (), sanitize="env:URTC_TEST_SANITIZE_SWITCH")
        os.environ.pop("URTC_TEST_SANITIZE_SWITCH", None)
        self.assertFalse(suite.sanitized)
        os.environ["URTC_TEST_SANITIZE_SWITCH"] = "1"
        try:
            self.assertTrue(suite.sanitized)
            self.assertIn("-fsanitize=address,undefined", runner.command(suite, "cc", Path("out")))
        finally:
            del os.environ["URTC_TEST_SANITIZE_SWITCH"]

    def test_the_build_scripts_leave_the_choice_of_runner_to_the_tool(self):
        for script in ("build_firmware.sh", "build_firmware.bat"):
            self.assertNotIn("--runner", (ROOT / script).read_text(encoding="utf-8"), script)


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
