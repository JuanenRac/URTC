# =============================================================================
# URTC - tests/test_link_crc_wiring.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The check byte of the expansion link is wired the same way everywhere: the master reaches the slave application only through the Crc-aware calls, and the build scripts can switch both sides on.

The registers of the slave application go through ExpansionI2C_App* (the checked calls when URTC_SLAVE_LINK_CRC=1); the registers of the slave bootloader (REG_BOOT_*) have
no check byte and keep the plain calls. A plain call on an application register would silently stop working on a board built with the switch on, so this reads the master sources.
"""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "src" / "F303-master"
PLAIN = re.compile(r"\bExpansionI2C_Slave(?:Write|Read)Register(?:WithParam)?\s*\(")
APP = re.compile(r"\bExpansionI2C_App(?:Write|Read)Register(?:WithParam)?\s*\(")


def code_lines(path: Path) -> list[tuple[int, str]]:
    return [(n, line.split("//")[0]) for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)]


class LinkCrcWiringTests(unittest.TestCase):
    def test_only_the_bootloader_registers_use_the_plain_calls(self) -> None:
        for path in sorted(MASTER.rglob("*.c")):
            if path.name == "firmware_expansion_i2c.c":
                continue
            for number, line in code_lines(path):
                if PLAIN.search(line):
                    self.assertEqual(path.name, "firmware_can_slavebridge.c", f"{path.name}:{number} calls the plain call outside the bootloader bridge")
                    self.assertIn("REG_BOOT_", line, f"{path.name}:{number} uses the plain call on a register that is not a bootloader register")

    def test_the_application_registers_use_the_switchable_calls(self) -> None:
        used = {path.name for path in MASTER.rglob("*.c") for _, line in code_lines(path) if APP.search(line)}
        for name in ("firmware_can_flyingprobe.c", "firmware_can_pastejetting.c", "firmware_can_thermalinspection.c", "firmware_can_global_pre.c", "STM32F303CC_main.c", "firmware_can_slavebridge.c"):
            self.assertIn(name, used)

    def test_the_header_maps_the_switchable_calls_to_both_families(self) -> None:
        text = (MASTER / "firmware_expansion_i2c.h").read_text(encoding="utf-8")
        for call in ("WriteRegister", "ReadRegister", "ReadRegisterWithParam"):
            self.assertRegex(text, rf"#define ExpansionI2C_App{call} ExpansionI2C_Slave{call}Crc")
            self.assertRegex(text, rf"#define ExpansionI2C_App{call} ExpansionI2C_Slave{call}\b(?!Crc)")
        self.assertRegex(text, r"#ifndef URTC_SLAVE_LINK_CRC\s+#define URTC_SLAVE_LINK_CRC 0")

    def test_both_build_scripts_pass_the_switch_to_every_compile(self) -> None:
        sh = (ROOT / "build_firmware.sh").read_text(encoding="utf-8")
        bat = (ROOT / "build_firmware.bat").read_text(encoding="utf-8")
        self.assertIn('CFLAGS="$CFLAGS -DURTC_SLAVE_LINK_CRC=1"', sh)
        self.assertIn('set "CFLAGS=%CFLAGS% -DURTC_SLAVE_LINK_CRC=1"', bat)
        self.assertIn("must be 0 or 1", sh)
        self.assertIn("must be 0 or 1", bat)

    def test_the_switch_is_off_in_the_slave_by_default(self) -> None:
        text = (ROOT / "src" / "F303-slave" / "slave_i2c_link.c").read_text(encoding="utf-8")
        self.assertRegex(text, r"#ifndef URTC_SLAVE_LINK_CRC\s+#define URTC_SLAVE_LINK_CRC 0")


if __name__ == "__main__":
    unittest.main()
