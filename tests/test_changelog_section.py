"""Tests for tools/changelog_section.py (the notes of a release come from the CHANGELOG)."""
import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("changelog_section", ROOT / "tools" / "changelog_section.py")
cs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cs)

SAMPLE = """# Changelog

## [Unreleased]

- not released yet

## [0.3.9] - Something new

- first
- second

  continued

## [0.3.8] - Before

- older

## [0.3.7]
"""


class SectionTests(unittest.TestCase):
    def test_the_body_of_a_version_without_its_heading(self):
        self.assertEqual(cs.section(SAMPLE, "0.3.9"), "- first\n- second\n\n  continued\n")

    def test_with_the_heading(self):
        text = cs.section(SAMPLE, "0.3.8", with_heading=True)
        self.assertTrue(text.startswith("## [0.3.8] - Before\n\n- older"))

    def test_the_unreleased_section_and_the_next_version_are_not_mixed_in(self):
        body = cs.section(SAMPLE, "0.3.9")
        self.assertNotIn("not released yet", body)
        self.assertNotIn("older", body)

    def test_a_version_that_is_not_there_or_has_an_empty_section_gives_nothing(self):
        self.assertIsNone(cs.section(SAMPLE, "9.9.9"))
        self.assertIsNone(cs.section(SAMPLE, "0.3.7"))
        self.assertIsNone(cs.section(SAMPLE, "0.3"))

    def test_a_prefix_of_another_version_is_not_a_match(self):
        self.assertIsNone(cs.section("## [0.3.10] - x\n\n- y\n", "0.3.1"))

    def test_windows_line_endings(self):
        self.assertEqual(cs.section(SAMPLE.replace("\n", "\r\n"), "0.3.8"), "- older\n")

    def test_four_part_versions(self):
        self.assertEqual(cs.section("## [0.8.0.1] - x\n\n- y\n", "0.8.0.1"), "- y\n")


class CommandTests(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(ROOT / "tools" / "changelog_section.py"), *args], check=False, capture_output=True, text=True)

    def test_the_real_changelog_has_a_section_for_the_released_version(self):
        result = self.run_tool("0.3.8")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.strip())

    def test_a_missing_version_fails_with_a_message(self):
        result = self.run_tool("99.99.99")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no section", result.stderr)

    def test_bad_arguments_fail(self):
        self.assertEqual(self.run_tool().returncode, 2)
        self.assertEqual(self.run_tool("latest").returncode, 2)


if __name__ == "__main__":
    unittest.main()
