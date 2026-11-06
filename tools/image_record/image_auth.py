# =============================================================================
# URTC-COMMON - src/urtc_common/image_auth.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The Ed25519 authentication of a firmware image: host reference of ``firmware_base/urtc_auth.h``.

[FIRMWARE] The bootloader side is designed and tested on the host only; nothing is built for the board or flashed. See ``docs/BOOTLOADER_AUTH.md``.

What is signed is a fixed 88-byte record, never the whole image, because a bootloader cannot hold 112 KB in RAM:

    domain (16)  "URTC-IMG-SIG-V1" and a zero
    header (28)  bytes 0 to 27 of the image header: hardware id, the four version components, payload length and CRC-32
    sha256 (32)  SHA-256 of the payload
    counter (4)  the rollback counter, big-endian
    key id (8)   the first 8 bytes of SHA-512 of the public key that signs

The authentication record that travels with the image is ``key id (8) | counter (4) | signature (64)``: 76 bytes. The bootloader keeps one to two trusted
public keys (rotation) and a floor for the counter. This module builds records (release machine only: it takes a seed) and decides, with the same rules as
the C code, whether an image may be installed. A release signing key never lives in the repository: the vectors use throwaway test keys.
"""
from __future__ import annotations

import hashlib
import struct

from . import ed25519

DOMAIN = b"URTC-IMG-SIG-V1\x00"
HEADER_BYTES = 28
RECORD_SIZE = 76
MESSAGE_SIZE = 88
KEY_ID_SIZE = 8

POLICY_ED25519_ONLY, POLICY_EITHER, POLICY_HMAC_ONLY = 0, 1, 2
POLICY_NAMES = ("ed25519_only", "either", "hmac_only")
POLICY_DEFAULT = POLICY_ED25519_ONLY   # what a bootloader ships with, the first one of the transition included, and what a zeroed setting means
(OK, BAD_ARGS, NOT_SIGNED, UNKNOWN_KEY, BAD_SIGNATURE, BAD_HMAC, HARDWARE_MISMATCH, ROLLBACK_COUNTER, ROLLBACK_VERSION) = range(9)
STATUS_NAMES = ("ok", "bad_args", "not_signed", "unknown_key", "bad_signature", "bad_hmac", "hardware_mismatch", "rollback_counter", "rollback_version")


def key_id(public_key: bytes) -> bytes:
    return hashlib.sha512(public_key).digest()[:KEY_ID_SIZE]


def message(header28: bytes, payload_sha256: bytes, counter: int, kid: bytes) -> bytes:
    if len(header28) != HEADER_BYTES or len(payload_sha256) != 32 or len(kid) != KEY_ID_SIZE:
        raise ValueError("wrong length in the signed record")
    return DOMAIN + header28 + payload_sha256 + struct.pack(">I", counter) + kid


def make_record(seed: bytes, header28: bytes, payload_sha256: bytes, counter: int) -> bytes:
    """Sign an image: ``key id | counter | signature``. For the release machine; a seed is a secret."""
    kid = key_id(ed25519.public_key(seed))
    return kid + struct.pack(">I", counter) + ed25519.sign(seed, message(header28, payload_sha256, counter, kid))


def header_version(header28: bytes) -> tuple[int, int, int, int]:
    major, minor, patch, build = struct.unpack(">HBBH", header28[12:18])
    return major, minor, patch, build


def header_hardware_id(header28: bytes) -> int:
    return int(struct.unpack(">I", header28[8:12])[0])


def decide(policy: int, trusted_keys: list[bytes], hardware_id: int, installed_counter: int, installed_version: tuple[int, int, int, int] | None,
           downgrade_authorized: bool, header28: bytes, payload_sha256: bytes, record: bytes | None, hmac_ok: bool) -> tuple[int, int]:
    """May this image be installed? Returns ``(status, counter)``: the counter to store as the new floor when the status is ``OK``.

    Order: the hardware id, then the authentication (by policy), then the rollback rules, so an image nobody vouched for gets an authentication status and
    reveals nothing about the installed version. ``hmac_ok`` is the result of the HMAC-SHA256 check the bootloader already does.
    """
    if len(header28) != HEADER_BYTES or len(payload_sha256) != 32 or policy not in (0, 1, 2) or (record is not None and len(record) != RECORD_SIZE):
        return BAD_ARGS, installed_counter
    if header_hardware_id(header28) != hardware_id:
        return HARDWARE_MISMATCH, installed_counter
    ed_status = NOT_SIGNED
    counter = installed_counter
    if record is not None:
        kid, counter = record[:KEY_ID_SIZE], struct.unpack(">I", record[8:12])[0]
        keys = [k for k in trusted_keys if key_id(k) == kid]
        if not keys:
            ed_status = UNKNOWN_KEY
        else:
            ed_status = OK if ed25519.verify(keys[0], message(header28, payload_sha256, counter, kid), record[12:]) else BAD_SIGNATURE
    via_ed = False
    if policy == POLICY_HMAC_ONLY:
        status = OK if hmac_ok else BAD_HMAC
    elif policy == POLICY_ED25519_ONLY:
        status = ed_status
        via_ed = status == OK
    else:
        if ed_status == OK:
            status, via_ed = OK, True
        else:
            status = OK if hmac_ok else ed_status
    if status != OK:
        return status, installed_counter
    if via_ed and counter < installed_counter:
        return ROLLBACK_COUNTER, installed_counter
    if installed_version is not None and header_version(header28) < installed_version and not downgrade_authorized:
        return ROLLBACK_VERSION, installed_counter
    return OK, counter if via_ed else installed_counter
