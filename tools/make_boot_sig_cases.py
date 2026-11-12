#!/usr/bin/env python3
# =============================================================================
# URTC - tools/make_boot_sig_cases.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Writes the signed test cases of the bootloader host tests: tests/boot_sig_cases.json (read by tools/bootloader_twin.py) and tests/boot_sig_cases.h (read by the C
suites). Each case is a patterned test image, the 28-byte header the bootloader rebuilds from the frames of an update, and the 76-byte record that signs it.

Every key here is a throwaway test key derived from a label (the development seed is public by design); no real key is read or created. Signing needs the
shared Python reference, so this tool runs on a machine that has URTC-COMMON next to this repository; the tests themselves need nothing but the generated files.

    python tools/make_boot_sig_cases.py            # write the two files
    python tools/make_boot_sig_cases.py --check    # fail when they are out of date (needs URTC-COMMON)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMMON = ROOT.parent / "URTC-COMMON" / "src"
OUT_JSON = ROOT / "tests" / "boot_sig_cases.json"
OUT_H = ROOT / "tests" / "boot_sig_cases.h"

THIS_HW = 0x0303CC01
SLAVE_HW = 0x0303CB01
DEV_SEED = hashlib.sha256(b"urtc public development image key v1").digest()
OTHER_SEED = hashlib.sha256(b"urtc host test other image key v1").digest()
THIRD_SEED = hashlib.sha256(b"urtc host test third key that no table holds v1").digest()
HOST_TIME = 1_000_000   # a made-up time of the host (seconds); only differences matter to the bootloader


def pack(minor: int, patch: int) -> int:
    return ((0x80 | patch) << 8) | minor


# (size, hardware id of the header, major, minor field, counter, key)
SPECS = [
    (3000, THIS_HW, 0, pack(5, 1), 5, "dev"),
    (1500, THIS_HW, 0, pack(2, 0), 1, "dev"),
    (1500, THIS_HW, 0, pack(3, 0), 1, "dev"),
    (1500, THIS_HW, 0, pack(4, 1), 1, "dev"),
    (1500, THIS_HW, 0, pack(4, 2), 1, "dev"),
    (1500, THIS_HW, 0, pack(4, 3), 1, "dev"),
    (1500, THIS_HW, 0, 4, 1, "dev"),
    (1500, THIS_HW, 0, pack(9, 9), 1, "dev"),
    (1000, THIS_HW, 0, pack(1, 0), 1, "dev"),
    (20, THIS_HW, 0, pack(1, 0), 1, "dev"),
    (3000, THIS_HW, 0, pack(6, 0), 1, "dev"),
    (1500, THIS_HW, 0, pack(4, 3), 3, "dev"),
    (1500, THIS_HW, 0, pack(4, 3), 7, "dev"),
    (1500, THIS_HW, 0, pack(2, 0), 1, "other"),
    (1500, SLAVE_HW, 0, pack(2, 0), 1, "dev"),
    (3000, THIS_HW, 0, pack(5, 1), 5, "other"),
    (3000, THIS_HW, 0, pack(6, 0), 4, "dev"),
    (3000, THIS_HW, 0, pack(6, 0), 9, "dev"),
]


def rotation_specs() -> list[tuple[str, str, str, str | None, int, int, int]]:
    """(name, signer, new current, previous, window seconds, host time, counter): signed rotation commands for the bootloader host tests. Keys by label."""
    return [
        ("dev_to_other_600", "dev", "other", "dev", 600, HOST_TIME, 1),
        ("dev_to_other_none", "dev", "other", None, 0, HOST_TIME, 1),
        ("dev_to_other_long", "dev", "other", "dev", 8 * 86400, HOST_TIME, 1),
        ("dev_to_third", "dev", "third", "dev", 600, HOST_TIME, 1),
        ("other_signs_first", "other", "dev", "other", 600, HOST_TIME, 1),
        ("other_ends_overlap", "other", "other", None, 0, HOST_TIME + 100, 2),
    ]


def test_image(size: int) -> bytes:
    return bytes((i * 13 + 5) & 0xFF for i in range(size))


SEALED_VERSION = (0, 5, 1, 0)
SEALED_COUNTER = 5


def sealed_main_image() -> bytes:
    """What the build leaves for the master application: a vector table, zeros up to the fixed offset, the slot the firmware emits, then code; filled in by the
    seal step (tools/image_record/image_header.py, the one tools/seal_image.py uses). The bootloader never looks inside, so this is the image it is handed."""
    sys.path.insert(0, str(ROOT / "tools"))
    from image_record import image_header as ih  # noqa: PLC0415
    raw = bytearray(range(32)) * 12 + bytearray(2000 - 384)
    raw[0x188:0x200] = bytes(0x200 - 0x188)
    raw[ih.EMBED_OFFSET:ih.EMBED_OFFSET + 32] = ih.embedded_slot(THIS_HW, SEALED_VERSION)
    return ih.seal_embedded(bytes(raw), THIS_HW, SEALED_VERSION)


def header28(hw: int, major: int, minor_field: int, size: int, crc: int) -> bytes:
    """What the bootloader rebuilds from START (size, hardware id) and END (CRC, version): the first 28 bytes of the image header."""
    minor = minor_field & 0xFF
    high = (minor_field >> 8) & 0xFF
    patch = (high & 0x7F) if high & 0x80 else 0          # an unknown patch signs as 0
    return struct.pack(">4sBBHIHBBHHII", b"URIH", 1, 32, 0, hw, major, minor, patch, 0, 0, size, crc)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    if not (COMMON / "urtc_common" / "image_auth.py").is_file():
        print("URTC-COMMON is not next to this repository: nothing was done", file=sys.stderr)
        return 2
    sys.path.insert(0, str(COMMON))
    from urtc_common import ed25519, image_auth as ia, key_rotation_command as krc  # noqa: PLC0415

    seeds = {"dev": DEV_SEED, "other": OTHER_SEED, "third": THIRD_SEED}
    cases = []
    for size, hw, major, minor_field, counter, key in SPECS:
        image = test_image(size)
        crc = zlib.crc32(image) & 0xFFFFFFFF
        head = header28(hw, major, minor_field, size, crc)
        digest = hashlib.sha256(image).digest()
        record = ia.make_record(seeds[key], head, digest, counter)
        public = ed25519.public_key(seeds[key])
        assert ed25519.verify(public, ia.message(head, digest, counter, record[:8]), record[12:]), "self check"
        cases.append({"size": size, "hardware_id": hw, "major": major, "minor_field": minor_field, "counter": counter, "key": key,
                      "crc32": crc, "sha256": digest.hex(), "header28": head.hex(), "record": record.hex()})
    publics = {name: ed25519.public_key(seed).hex() for name, seed in seeds.items()}
    ids = {name: krc.key_id(ed25519.public_key(seed)) for name, seed in seeds.items()}
    rotations = []
    for name, signer, new_current, previous, window_s, host_time, counter in rotation_specs():
        record = krc.sign(seeds[signer], ids[new_current], ids[previous] if previous else None, window_s, host_time, counter)
        rotations.append({"name": name, "record": record.hex()})
    sealed_image = sealed_main_image()
    sealed_crc = zlib.crc32(sealed_image) & 0xFFFFFFFF
    sealed_minor = pack(SEALED_VERSION[1], SEALED_VERSION[2])
    sealed_head = header28(THIS_HW, SEALED_VERSION[0], sealed_minor, len(sealed_image), sealed_crc)
    sealed_digest = hashlib.sha256(sealed_image).digest()
    sealed_record = ia.make_record(seeds["dev"], sealed_head, sealed_digest, SEALED_COUNTER)
    assert ed25519.verify(ed25519.public_key(seeds["dev"]), ia.message(sealed_head, sealed_digest, SEALED_COUNTER, sealed_record[:8]), sealed_record[12:]), "self check"
    sealed = [{"name": "main_sealed", "size": len(sealed_image), "hardware_id": THIS_HW, "major": SEALED_VERSION[0], "minor_field": sealed_minor,
               "counter": SEALED_COUNTER, "key": "dev", "crc32": sealed_crc, "image": sealed_image.hex(), "record": sealed_record.hex()}]
    document = {"about": "Throwaway test keys only; see tools/make_boot_sig_cases.py", "public": publics, "cases": cases, "rotation": rotations, "sealed": sealed}
    text_json = json.dumps(document, indent=1) + "\n"

    def arr(hexs: str) -> str:
        return ", ".join(f"0x{b:02X}" for b in bytes.fromhex(hexs))

    lines = [
        "// Generated by tools/make_boot_sig_cases.py - do not edit. Throwaway test keys only; the development seed is public by design.",
        "#ifndef BOOT_SIG_CASES_H",
        "#define BOOT_SIG_CASES_H",
        "#include <stdint.h>",
        "",
        f"static const uint8_t BOOT_SIG_PUBLIC_DEV[32] = {{{arr(publics['dev'])}}};",
        f"static const uint8_t BOOT_SIG_PUBLIC_OTHER[32] = {{{arr(publics['other'])}}};",
        f"static const uint8_t BOOT_SIG_PUBLIC_THIRD[32] = {{{arr(publics['third'])}}};",
        f"static const uint8_t BOOT_SIG_ID_DEV[8] = {{{arr(ids['dev'].hex())}}};",
        f"static const uint8_t BOOT_SIG_ID_OTHER[8] = {{{arr(ids['other'].hex())}}};",
        f"static const uint8_t BOOT_SIG_ID_THIRD[8] = {{{arr(ids['third'].hex())}}};",
        "",
        "typedef struct {",
        "    uint32_t size, hardware_id, major, minor_field, counter;",
        "    uint8_t other_key;   // 0: signed with the development key, 1: with another throwaway key",
        "    uint8_t sha256[32];  // of the patterned test image",
        "    uint8_t record[76];  // key id | counter | signature",
        "} BootSigCase;",
        "",
        "static const BootSigCase BOOT_SIG_CASES[] = {",
    ]
    for c in cases:
        lines.append(f"    {{{c['size']}u, 0x{c['hardware_id']:08X}u, {c['major']}u, 0x{c['minor_field']:04X}u, {c['counter']}u, {1 if c['key'] == 'other' else 0}, "
                     f"{{{arr(c['sha256'])}}}, {{{arr(c['record'])}}}}},")
    lines += ["};", f"#define BOOT_SIG_CASE_COUNT {len(cases)}u", "",
              "// Signed key rotation commands (100 bytes each, the host signs); the order is the one of rotation_specs() in tools/make_boot_sig_cases.py.",
              "typedef struct {", "    const char *name;", "    uint8_t record[100];", "} BootRotationCase;", "", "static const BootRotationCase BOOT_ROTATION_CASES[] = {"]
    for r in rotations:
        lines.append(f"    {{\"{r['name']}\", {{{arr(r['record'])}}}}},")
    lines += ["};", f"#define BOOT_ROTATION_CASE_COUNT {len(rotations)}u", "", "#endif", ""]
    text_h = "\n".join(lines)
    if args.check:
        stale = [p.name for p, t in ((OUT_JSON, text_json), (OUT_H, text_h)) if not p.is_file() or p.read_text(encoding="utf-8") != t]
        if stale:
            print("BOOT_SIG_CASES=FAIL out of date: " + ", ".join(stale), file=sys.stderr)
            return 1
        print("BOOT_SIG_CASES=PASS")
        return 0
    OUT_JSON.write_text(text_json, encoding="utf-8", newline="\n")
    OUT_H.write_text(text_h, encoding="utf-8", newline="\n")
    print(f"BOOT_SIG_CASES=PASS wrote {len(cases)} cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
