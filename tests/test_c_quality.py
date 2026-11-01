# =============================================================================
# URTC - tests/test_c_quality.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The checks of the quality of the C sources and the generator of the shared data of the two build scripts."""
from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import c_quality  # noqa: E402
import gen_build_scripts  # noqa: E402


class CommentBlocks(unittest.TestCase):
    def test_runs_of_line_comments_and_block_comments_are_counted(self) -> None:
        text = "int a;\n// one\n// two\n// three\nint b;\n/* start\n * middle\n */\nint c;\n"
        self.assertEqual(c_quality.comment_blocks(text), [(2, 3), (6, 3)])

    def test_a_comment_after_code_does_not_start_a_block(self) -> None:
        self.assertEqual(c_quality.comment_blocks("int a; // trailing\nint b;\n"), [])

    def test_a_block_at_the_end_of_the_file_is_closed(self) -> None:
        self.assertEqual(c_quality.comment_blocks("int a;\n// x\n// y"), [(2, 2)])


class Configuration(unittest.TestCase):
    def setUp(self) -> None:
        self.config = c_quality.load_config()

    def test_every_suppression_says_why(self) -> None:
        self.assertEqual(c_quality.check_suppressions_have_reasons(self.config), [])
        broken = {"cppcheck": {"suppress": [{"id": "x", "file": "y.c", "reason": " "}]}}
        self.assertEqual(c_quality.check_suppressions_have_reasons(broken), ["x:y.c"])

    def test_every_excluded_folder_has_a_reason_and_exists(self) -> None:
        for entry in self.config["conversion"]["exclude"]:
            self.assertTrue(entry["reason"].strip(), entry)
            self.assertTrue((ROOT / entry["path"]).is_dir(), entry["path"])

    def test_the_cppcheck_command_has_the_suppressions_the_includes_and_the_folders(self) -> None:
        cmd = c_quality.cppcheck_command(self.config, "cppcheck")
        self.assertEqual(cmd[0], "cppcheck")
        self.assertIn("--error-exitcode=1", cmd)
        self.assertTrue(any(item.startswith("--suppress=oppositeInnerCondition:") for item in cmd))
        self.assertIn("-Isrc/common", cmd)
        self.assertEqual(cmd[-3:], ["src/F303-master", "src/F303-slave", "src/common"])

    def test_exclusion_matches_whole_folders_only(self) -> None:
        self.assertTrue(c_quality.excluded("src/a/b.c", ["src/a/"]))
        self.assertFalse(c_quality.excluded("src/ab/b.c", ["src/a"]))

    def test_the_comment_budget_holds_today(self) -> None:
        blocks = c_quality.long_comment_blocks(self.config)
        self.assertLessEqual(len(blocks), int(self.config["comments"]["max_long_blocks"]))


class BuildScripts(unittest.TestCase):
    def test_both_scripts_carry_the_plan(self) -> None:
        plan = gen_build_scripts.load_plan()
        for kind, path in gen_build_scripts.SCRIPTS.items():
            text = path.read_bytes().decode("utf-8")
            self.assertEqual(gen_build_scripts.rewrite(text, kind, plan), text, path.name)

    def test_the_plan_lists_the_21_hal_modules_once_each(self) -> None:
        modules = gen_build_scripts.load_plan()["hal_modules"]
        self.assertEqual(len(modules), 21)
        self.assertEqual(len(set(modules)), 21)

    def test_paths_are_written_in_the_style_of_each_shell(self) -> None:
        self.assertEqual(gen_build_scripts._path("{BUILD}/common/x", "sh"), "$BUILD/common/x")
        self.assertEqual(gen_build_scripts._path("{BUILD}/common/x", "bat"), "%BUILD%\\common\\x")

    def test_a_script_without_markers_is_refused(self) -> None:
        plan = gen_build_scripts.load_plan()
        with self.assertRaises(SystemExit):
            gen_build_scripts.rewrite("echo no markers\n", "sh", plan)

    def test_the_newline_style_of_a_script_is_kept(self) -> None:
        plan = gen_build_scripts.load_plan()
        text = "REM BUILD_PLAN_BEGIN pins x\r\nold\r\nREM BUILD_PLAN_END pins\r\nREM BUILD_PLAN_BEGIN flags x\r\nold\r\nREM BUILD_PLAN_END flags\r\n"
        out = gen_build_scripts.rewrite(text, "bat", plan)
        self.assertNotIn("\n", out.replace("\r\n", ""))
        self.assertIn(f'set "HAL_COMMIT={plan["pins"]["HAL_COMMIT"]}"', out)

    def test_the_plan_is_valid_json_with_the_expected_keys(self) -> None:
        plan = json.loads((ROOT / "tools" / "build_plan.json").read_text(encoding="utf-8"))
        self.assertEqual(set(plan), {"about", "pins", "flags", "hal_modules"})


if __name__ == "__main__":
    unittest.main()
