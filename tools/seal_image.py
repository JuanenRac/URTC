#!/usr/bin/env python3
# =============================================================================
# URTC - tools/seal_image.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The step after the link: fill in the header that an application image carries inside it.

    python tools/seal_image.py main  URTC_MAIN_FIRMWARE_v0.4.8.bin
    python tools/seal_image.py slave URTC_SLAVE_FIRMWARE_v0.3.2.bin

The firmware emits a 32-byte slot at the fixed offset 0x200 of the image (src/F303-master/firmware_image_slot.c, src/F303-slave/slave_image_slot.c; the linker
script puts it there) with the hardware it is for and its version, four components. The length and the CRC-32 of the whole image depend on the link, so they are
filled in here, in the same file: the vector table and the flash map do not move and the file is no longer than it was. The file name must state the version the slot
states and the slot must name the board asked for, or nothing is written. Intel HEX next to the binary (same name, ``.hex``) is written again from the sealed bytes,
so the two files carry the same image. The same bytes come out every time. Prints SEAL_IMAGE=PASS and exits 0, or SEAL_IMAGE=FAIL and exits 1.

The signature record (``.imgsig``) is made afterwards, over the sealed file, exactly as before (docs/SIGNATURE_SEPARATION.md).
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from image_record import image_header  # noqa: E402

BOARDS = {"main": (0x0303CC01, 0x08008000), "slave": (0x0303CB01, 0x08005000)}   # (HardwareID, address of the application slot)
VERSION_IN_NAME = re.compile(r"_v(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?\.bin$")


def intel_hex(data: bytes, base: int) -> str:
    """Intel HEX text of ``data`` placed at ``base``: extended linear address records, 16 data bytes per line, the end record."""
    def record(address: int, kind: int, body: bytes) -> str:
        raw = bytes([len(body), address >> 8 & 0xFF, address & 0xFF, kind]) + body
        return ":" + (raw + bytes([(-sum(raw)) & 0xFF])).hex().upper()

    lines: list[str] = []
    upper = -1
    for offset in range(0, len(data), 16):
        address = base + offset
        if address >> 16 != upper:
            upper = address >> 16
            lines.append(record(0, 4, upper.to_bytes(2, "big")))
        lines.append(record(address & 0xFFFF, 0, data[offset:offset + 16]))
    lines.append(":00000001FF")
    return "\r\n".join(lines) + "\r\n"


def version_of_name(name: str) -> tuple[int, int, int, int] | None:
    found = VERSION_IN_NAME.search(name)
    if not found:
        return None
    return (int(found.group(1)), int(found.group(2)), int(found.group(3)), int(found.group(4) or 0))


def seal(board: str, binary: Path) -> tuple[bytes, image_header.ImageInfo]:
    """The sealed bytes of ``binary`` and what its header states; ValueError says why not."""
    hardware, _base = BOARDS[board]
    version = version_of_name(binary.name)
    if version is None:
        raise ValueError(f"{binary.name}: the file name does not state a version (..._vMAJOR.MINOR.PATCH[.BUILD].bin)")
    sealed = image_header.seal_embedded(binary.read_bytes(), hardware, version)   # refuses another board, another version and a binary with no slot
    status, info = image_header.verify_embedded(sealed)
    if status != image_header.OK or info is None:
        raise ValueError("the sealed image does not verify: " + image_header.STATUS_NAMES[status])
    return sealed, info


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("board", choices=sorted(BOARDS))
    parser.add_argument("binary", type=Path)
    args = parser.parse_args(argv)
    try:
        sealed, info = seal(args.board, args.binary)
        args.binary.write_bytes(sealed)
        hex_path = args.binary.with_suffix(".hex")
        if hex_path.is_file():
            hex_path.write_bytes(intel_hex(sealed, BOARDS[args.board][1]).encode("ascii"))
    except (ValueError, OSError) as error:
        print(f"SEAL_IMAGE=FAIL {error}")
        return 1
    print(f"SEAL_IMAGE=PASS {args.binary.name} hardware=0x{info.hardware_id:08X} version={info.version_text()} length={info.payload_length}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
