#!/usr/bin/env python3
# =============================================================================
# URTC - tools/image_trust.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The trust table of the bootloaders from its public source file, and the check that a compiled bootloader holds exactly that table.

    python tools/image_trust.py validate                    the file is well formed (key ids match the keys, states are legal)
    python tools/image_trust.py table FLAVOUR [--c]         the table bytes (hexadecimal, or a C array for the linker section)
    python tools/image_trust.py check BIN TARGET FLAVOUR    BIN holds, at the fixed offset of TARGET, exactly the table of FLAVOUR
    python tools/image_trust.py fingerprints FLAVOUR        the key ids (the pin the local release tool compares)

Layout (little endian): ``TKEY`` | schema 1 | number of keys (1 or 2) | flags 0 (u16) | per key: key id (8) | public key (32) | flags (1: current,
2: next, 4: revoked) | 3 zero bytes | CRC-32 (zlib) of all of it. Only public keys are read or written here; no key file is ever opened.
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
TRUST_FILE = ROOT / "keys" / "image_trust.json"
STATES = {"current": 1, "next": 2, "revoked": 4}


class TrustFileError(ValueError):
    pass


def load(path: Path = TRUST_FILE) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def key_id(public: bytes) -> bytes:
    return hashlib.sha512(public).digest()[:8]


def validate(doc: dict) -> None:
    if doc.get("format") != 1 or doc.get("schema") != 1:
        raise TrustFileError("unknown format or schema")
    if doc.get("default_policy") != "ed25519_only":
        raise TrustFileError("the default policy must be ed25519_only (fails closed)")
    if not isinstance(doc.get("min_counter"), int) or not 0 <= doc["min_counter"] <= 0xFFFFFFFF:
        raise TrustFileError("min_counter is a 32-bit number")
    for name, target in doc.get("targets", {}).items():
        if not str(target.get("table_offset", "")).startswith("0x") or int(target["table_offset"], 16) % 4:
            raise TrustFileError(f"target {name}: table_offset must be a word-aligned hexadecimal number")
    if set(doc.get("flavours", {})) != {"dev", "release"}:
        raise TrustFileError("the flavours are exactly dev and release")
    for flavour, body in doc["flavours"].items():
        keys = body["keys"]
        if len(keys) > 2:
            raise TrustFileError(f"{flavour}: at most two keys")
        seen = set()
        for entry in keys:
            public = bytes.fromhex(entry["public"])
            if len(public) != 32 or entry["state"] not in STATES:
                raise TrustFileError(f"{flavour}: a key is 32 bytes and its state one of {sorted(STATES)}")
            if entry["key_id"] != key_id(public).hex():
                raise TrustFileError(f"{flavour}: key_id {entry['key_id']} is not the hash prefix of its key")
            if entry["public"] in seen:
                raise TrustFileError(f"{flavour}: a key is listed twice")
            seen.add(entry["public"])
        if sum(1 for e in keys if e["state"] == "current") > 1:
            raise TrustFileError(f"{flavour}: more than one current key")
        if flavour == "dev" and not any(e["state"] == "current" for e in keys):
            raise TrustFileError("the dev flavour needs a current key")


def table_bytes(doc: dict, flavour: str) -> bytes:
    validate(doc)
    keys = doc["flavours"][flavour]["keys"]
    if not any(e["state"] == "current" for e in keys):
        raise TrustFileError(f"the {flavour} table has no current key: it cannot be built (the image-signing key is not provisioned yet)")
    body = b"TKEY" + struct.pack("<BBH", 1, len(keys), 0)
    for entry in keys:
        public = bytes.fromhex(entry["public"])
        body += key_id(public) + public + bytes([STATES[entry["state"]]]) + bytes(3)
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def check_binary(doc: dict, binary: bytes, target: str, flavour: str) -> str | None:
    """None when the binary holds exactly the expected table at the fixed offset; otherwise why not."""
    expected = table_bytes(doc, flavour)
    offset = int(doc["targets"][target]["table_offset"], 16)
    if binary[offset:offset + len(expected)] == expected:
        return None
    if binary[offset:offset + 4] != b"TKEY":
        return f"no trust table at offset 0x{offset:X}"
    return f"the table at offset 0x{offset:X} differs from the {flavour} table of the public file"


def fingerprints(doc: dict, flavour: str) -> list[str]:
    validate(doc)
    return [e["key_id"] for e in doc["flavours"][flavour]["keys"]]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("validate")
    table = commands.add_parser("table")
    table.add_argument("flavour", choices=("dev", "release"))
    table.add_argument("--c", action="store_true", help="print a C array")
    check = commands.add_parser("check")
    check.add_argument("binary")
    check.add_argument("target", choices=("master", "slave"))
    check.add_argument("flavour", choices=("dev", "release"))
    pins = commands.add_parser("fingerprints")
    pins.add_argument("flavour", choices=("dev", "release"))
    args = parser.parse_args(argv)
    try:
        doc = load()
        if args.command == "validate":
            validate(doc)
            print("IMAGE_TRUST=PASS")
        elif args.command == "table":
            data = table_bytes(doc, args.flavour)
            print(", ".join(f"0x{b:02X}" for b in data) if args.c else data.hex())
        elif args.command == "fingerprints":
            print("\n".join(fingerprints(doc, args.flavour)))
        else:
            problem = check_binary(doc, Path(args.binary).read_bytes(), args.target, args.flavour)
            if problem:
                print("IMAGE_TRUST=FAIL " + problem, file=sys.stderr)
                return 1
            print("IMAGE_TRUST=PASS the bootloader holds the " + args.flavour + " table")
        return 0
    except (TrustFileError, OSError, KeyError, ValueError) as error:
        print(f"IMAGE_TRUST=FAIL {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
