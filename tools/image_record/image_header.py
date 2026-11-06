# =============================================================================
# URTC-COMMON - src/urtc_common/image_header.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The header in front of a firmware image: reference of ``firmware_base/urtc_image_header.h``.

32 bytes, big-endian like the bus: magic ``URIH``, header version 1, header size 32, flags, hardware id, the version as four components
(major.minor.patch.build), the length and the CRC-32 of the payload and the CRC-32 of the header itself. The signature of a release (HMAC-SHA256)
covers the first 28 bytes of the header and the payload, so the identity and the length it states cannot change without breaking it.

The C header and this module are held to the same vectors (``conformance/image_header_vectors.json``). Nothing puts the header in an image yet: that
is a change of the linker scripts and of the post-build step of each firmware.
"""
from __future__ import annotations

import hashlib
import hmac
import struct
import zlib
from dataclasses import dataclass

MAGIC = 0x55524948  # "URIH"
HEADER_VERSION = 1
HEADER_SIZE = 32
FLAG_SIGNED = 0x0001
SIGNATURE_SIZE = 32
SIGNED_HEADER = 28

OK, SHORT, BAD_MAGIC, BAD_VERSION, BAD_SIZE, BAD_HEADER_CRC, BAD_LENGTH, BAD_PAYLOAD_CRC = range(8)
STATUS_NAMES = ("ok", "short", "bad_magic", "bad_version", "bad_size", "bad_header_crc", "bad_length", "bad_payload_crc")

_LAYOUT = struct.Struct(">IBBHIHBBHHII")  # magic, version, size, flags, hardware id, major, minor, patch, build, reserved, length, payload crc


@dataclass(frozen=True)
class ImageInfo:
    hardware_id: int
    version_major: int
    version_minor: int
    version_patch: int
    version_build: int
    flags: int
    payload_length: int
    payload_crc32: int

    @property
    def version(self) -> tuple[int, int, int, int]:
        return (self.version_major, self.version_minor, self.version_patch, self.version_build)

    @property
    def signed(self) -> bool:
        return bool(self.flags & FLAG_SIGNED)

    def version_text(self) -> str:
        return ".".join(str(part) for part in self.version)


def pack_header(info: ImageInfo) -> bytes:
    """The 32 bytes of the header of ``info``, the header CRC included."""
    body = _LAYOUT.pack(MAGIC, HEADER_VERSION, HEADER_SIZE, info.flags, info.hardware_id, info.version_major, info.version_minor, info.version_patch,
                        info.version_build, 0, info.payload_length, info.payload_crc32)
    return body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def make_header(payload: bytes, hardware_id: int, version: tuple[int, int, int, int], signed: bool = False) -> bytes:
    """The header for ``payload``: length and CRC-32 computed from it."""
    major, minor, patch, build = version
    return pack_header(ImageInfo(hardware_id, major, minor, patch, build, FLAG_SIGNED if signed else 0, len(payload), zlib.crc32(payload) & 0xFFFFFFFF))


def parse_header(header: bytes) -> tuple[int, ImageInfo | None]:
    """(status, info): the info only when the magic, the version, the size and the header CRC hold. The payload is not looked at."""
    if len(header) < HEADER_SIZE:
        return SHORT, None
    magic, version, size, flags, hardware_id, major, minor, patch, build, _reserved, length, payload_crc = _LAYOUT.unpack(header[:SIGNED_HEADER])
    if magic != MAGIC:
        return BAD_MAGIC, None
    if version != HEADER_VERSION:
        return BAD_VERSION, None
    if size != HEADER_SIZE:
        return BAD_SIZE, None
    if struct.unpack(">I", header[SIGNED_HEADER:HEADER_SIZE])[0] != zlib.crc32(header[:SIGNED_HEADER]) & 0xFFFFFFFF:
        return BAD_HEADER_CRC, None
    return OK, ImageInfo(hardware_id, major, minor, patch, build, flags, length, payload_crc)


def verify_image(image: bytes) -> tuple[int, ImageInfo | None]:
    """Header and payload: the bytes must be the header, the payload it announces and, when signed, the 32-byte trailer, and the payload must have its CRC-32.
    The signature itself is not checked here (that needs the key)."""
    status, info = parse_header(image)
    if status != OK or info is None:
        return status, None
    trailer = SIGNATURE_SIZE if info.signed else 0
    if info.payload_length + HEADER_SIZE + trailer != len(image):
        return BAD_LENGTH, None
    if zlib.crc32(image[HEADER_SIZE:HEADER_SIZE + info.payload_length]) & 0xFFFFFFFF != info.payload_crc32:
        return BAD_PAYLOAD_CRC, None
    return OK, info


def signed_region(header: bytes, payload: bytes) -> bytes:
    """The bytes the HMAC of a release covers: the header without its own CRC, then the payload."""
    return bytes(header[:SIGNED_HEADER]) + bytes(payload)


def sign(key: bytes, header: bytes, payload: bytes) -> bytes:
    """HMAC-SHA256 over the signed region: the trailer of a signed image."""
    return hmac.new(key, signed_region(header, payload), hashlib.sha256).digest()


def build_image(payload: bytes, hardware_id: int, version: tuple[int, int, int, int], key: bytes | None = None) -> bytes:
    """Header + payload (+ signature trailer when a key is given)."""
    header = make_header(payload, hardware_id, version, signed=key is not None)
    return header + payload + (sign(key, header, payload) if key is not None else b"")
