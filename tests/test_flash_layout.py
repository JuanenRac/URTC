# =============================================================================
# URTC - tests/test_flash_layout.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The addresses of the flash layout are written once, in src/common/urtc_flash_layout.h; the linker scripts cannot include a header, so this holds them to it.

A wrong origin in a linker script would build an image that the bootloader accepts and then jumps into the wrong place, so the numbers of the four scripts are read
here and compared with the header, and the C files that used to spell the address out are checked to take it from the header.
"""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
HEADER = SRC / "common" / "urtc_flash_layout.h"


def layout() -> dict[str, int]:
    values: dict[str, int] = {}
    for name, expression in re.findall(r"#define[ \t]+(URTC_[A-Z_]+)[ \t]+(.+)", HEADER.read_text(encoding="utf-8")):
        expression = expression.split("//")[0].strip().replace("UL", "")
        values[name] = eval(expression, {"__builtins__": {}})   # noqa: S307 - only digits, parentheses and * from our own header
    return values


def origin_and_length(script: str) -> tuple[int, int]:
    text = (SRC / script).read_text(encoding="utf-8")
    match = re.search(r"FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+),\s*LENGTH\s*=\s*(\d+)K", text)
    assert match, script
    return int(match.group(1), 16), int(match.group(2)) * 1024


class FlashLayoutTests(unittest.TestCase):
    def test_the_header_is_self_consistent(self) -> None:
        v = layout()
        self.assertEqual(v["URTC_MASTER_MAIN_APP_ADDR"] + v["URTC_MASTER_APP_MAX_SIZE"], 0x08024000)
        self.assertEqual(v["URTC_MASTER_BACKUP_APP_ADDR"], v["URTC_MASTER_MAIN_APP_ADDR"] + 0x1C000)
        self.assertEqual(v["URTC_SLAVE_BACKUP_APP_ADDR"], v["URTC_SLAVE_MAIN_APP_ADDR"] + v["URTC_SLAVE_APP_MAX_SIZE"])
        self.assertEqual(v["URTC_SLAVE_MAIN_APP_ADDR"], 0x08000000 + v["URTC_SLAVE_BOOTLOADER_SIZE"] + 0x800)
        for name, value in v.items():
            if name.endswith("_ADDR"):
                self.assertEqual(value % 0x800, 0, f"{name} is not on a 2 KB page")

    def test_the_linker_scripts_start_where_the_header_says(self) -> None:
        v = layout()
        self.assertEqual(origin_and_length("F303-master/STM32F303CCTx_APP.ld"), (v["URTC_MASTER_MAIN_APP_ADDR"], v["URTC_MASTER_APP_MAX_SIZE"]))
        self.assertEqual(origin_and_length("F303-slave/STM32F303CBTx_SLAVEAPP.ld"), (v["URTC_SLAVE_MAIN_APP_ADDR"], v["URTC_SLAVE_APP_MAX_SIZE"]))
        self.assertEqual(origin_and_length("F303-slave/boot/STM32F303CBTx_SLAVEBOOT.ld"), (0x08000000, v["URTC_SLAVE_BOOTLOADER_SIZE"]))
        origin, length = origin_and_length("F303-master/boot/STM32F303CCTx_BOOTLOADER.ld")
        self.assertEqual(origin, 0x08000000)
        self.assertEqual(origin + length, v["URTC_MASTER_METADATA_ADDR"] + 0x0, "the bootloader of the master ends at the metadata page")

    def test_the_c_files_take_the_numbers_from_the_header(self) -> None:
        for name in ("F303-master/boot/bootloader_common.h", "F303-slave/boot/slaveboot_common.h"):
            text = (SRC / name).read_text(encoding="utf-8")
            self.assertIn('#include "urtc_flash_layout.h"', text, name)
            self.assertIsNone(re.search(r"#define\s+(MAIN_APP_ADDR|BACKUP_APP_ADDR|METADATA_ADDR)\s+0x", text), name)
        main = (SRC / "F303-master" / "STM32F303CC_main.c").read_text(encoding="utf-8")
        self.assertIn("SCB->VTOR = URTC_MASTER_MAIN_APP_ADDR;", main)
        self.assertNotIn("0x08008000", re.sub(r"//.*", "", main))

    def test_every_other_place_that_spells_the_application_address_agrees(self) -> None:
        """The manifest generator, the documents and the readme of the firmware spell the address out; each one is held to the header too."""
        address = f"0x{layout()['URTC_MASTER_MAIN_APP_ADDR']:08X}"
        generator = (ROOT / "generate_manifest.py").read_text(encoding="utf-8")
        self.assertIn(f'"flash_address": "{address}"', generator)
        for name in ("docs/SOURCE_MAP.md", "src/F303-master/README.md", "src/F303-master/boot/README.md", "README.md"):
            text = (ROOT / name).read_text(encoding="utf-8")
            spelled = set(re.findall(r"0x0800[0-9A-Fa-f]{4}", text))
            self.assertIn(address, spelled, name)
        linker = (SRC / "F303-master" / "STM32F303CCTx_APP.ld").read_text(encoding="utf-8")
        self.assertIn(address, linker)
        bootloader_linker = (SRC / "F303-master" / "boot" / "STM32F303CCTx_BOOTLOADER.ld").read_text(encoding="utf-8")
        self.assertIn(address, bootloader_linker, "the bootloader script names the application address in its comment")


if __name__ == "__main__":
    unittest.main()
