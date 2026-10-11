#!/usr/bin/env python3
# =============================================================================
# URTC - Firmware signing keys and per-image signature/digest sidecars
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
# The bootloaders authenticate an application image with HMAC-SHA256 over the
# whole .bin (sent in 4 CAN chunks before END_UPDATE). Whoever computes that
# HMAC must hold the key, so the key must live with whoever BUILDS the
# release - never in this repository, never in URTC-WEB-STUDIO's JavaScript,
# never as a default in URTC-FLASHER. This module:
#
#   * reads the private keys from the file named by URTC_SIGNING_KEY_FILE
#     (two lines: "master=<64 hex>" and "slave=<64 hex>", see
#     docs/FIRMWARE_SIGNING.TXT for how to create one);
#   * falls back to the PUBLIC development keys (parsed from the bootloader
#     sources' own #else branch, so there is exactly one copy of them) and
#     says so loudly - a release build (URTC_RELEASE=1) refuses that;
#   * prints the -D compiler flags that inject the key into the bootloaders;
#   * computes the HMAC of each application image at build time, so the
#     flashing tools only ever TRANSPORT a signature published next to the
#     image (<bin>.manifest.json "hmac_sha256") instead of signing anything
#     themselves.
#
# CLI (used by build_firmware.sh/.bat):
#   python3 firmware_signing.py cflags master|slave   -> -D... or nothing
#   python3 firmware_signing.py check                  -> exit 1 if a release
#                                                         build has no keys
#   python3 firmware_signing.py hmac master|slave FILE -> hex HMAC of FILE
import hashlib
import hmac
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
_SOURCES = {
    "master": (os.path.join(ROOT, "src", "F303-master", "boot", "bootloader_crypto.c"), "URTC_HMAC_KEY_BYTES"),
    "slave": (os.path.join(ROOT, "src", "F303-slave", "boot", "slaveboot_crypto.c"), "URTC_SLAVE_HMAC_KEY_BYTES"),
}


class SigningKeyError(Exception):
    pass


def dev_key(board):
    """The PUBLIC development key of `board`, read from its bootloader's own
    #else branch - the one place it is written down."""
    path, _macro = _SOURCES[board]
    with open(path, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"#else.*?const uint8_t HMAC_KEY\[32\] = \{(.*?)\};", text, re.S)
    if not m:
        raise SigningKeyError(f"no development key block found in {path}")
    key = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
    if len(key) != 32:
        raise SigningKeyError(f"development key in {path} is {len(key)} bytes, expected 32")
    return key


def parse_key_file(text):
    keys = {}
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise SigningKeyError(f"key file line without '=': {raw!r}")
        name, value = (part.strip() for part in line.split("=", 1))
        if name not in _SOURCES:
            raise SigningKeyError(f"unknown key name {name!r} (expected master or slave)")
        if not re.fullmatch(r"[0-9A-Fa-f]{64}", value):
            raise SigningKeyError(f"key {name!r} must be exactly 64 hex characters (32 bytes)")
        keys[name] = bytes.fromhex(value)
    return keys


def load_key(board, env=None):
    """(key_bytes, kind) where kind is "release" (from URTC_SIGNING_KEY_FILE)
    or "dev" (the public development key)."""
    env = os.environ if env is None else env
    path = env.get("URTC_SIGNING_KEY_FILE", "").strip()
    if path:
        try:
            with open(path, encoding="utf-8") as f:
                keys = parse_key_file(f.read())
        except OSError as exc:
            raise SigningKeyError(f"cannot read URTC_SIGNING_KEY_FILE {path}: {exc}") from exc
        if board not in keys:
            raise SigningKeyError(f"URTC_SIGNING_KEY_FILE {path} has no '{board}=' line")
        if keys[board] == dev_key(board):
            raise SigningKeyError(f"the '{board}' key in {path} IS the public development key - generate a new one")
        return keys[board], "release"
    if env.get("URTC_RELEASE", "0") == "1":
        raise SigningKeyError("URTC_RELEASE=1 but URTC_SIGNING_KEY_FILE is not set - a release is never signed with the public development key")
    return dev_key(board), "dev"


def key_id(key):
    """A short, non-secret fingerprint so a manifest says WHICH key signed it."""
    return hashlib.sha256(b"URTC-KEY-ID:" + key).hexdigest()[:16]


def cflags(board, env=None):
    key, kind = load_key(board, env)
    if kind == "dev":
        return ""
    _path, macro = _SOURCES[board]
    return "-D" + macro + "=" + ",".join(f"0x{b:02X}" for b in key)


def hmac_file(board, path, env=None):
    key, _kind = load_key(board, env)
    with open(path, "rb") as f:
        return hmac.new(key, f.read(), hashlib.sha256).hexdigest()


def main(argv):
    if len(argv) < 2:
        print(__doc__ or "usage: firmware_signing.py cflags|check|hmac ...", file=sys.stderr)
        return 2
    try:
        if argv[1] == "cflags" and len(argv) == 3:
            print(cflags(argv[2]))
        elif argv[1] == "check" and len(argv) == 2:
            kinds = {board: load_key(board)[1] for board in _SOURCES}
            if "dev" in kinds.values():
                print("WARNING: signing with the PUBLIC development key(s) - images are only for bench boards built with the same dev bootloader", file=sys.stderr)
            print(" ".join(f"{b}={k}" for b, k in kinds.items()))
        elif argv[1] == "hmac" and len(argv) == 4:
            print(hmac_file(argv[2], argv[3]))
        else:
            print("usage: firmware_signing.py cflags master|slave | check | hmac master|slave FILE", file=sys.stderr)
            return 2
    except SigningKeyError as exc:
        print(f"firmware_signing.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
