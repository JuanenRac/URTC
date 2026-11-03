#!/usr/bin/env python3
# =============================================================================
# URTC - Firmware signing keys and per-image signature/digest sidecars
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
# Neither bootloader holds a secret any more. The main bootloader verifies an Ed25519 record against a table of PUBLIC keys; the expansion slave bootloader
# holds no key at all (the master verifies for it and confirms the image, docs/SLAVE_BOOT_CONFIRMATION.md). What is left here:
#
#   * the flavour of the trust table the main bootloader is compiled with (dev: the public bench table; release: keys/image_trust.json);
#   * a LEGACY HMAC-SHA256 sidecar of the MAIN application, only for bench boards that still run a bootloader of the earlier generation. It needs the key
#     of the line "master=" of the file named by URTC_SIGNING_KEY_FILE, or the public development key. A release build (URTC_RELEASE=1) without that
#     file simply publishes no HMAC: the signature of a release is the Ed25519 record, made apart from the build.
#   * no HMAC for the expansion slave at all (the old "slave=" line of a key file is read and ignored).
#
# CLI (used by build_firmware.sh/.bat):
#   python3 firmware_signing.py cflags master          -> -DURTC_TRUST_DEV for the bench table, nothing for the release one
#   python3 firmware_signing.py check                  -> prints trust=dev|release
#   python3 firmware_signing.py hmac master FILE       -> hex HMAC of FILE (legacy bootloaders only)
import hashlib
import hmac
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
# The PUBLIC development HMAC key of the main board. The main bootloader no longer holds any HMAC key (it verifies an Ed25519 record against a table of
# public keys, src/F303-master/boot/bootloader_trust.h); this value only lets the release tooling keep producing the HMAC that bootloaders of the earlier
# generation (bench boards) still check.
MASTER_DEV_HMAC_KEY_HEX = "2526cc3e9011ec7c49ecd2d2b7f389e359aa246014ff5154a0af8a3c3bf95581"
BOARDS = ("master",)
_IGNORED_KEY_NAMES = ("slave",)    # the line of the retired slave key: tolerated in an old key file, never used


class SigningKeyError(Exception):
    pass


def dev_key(board):
    """The PUBLIC development key of `board` (only the main board still has one, for legacy bootloaders)."""
    if board == "master":
        return bytes.fromhex(MASTER_DEV_HMAC_KEY_HEX)
    raise SigningKeyError(f"{board!r} has no signing key: the expansion slave bootloader holds none")


def parse_key_file(text):
    keys = {}
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise SigningKeyError(f"key file line without '=': {raw!r}")
        name, value = (part.strip() for part in line.split("=", 1))
        if name in _IGNORED_KEY_NAMES:
            continue
        if name not in BOARDS:
            raise SigningKeyError(f"unknown key name {name!r} (expected master)")
        if not re.fullmatch(r"[0-9A-Fa-f]{64}", value):
            raise SigningKeyError(f"key {name!r} must be exactly 64 hex characters (32 bytes)")
        keys[name] = bytes.fromhex(value)
    return keys


def load_key(board, env=None):
    """(key_bytes, kind) where kind is "release" (from URTC_SIGNING_KEY_FILE) or "dev" (the public development key)."""
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


def trust_flavour(env=None):
    """Which trust table the main bootloader is built with: "release" for an official build (URTC_RELEASE=1), otherwise the public "dev" table."""
    env = os.environ if env is None else env
    return "release" if env.get("URTC_RELEASE", "0") == "1" else "dev"


def cflags(board, env=None):
    if board == "master":
        # no key is injected: the flavour picks the public table the bootloader is compiled with (release: the file keys/image_trust.json, closed until the
        # image-signing key exists; dev: the bench table, whose key is public)
        return "-DURTC_TRUST_DEV" if trust_flavour(env) == "dev" else ""
    raise SigningKeyError(f"{board!r} takes no compiler flag: the expansion slave bootloader holds no key")


def hmac_file(board, path, env=None):
    key, _kind = load_key(board, env)
    with open(path, "rb") as f:
        return hmac.new(key, f.read(), hashlib.sha256).hexdigest()


def application_signing(board, bin_path, env=None):
    """The `signing` block of a component of the manifest.

    The main application keeps its legacy HMAC (public development key, or the private key of a bench build); a release build without that key publishes none.
    The expansion slave application never has one. `key_kind` says which trust table the build was made for ("dev" or "release"), so that a release
    workflow can tell a bench image from an official one.
    """
    env = os.environ if env is None else env
    flavour = trust_flavour(env)
    if board == "master":
        try:
            key, kind = load_key("master", env)
        except SigningKeyError:
            if flavour == "release":
                return {"algorithm": "none", "key_kind": flavour}
            raise
        return {"algorithm": "HMAC-SHA256", "key_kind": kind, "key_id": key_id(key), "hmac_sha256": hmac_file("master", bin_path, env)}
    return {"algorithm": "none", "key_kind": flavour}


def main(argv):
    if len(argv) < 2:
        print(__doc__ or "usage: firmware_signing.py cflags|check|hmac ...", file=sys.stderr)
        return 2
    try:
        if argv[1] == "cflags" and len(argv) == 3:
            print(cflags(argv[2]))
        elif argv[1] == "check" and len(argv) == 2:
            flavour = trust_flavour()
            if flavour == "dev":
                print("WARNING: building with the PUBLIC development trust table - images are only for bench boards built with the same dev bootloader", file=sys.stderr)
            print(f"trust={flavour}")
        elif argv[1] == "hmac" and len(argv) == 4:
            print(hmac_file(argv[2], argv[3]))
        else:
            print("usage: firmware_signing.py cflags master | check | hmac master FILE", file=sys.stderr)
            return 2
    except SigningKeyError as exc:
        print(f"firmware_signing.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
