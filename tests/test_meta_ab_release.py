# =============================================================================
# URTC - tests/test_meta_ab_release.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The official build of the main bootloader keeps its metadata in two alternating pages; the bench build keeps the log of one page.

The choice is made by build_firmware.sh and build_firmware.bat (URTC_RELEASE=1 turns the option on, URTC_BL_META_AB=0 or 1 overrides it). Nothing may let a release
reach the field with the old layout, so these tests hold the pieces together: the shell logic itself (run with the shell when there is one), the batch file, the
marker that the linker keeps and the build of the release candidate looks for, and the room the second page needs.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SH = (ROOT / "build_firmware.sh").read_text(encoding="utf-8").replace("\r\n", "\n")
BAT = (ROOT / "build_firmware.bat").read_text(encoding="utf-8").replace("\r\n", "\n")
FLASH_C = (ROOT / "src" / "F303-master" / "boot" / "bootloader_flash.c").read_text(encoding="utf-8")
LINKER = (ROOT / "src" / "F303-master" / "boot" / "STM32F303CCTx_BOOTLOADER.ld").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github" / "workflows" / "firmware-rc.yml").read_text(encoding="utf-8")
MARKER = "URTC-META-AB-V1"


def shell_choice(environment: dict[str, str]) -> tuple[int, str]:
    """Runs the lines of build_firmware.sh that choose the layout and returns the exit code and the flags that the bootloader is compiled with."""
    start = SH.index("META_AB=0\n")
    end = SH.index('MASTER_KEY_FLAGS="$MASTER_KEY_FLAGS -DURTC_BL_META_AB=$META_AB"\n') + len('MASTER_KEY_FLAGS="$MASTER_KEY_FLAGS -DURTC_BL_META_AB=$META_AB"\n')
    script = 'MASTER_KEY_FLAGS="-DKEY"\n' + SH[start:end] + 'echo "$MASTER_KEY_FLAGS"\n'
    bash = shutil.which("bash")
    if bash is None:
        raise unittest.SkipTest("no bash on this machine")
    run = subprocess.run([bash, "-c", script], capture_output=True, text=True, env={k: v for k, v in os.environ.items() if not k.startswith("URTC_")} | environment, check=False)
    return run.returncode, run.stdout.strip()


class BuildScripts(unittest.TestCase):
    def test_the_official_build_turns_the_two_pages_on(self) -> None:
        self.assertEqual(shell_choice({"URTC_RELEASE": "1"}), (0, "-DKEY -DURTC_BL_META_AB=1"))

    def test_the_bench_build_keeps_the_log_of_one_page(self) -> None:
        self.assertEqual(shell_choice({}), (0, "-DKEY -DURTC_BL_META_AB=0"))
        self.assertEqual(shell_choice({"URTC_RELEASE": "0"}), (0, "-DKEY -DURTC_BL_META_AB=0"))

    def test_the_environment_overrides_the_choice_either_way(self) -> None:
        self.assertEqual(shell_choice({"URTC_RELEASE": "1", "URTC_BL_META_AB": "0"}), (0, "-DKEY -DURTC_BL_META_AB=0"))
        self.assertEqual(shell_choice({"URTC_BL_META_AB": "1"}), (0, "-DKEY -DURTC_BL_META_AB=1"))

    def test_any_other_value_stops_the_build(self) -> None:
        code, _ = shell_choice({"URTC_BL_META_AB": "2"})
        self.assertNotEqual(code, 0)

    def test_the_batch_file_makes_the_same_choice(self) -> None:
        self.assertIn('set "META_AB=0"\nif "%URTC_RELEASE%"=="1" set "META_AB=1"\nif defined URTC_BL_META_AB set "META_AB=%URTC_BL_META_AB%"\n', BAT)
        self.assertIn('set "MASTER_KEY_FLAGS=%MASTER_KEY_FLAGS% -DURTC_BL_META_AB=%META_AB%"', BAT)
        self.assertIn('if not "%META_AB%"=="0" if not "%META_AB%"=="1" (', BAT)

    def test_only_the_main_bootloader_takes_the_flag(self) -> None:
        # MASTER_KEY_FLAGS is used by the compile of the main bootloader and by nothing else
        self.assertEqual(len(re.findall(r"\$MASTER_KEY_FLAGS\b|\"\$MASTER_KEY_FLAGS\"", SH)), 2)  # the flag line and the compile of the bootloader
        self.assertEqual(len(re.findall(r"!MASTER_KEY_FLAGS!", BAT)), 1)


class Image(unittest.TestCase):
    def test_the_two_page_code_carries_a_marker_that_the_linker_keeps(self) -> None:
        self.assertIn(f'const char urtc_bl_meta_layout[16] = "{MARKER}"', FLASH_C)
        self.assertEqual(len(MARKER) + 1, 16)
        self.assertIn("KEEP(*(.urtc_layout))", LINKER)
        self.assertIn('section(".urtc_layout")', FLASH_C)
        # the marker is in the part of the file that only the two-page build compiles
        two_pages = FLASH_C[FLASH_C.index("#if URTC_BL_META_AB\n// ---- metadata in two alternating pages"):]
        self.assertIn("urtc_bl_meta_layout", two_pages)

    def test_the_release_candidate_refuses_a_bootloader_without_it(self) -> None:
        self.assertIn(f'grep -a -q "{MARKER}" build/firmware/URTC_MAIN_BOOTLOADER_v*.bin', WORKFLOW)

    def test_the_option_is_off_in_the_source_unless_a_build_turns_it_on(self) -> None:
        self.assertRegex(FLASH_C, r"#ifndef URTC_BL_META_AB\n#define URTC_BL_META_AB 0\n#endif")

    def test_the_second_page_is_inside_the_region_the_linker_script_leaves_free(self) -> None:
        header = (ROOT / "src" / "common" / "urtc_flash_layout.h").read_text(encoding="utf-8")
        second = int(re.search(r"#define URTC_MASTER_METADATA_B_ADDR\s+0x([0-9A-Fa-f]+)UL", header).group(1), 16)
        kilobytes = int(re.search(r"LENGTH = (\d+)K", LINKER).group(1))
        self.assertEqual(0x08000000 + kilobytes * 1024, second)


if __name__ == "__main__":
    unittest.main()
