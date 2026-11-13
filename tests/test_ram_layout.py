# =============================================================================
# URTC - tests/test_ram_layout.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The RAM of the four linker scripts: the same 40 KB of main SRAM at 0x20000000 everywhere, the stack at the top of it and room left for the heap and the stack.

The flash addresses are held to one header by test_flash_layout.py; the RAM is the other half of the memory map. An application and its bootloader run one after the
other in the same RAM, so a script that disagreed with its partner would hand the application a stack pointer that points outside the part. This reads the numbers
of the scripts (the build is not needed) and compares them with each other. Evidence: simulator (text of the scripts). Linking both images and reading the map on a
board, and running a thermal capture there, is still to be done: the size of the SRAM is the one the board data gives, to be confirmed against the part in hand.
"""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SCRIPTS = {
    "master application": "F303-master/STM32F303CCTx_APP.ld",
    "master bootloader": "F303-master/boot/STM32F303CCTx_BOOTLOADER.ld",
    "slave application": "F303-slave/STM32F303CBTx_SLAVEAPP.ld",
    "slave bootloader": "F303-slave/boot/STM32F303CBTx_SLAVEBOOT.ld",
}
SRAM_BASE = 0x20000000
SRAM_BYTES = 40 * 1024
STACK_TOP = 0x2000A000
CCM_BASE = 0x10000000


def text(script: str) -> str:
    return (SRC / script).read_text(encoding="utf-8")


def code(script: str) -> str:
    """The script without its comments (they speak of the CCM RAM and of addresses that are not used)."""
    return re.sub(r"/\*.*?\*/", "", text(script), flags=re.DOTALL)


def ram(script: str) -> tuple[int, int]:
    match = re.search(r"RAM\s*\(xrw\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+),\s*LENGTH\s*=\s*(\d+)K", text(script))
    assert match, script
    return int(match.group(1), 16), int(match.group(2)) * 1024


def number(script: str, name: str) -> int:
    match = re.search(rf"^{name}\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;", text(script), re.MULTILINE)
    assert match, f"{name} in {script}"
    return int(match.group(1), 0)


class RamLayoutTests(unittest.TestCase):
    def test_every_script_has_the_main_sram_and_nothing_else_as_ram(self) -> None:
        for label, script in SCRIPTS.items():
            with self.subTest(label):
                self.assertEqual(ram(script), (SRAM_BASE, SRAM_BYTES))
                regions = re.findall(r"^\s*(\w+)\s*\([rwx]+\)\s*:", text(script), re.MULTILINE)
                self.assertEqual(sorted(regions), ["FLASH", "RAM"], "a region that the other scripts do not know about")

    def test_the_stack_starts_at_the_top_of_the_sram(self) -> None:
        for label, script in SCRIPTS.items():
            with self.subTest(label):
                self.assertRegex(code(script), re.compile(r"^_estack\s*=\s*ORIGIN\(RAM\)\s*\+\s*LENGTH\(RAM\)\s*;", re.MULTILINE), "the stack top is the end of the RAM region")
                origin, length = ram(script)
                self.assertEqual(origin + length, STACK_TOP)

    def test_an_application_and_its_bootloader_agree_on_the_ram(self) -> None:
        self.assertEqual(ram(SCRIPTS["master application"]), ram(SCRIPTS["master bootloader"]))
        self.assertEqual(ram(SCRIPTS["slave application"]), ram(SCRIPTS["slave bootloader"]))
        for label in ("master", "slave"):
            self.assertEqual(number(SCRIPTS[f"{label} application"], "_Min_Stack_Size"), number(SCRIPTS[f"{label} bootloader"], "_Min_Stack_Size"))
            self.assertEqual(number(SCRIPTS[f"{label} application"], "_Min_Heap_Size"), number(SCRIPTS[f"{label} bootloader"], "_Min_Heap_Size"))

    def test_the_reserved_stack_and_heap_are_sensible_and_leave_room_for_the_data(self) -> None:
        for label, script in SCRIPTS.items():
            with self.subTest(label):
                stack, heap = number(script, "_Min_Stack_Size"), number(script, "_Min_Heap_Size")
                self.assertGreaterEqual(stack, 0x400, "less than 1 KB of stack")
                self.assertGreaterEqual(heap, 0x100)
                self.assertLess(stack + heap, SRAM_BYTES // 4, "the reservations take a quarter of the RAM or more")

    def test_the_ccm_is_not_used_by_any_script(self) -> None:
        for label, script in SCRIPTS.items():
            with self.subTest(label):
                self.assertNotIn(f"{CCM_BASE:#x}", code(script).lower(), "a section placed in the CCM RAM would need its own load and init code")
                self.assertNotIn("CCMRAM", code(script))

    def test_the_noinit_zone_of_the_crash_dump_is_in_the_main_ram_of_the_application(self) -> None:
        master = text(SCRIPTS["master application"])
        self.assertRegex(master, r"\.noinit\s*\(NOLOAD\)\s*:", "the dump must survive the reset: it lives in a section that start-up does not clear")
        self.assertRegex(master, r"\}\s*>\s*RAM", "and the section goes to the main RAM")


if __name__ == "__main__":
    unittest.main()
