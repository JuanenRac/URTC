# =============================================================================
# URTC-COMMON - src/urtc_common/image_sig.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The signature of an image as a record apart from the build: the ``.imgsig`` file and the trust table of the bootloader.

[FIRMWARE] Host reference only; nothing is built for a board and no bootloader reads these bytes yet. See ``docs/SIGNATURE_SEPARATION.md``.

``.imgsig``: the 76-byte record that travels with an image (``key id (8) | counter (4, big-endian) | Ed25519 signature (64)``, see ``image_auth``) written as
152 hexadecimal digits on one line. It is made after the build, from the header, the SHA-256 of the payload and the counter, so the build needs no key.

Trust table (little-endian), the only thing a bootloader holds to decide who may sign: a public file, never a secret.

    magic "TKEY" (4) | schema (1) = 1 | number of keys (1) = 1 or 2 | table flags (2) = 0
    per key: key id (8) | public key (32) | flags (1: 1 current, 2 next, 4 revoked) | 3 zero bytes
    CRC-32 (zlib) of everything above (4)

This module signs only when it is given a seed (the release machine, or an ephemeral test key); it never reads, creates or stores a key file.
"""
from __future__ import annotations

import hashlib
import struct
import zlib

from . import ed25519, image_auth as ia

TABLE_MAGIC = b"TKEY"
TABLE_SCHEMA = 1
KEY_ENTRY_SIZE = 44
FLAG_CURRENT, FLAG_NEXT, FLAG_REVOKED = 1, 2, 4
KNOWN_FLAGS = FLAG_CURRENT | FLAG_NEXT | FLAG_REVOKED
IMGSIG_HEX_DIGITS = ia.RECORD_SIZE * 2
# The seed of the key a bench build trusts. It is public on purpose: a bootloader built with the dev table is a bench build, never an official one.
DEV_SEED = hashlib.sha256(b"urtc public development image key v1").digest()


class TrustError(ValueError):
    """The trust table or the .imgsig file is not acceptable; the message says why."""


def table_size(keys: int) -> int:
    return 8 + KEY_ENTRY_SIZE * keys + 4


def pack_table(keys: list[tuple[bytes, int]]) -> bytes:
    """The table for ``[(public key, flags), ...]``; the key ids are derived, never typed."""
    if not 1 <= len(keys) <= 2:
        raise TrustError("a trust table holds one or two keys")
    body = TABLE_MAGIC + struct.pack("<BBH", TABLE_SCHEMA, len(keys), 0)
    for public, flags in keys:
        if len(public) != 32:
            raise TrustError("a public key is 32 bytes")
        if flags & ~KNOWN_FLAGS or flags == 0 or (flags & FLAG_REVOKED and flags != FLAG_REVOKED) or (flags & FLAG_CURRENT and flags & FLAG_NEXT):
            raise TrustError(f"flags {flags} are not a valid state of a key")
        body += ia.key_id(public) + public + bytes([flags]) + bytes(3)
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def parse_table(data: bytes) -> list[tuple[bytes, int]]:
    """Decode and validate a table (any bytes after it are ignored). Fails closed: every doubt is an error."""
    if len(data) < table_size(1) or data[:4] != TABLE_MAGIC:
        raise TrustError("no trust table here (bad magic)")
    schema, count, table_flags = struct.unpack_from("<BBH", data, 4)
    if schema != TABLE_SCHEMA:
        raise TrustError(f"unknown table schema {schema}")
    if count not in (1, 2) or table_flags != 0:
        raise TrustError("the table must hold one or two keys and no flags")
    size = table_size(count)
    if len(data) < size:
        raise TrustError("the table is cut short")
    if struct.unpack_from("<I", data, size - 4)[0] != zlib.crc32(data[:size - 4]) & 0xFFFFFFFF:
        raise TrustError("the CRC-32 of the table does not match")
    keys: list[tuple[bytes, int]] = []
    for index in range(count):
        base = 8 + index * KEY_ENTRY_SIZE
        kid, public, flags, pad = data[base:base + 8], data[base + 8:base + 40], data[base + 40], data[base + 41:base + 44]
        if kid != ia.key_id(public):
            raise TrustError("a key id is not the SHA-512 prefix of its public key")
        if pad != bytes(3) or flags == 0 or flags & ~KNOWN_FLAGS or (flags & FLAG_REVOKED and flags != FLAG_REVOKED) or (flags & FLAG_CURRENT and flags & FLAG_NEXT):
            raise TrustError("invalid flags or padding in a key entry")
        keys.append((public, flags))
    if len({k[0] for k in keys}) != count:
        raise TrustError("a key is listed twice")
    if sum(1 for _, f in keys if f & FLAG_CURRENT) > 1:
        raise TrustError("more than one current key")
    return keys


def find_table(binary: bytes, offset: int) -> list[tuple[bytes, int]]:
    """Read the table of a compiled bootloader at its fixed offset (no disassembly)."""
    if offset < 0 or offset + table_size(1) > len(binary):
        raise TrustError("the offset is outside the binary")
    return parse_table(binary[offset:])


def trusted_keys(table: list[tuple[bytes, int]]) -> list[bytes]:
    """The public keys a bootloader with this table accepts: every key that is not revoked (current first)."""
    live = [(p, f) for p, f in table if not f & FLAG_REVOKED]
    return [p for p, f in sorted(live, key=lambda e: 0 if e[1] & FLAG_CURRENT else 1)]


def fingerprints(table: list[tuple[bytes, int]]) -> list[str]:
    return [ia.key_id(p).hex() for p, _ in table]


# --- the .imgsig file ----------------------------------------------------------------------------------------------------------------------
def encode_imgsig(record: bytes) -> str:
    if len(record) != ia.RECORD_SIZE:
        raise TrustError(f"an image record is {ia.RECORD_SIZE} bytes")
    return record.hex() + "\n"


def decode_imgsig(text: str) -> bytes:
    """Strict: 152 hexadecimal digits on one line, optionally followed by one line end."""
    line = text[:-1] if text.endswith("\n") else text
    if len(line) != IMGSIG_HEX_DIGITS or any(c not in "0123456789abcdefABCDEF" for c in line):
        raise TrustError(f"a .imgsig holds exactly {IMGSIG_HEX_DIGITS} hexadecimal digits on one line")
    return bytes.fromhex(line)


def sign_image(seed: bytes, header28: bytes, payload: bytes, counter: int) -> str:
    """The content of the ``.imgsig`` of an image. Needs a seed: a release machine or an ephemeral test key, never a file of this repository."""
    if not 0 <= counter <= 0xFFFFFFFF:
        raise TrustError("the counter is 32 bits")
    return encode_imgsig(ia.make_record(seed, header28, hashlib.sha256(payload).digest(), counter))


def verify_image(imgsig_text: str, table: list[tuple[bytes, int]], hardware_id: int, header28: bytes, payload: bytes, floor: int = 0,
                 installed_version: tuple[int, int, int, int] | None = None) -> str:
    """What a bootloader with the policy ``ed25519_only`` and this table decides; returns the status name (``ok`` or why not)."""
    try:
        record = decode_imgsig(imgsig_text)
    except TrustError:
        return "bad_args"
    status, _ = ia.decide(ia.POLICY_ED25519_ONLY, trusted_keys(table), hardware_id, floor, installed_version, False, header28,
                          hashlib.sha256(payload).digest(), record, False)
    return ia.STATUS_NAMES[status]


def public_of(seed: bytes) -> bytes:
    return ed25519.public_key(seed)


# ---- the confirmation of the master to the expansion slave bootloader (URTC firmware: src/common/urtc_slave_confirm.h) ------------------------------
SLAVE_CONFIRM_DOMAIN = b"URTC-SLV-CONF-V2"
SLAVE_CONFIRM_MESSAGE_SIZE = 80


def slave_confirm_message(nonce: bytes, image_sha256: bytes, hardware_id: int, version_major: int, version_minor_field: int, size: int, counter: int) -> bytes:
    """The 80-byte message both sides build: domain (16) | nonce (16) | image SHA-256 (32) | hardware id (4) | major (2) | minor field (2) | size (4) | counter (4).

    The slave bootloader holds no key, so the confirmation is the plain SHA-256 of this message (``slave_confirm_tag``): it binds the master's check to ONE update
    session (the nonce the slave published), ONE image and the signed rollback counter of its record. All integers are big-endian.
    """
    if len(nonce) != 16 or len(image_sha256) != 32:
        raise TrustError("the nonce is 16 bytes and the image digest 32")
    for value, bits in ((hardware_id, 32), (version_major, 16), (version_minor_field, 16), (size, 32), (counter, 32)):
        if not 0 <= value < 1 << bits:
            raise TrustError("a field of the confirmation is out of range")
    message = (SLAVE_CONFIRM_DOMAIN + nonce + image_sha256 + struct.pack(">IHHII", hardware_id, version_major, version_minor_field, size, counter))
    assert len(message) == SLAVE_CONFIRM_MESSAGE_SIZE
    return message


def slave_confirm_tag(*args: int | bytes) -> bytes:
    """The 32-byte confirmation written to ``REG_CONFIRM``: SHA-256 of ``slave_confirm_message(*args)``."""
    return hashlib.sha256(slave_confirm_message(*args)).digest()  # type: ignore[arg-type]
