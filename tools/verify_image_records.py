#!/usr/bin/env python3
# =============================================================================
# URTC - tools/verify_image_records.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Checks, with no key and no board, that the image signature records of a folder say what they must.

    python tools/verify_image_records.py DIR [--flavour release|dev]

For every ``<image>.imgsig`` in DIR it rebuilds what the bootloader checks and decides as the bootloader would (the same reference code, in
``tools/image_record``, copied from URTC-COMMON):

  * the record is the 76 bytes of ``docs/SIGNATURE_SEPARATION.md`` and was made with a key of the trust table of the chosen flavour
    (``keys/image_trust.json``, public values only);
  * it covers exactly this binary: the hardware, the version of the file name, the length, the CRC-32 and the SHA-256 of the image;
  * the counter is not below the minimum compiled into the bootloaders.

The header the record covers is never part of the flashed image: it is rebuilt here from the file name and the bytes, as the signing tool does.
A main-board bootloader found in the folder must hold exactly the trust table of the flavour, so the keys that signed are the keys it trusts.
Prints VERIFY_IMAGE_RECORDS=PASS and exits 0 only when every record verifies.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from image_record import image_header, image_sig  # noqa: E402

HARDWARE = {"MAIN": ("master", 0x0303CC01), "SLAVE": ("slave", 0x0303CB01)}
NAME = re.compile(r"^URTC_(MAIN|SLAVE)_FIRMWARE_v(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?\.bin\.imgsig$")
BOOTLOADER = re.compile(r"^URTC_MAIN_BOOTLOADER_v[\d.]+\.bin$")


def _trust_module():
    spec = importlib.util.spec_from_file_location("urtc_image_trust_records", HERE / "image_trust.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def header28(hardware_id: int, version: tuple[int, int, int, int], payload: bytes) -> bytes:
    """The 28 bytes the record covers: the descriptor of the image, rebuilt (it is not in the flashed file)."""
    return image_header.make_header(payload, hardware_id, version, signed=False)[:28]


def check_folder(folder: Path, flavour: str = "release", doc: dict | None = None) -> list[str]:
    """Returns the problems found (empty when every record verifies). A folder without any record is not a problem here: whether one is required is
    the business of tools/publish_release_assets.py."""
    trust = _trust_module()
    doc = doc or trust.load()
    try:
        table_bytes = trust.table_bytes(doc, flavour)
    except trust.TrustFileError as error:
        return [f"the {flavour} trust table cannot be built: {error}"]
    table = image_sig.parse_table(table_bytes)
    floor = int(doc["min_counter"])
    problems: list[str] = []
    records = sorted(folder.glob("*.imgsig"))
    for record in records:
        found = NAME.match(record.name)
        if not found:
            problems.append(f"{record.name}: not the record of an application image (URTC_MAIN_FIRMWARE_v*.bin or URTC_SLAVE_FIRMWARE_v*.bin)")
            continue
        _, hardware_id = HARDWARE[found.group(1)]
        version = (int(found.group(2)), int(found.group(3)), int(found.group(4)), int(found.group(5) or 0))
        image = folder / record.name[: -len(".imgsig")]
        if not image.is_file():
            problems.append(f"{record.name}: its image {image.name} is not in the folder")
            continue
        payload = image.read_bytes()
        verdict = image_sig.verify_image(record.read_bytes().decode("ascii", errors="replace"), table, hardware_id,
                                         header28(hardware_id, version, payload), payload, floor=floor)
        if verdict != "ok":
            problems.append(f"{record.name}: does not verify against the {flavour} trust table for {image.name} ({verdict})")
    if records:
        for boot in sorted(path for path in folder.iterdir() if BOOTLOADER.match(path.name)):
            why = trust.check_binary(doc, boot.read_bytes(), "master", flavour)
            if why:
                problems.append(f"{boot.name}: {why}; the keys that sign are not the keys it trusts")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    parser.add_argument("--flavour", choices=("release", "dev"), default="release")
    args = parser.parse_args(argv)
    folder = Path(args.folder)
    if not folder.is_dir():
        print(f"VERIFY_IMAGE_RECORDS=FAIL {folder} is not a folder")
        return 1
    problems = check_folder(folder, args.flavour)
    for problem in problems:
        print(f"  PROBLEM {problem}")
    print("VERIFY_IMAGE_RECORDS=PASS" if not problems else "VERIFY_IMAGE_RECORDS=FAIL")
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
