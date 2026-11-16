# =============================================================================
# URTC Firmware - tests/test_feature_map_doc.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The second byte of the switch map: the F-RAM map and the CAN reference say what the firmware does, and the record fits where it lives."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FEATURES_H = (ROOT / "src" / "F303-master" / "fw_features.h").read_text(encoding="utf-8")
EEPROM = (ROOT / "docs" / "EEPROM.TXT").read_text(encoding="utf-8")
CANBUS = (ROOT / "docs" / "CANBUS.TXT").read_text(encoding="utf-8")
FIRMWARE_FEATURES_C = (ROOT / "src" / "F303-master" / "firmware_features.c").read_text(encoding="utf-8")


def define(name: str) -> int:
    match = re.search(rf"#define {name}\s+(0x[0-9A-Fa-f]+|\d+)u", FEATURES_H)
    assert match, name
    return int(match.group(1), 0)


def test_the_record_ends_before_the_next_region_of_the_chip():
    address = define("FEAT_CONFIG_FRAM_ADDR")
    size = define("FEAT_CONFIG_RAW_SIZE")
    pid = int(re.search(r"#define PID_FRAM_ADDR (\d+)u", FIRMWARE_FEATURES_C).group(1))
    assert address + size <= pid


def test_every_switch_of_the_second_byte_is_in_the_supported_mask_and_one_bit():
    names = re.findall(r"#define (FEAT_EXT_(?!SUPPORTED)\w+)\s+(0x[0-9A-Fa-f]+)u", FEATURES_H)
    assert names
    mask = 0
    for _name, value in names:
        bit = int(value, 16)
        assert bit & (bit - 1) == 0 and bit < 256
        mask |= bit
    assert define("FEAT_EXT_SUPPORTED") == mask


def test_the_documents_describe_both_versions_of_the_record():
    assert define("FEAT_CONFIG_VERSION_1") == 1 and define("FEAT_CONFIG_VERSION") == 2
    assert "(address 976, version 2, 16 bytes" in EEPROM
    assert "mask_ext" in EEPROM and "Version 1 (written by earlier releases)" in EEPROM
    assert "still read" in EEPROM and "not rewritten at power-up" in EEPROM


def test_the_can_reference_describes_the_six_byte_forms_and_the_host_ids():
    assert "rxData[5]" in CANBUS and "URTC_HOST_FRAME_IDS" in CANBUS
    assert "txData[3]: (six bytes only)" in CANBUS and "a single byte 0x02" in CANBUS
