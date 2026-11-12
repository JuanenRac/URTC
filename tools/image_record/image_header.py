# =============================================================================
# URTC-COMMON - src/urtc_common/image_header.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The header in front of a firmware image: reference of ``firmware_base/urtc_image_header.h``.

32 bytes, big-endian like the bus: magic ``URIH``, header version 1, header size 32, flags, hardware id, the version as four components
(major.minor.patch.build), the length and the CRC-32 of the payload and the CRC-32 of the header itself. The signature of a release (HMAC-SHA256)
covers the first 28 bytes of the header and the payload, so the identity and the length it states cannot change without breaking it.

The C header and this module are held to the same vectors (``conformance/image_header_vectors.json``).

Two forms. The *flat* form is the header in front of a binary (a ``.urimg`` file, never flashed). The *embedded* form is the header INSIDE the flashed binary, at the
fixed offset ``EMBED_OFFSET`` (after the vector table, so the flash map and the vector table do not move): flag ``FLAG_EMBEDDED``, ``payload_length`` is the length of
the whole image and ``payload_crc32`` is the CRC-32 of the whole image with the 32 bytes of the header taken as zeros, so the header can be sealed after the link.
``inspect_image`` is what every consumer calls: it classifies a file and applies the compatibility switch (``accept``, ``warn``, ``require``). See
``conformance/image_header_embedded_vectors.json``.
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
FLAG_EMBEDDED = 0x0002   # the header sits inside the image at EMBED_OFFSET (no signature trailer is possible)
EMBED_OFFSET = 0x200     # after the vector table of the STM32F303 (0x188 bytes) and below the first code
SIGNATURE_SIZE = 32
SIGNED_HEADER = 28

OK, SHORT, BAD_MAGIC, BAD_VERSION, BAD_SIZE, BAD_HEADER_CRC, BAD_LENGTH, BAD_PAYLOAD_CRC, BAD_FLAGS, NOT_SEALED = range(10)
STATUS_NAMES = ("ok", "short", "bad_magic", "bad_version", "bad_size", "bad_header_crc", "bad_length", "bad_payload_crc", "bad_flags", "not_sealed")

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
    def embedded(self) -> bool:
        return bool(self.flags & FLAG_EMBEDDED)

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


# ---- the embedded form ----------------------------------------------------------------------------------------------------------------
def embedded_crc(image: bytes) -> int:
    """CRC-32 of the whole image with the 32 bytes of the embedded header taken as zeros (the value the header states)."""
    zeroed = bytes(image[:EMBED_OFFSET]) + bytes(HEADER_SIZE) + bytes(image[EMBED_OFFSET + HEADER_SIZE:])
    return zlib.crc32(zeroed) & 0xFFFFFFFF


def embedded_slot(hardware_id: int, version: tuple[int, int, int, int]) -> bytes:
    """What the firmware emits at EMBED_OFFSET: the identity it knows when it is compiled, length and CRC not yet known (the post-build step seals them).
    The header CRC is zero, so an image that was never sealed cannot pass for a sealed one."""
    major, minor, patch, build = version
    body = _LAYOUT.pack(MAGIC, HEADER_VERSION, HEADER_SIZE, FLAG_EMBEDDED, hardware_id, major, minor, patch, build, 0, 0, 0)
    return body + bytes(4)


def seal_embedded(binary: bytes, hardware_id: int | None = None, version: tuple[int, int, int, int] | None = None) -> bytes:
    """The binary with its slot sealed: length, CRC-32 and header CRC filled in. The slot must be the one the firmware emitted (magic, embedded flag); when
    ``hardware_id`` or ``version`` are given they must be the ones the firmware states. Raises ValueError with the reason otherwise. Same bytes every time."""
    if len(binary) < EMBED_OFFSET + HEADER_SIZE:
        raise ValueError("the binary is too short to hold the header slot")
    slot = bytes(binary[EMBED_OFFSET:EMBED_OFFSET + HEADER_SIZE])
    magic, hver, size, flags, hw, major, minor, patch, build, reserved, _length, _crc = _LAYOUT.unpack(slot[:SIGNED_HEADER])
    if magic != MAGIC or hver != HEADER_VERSION or size != HEADER_SIZE or not flags & FLAG_EMBEDDED or flags & FLAG_SIGNED or reserved:
        raise ValueError("there is no header slot at the fixed offset: the firmware does not emit one")
    if hardware_id is not None and hardware_id != hw:
        raise ValueError(f"the firmware states hardware 0x{hw:08X}, not 0x{hardware_id:08X}")
    if version is not None and version != (major, minor, patch, build):
        raise ValueError(f"the firmware states version {major}.{minor}.{patch}.{build}, not {'.'.join(str(v) for v in version)}")
    info = ImageInfo(hw, major, minor, patch, build, flags, len(binary), embedded_crc(binary))
    return bytes(binary[:EMBED_OFFSET]) + pack_header(info) + bytes(binary[EMBED_OFFSET + HEADER_SIZE:])


def verify_embedded(image: bytes) -> tuple[int, ImageInfo | None]:
    """The header at EMBED_OFFSET and the whole image: the header must hold, say it is embedded, be sealed, state the length of the image and the CRC-32 of it."""
    if len(image) < EMBED_OFFSET + HEADER_SIZE:
        return SHORT, None
    slot = bytes(image[EMBED_OFFSET:EMBED_OFFSET + HEADER_SIZE])
    if slot[28:] == bytes(4) and slot[:4] == struct.pack(">I", MAGIC):
        return NOT_SEALED, None
    status, info = parse_header(slot)
    if status != OK or info is None:
        return status, None
    if not info.embedded or info.signed:
        return BAD_FLAGS, None
    if info.payload_length != len(image):
        return BAD_LENGTH, None
    if embedded_crc(image) != info.payload_crc32:
        return BAD_PAYLOAD_CRC, None
    return OK, info


# ---- what a consumer does with a file: classify it and apply the compatibility switch ----------------------------------------------------
POLICY_ACCEPT, POLICY_WARN, POLICY_REQUIRE = "accept", "warn", "require"
POLICIES = (POLICY_ACCEPT, POLICY_WARN, POLICY_REQUIRE)
DEFAULT_POLICY = POLICY_WARN
KIND_EMBEDDED, KIND_LEGACY, KIND_STAMPED_FILE = "embedded", "legacy", "stamped_file"
# reasons: ok, no_header (accepted with a warning), header_required, stamped_file, wrong_hardware, wrong_version, or header_<status>
REASON_OK, REASON_NO_HEADER, REASON_REQUIRED, REASON_STAMPED, REASON_HARDWARE, REASON_VERSION = (
    "ok", "no_header", "header_required", "stamped_file", "wrong_hardware", "wrong_version")
_MAGIC_BYTES = struct.pack(">I", MAGIC)


def parse_policy(value: object) -> str:
    """The compatibility switch from a setting: a known word, else the strictest one (an unknown value never loosens the check)."""
    text = str(value).strip().lower() if value is not None else ""
    if text == "":
        return DEFAULT_POLICY
    return text if text in POLICIES else POLICY_REQUIRE


@dataclass(frozen=True)
class ImageCheck:
    accepted: bool
    kind: str
    reason: str
    info: ImageInfo | None = None

    @property
    def warn(self) -> bool:
        return self.accepted and self.reason == REASON_NO_HEADER


def inspect_image(data: bytes, policy: str = DEFAULT_POLICY, hardware_id: int | None = None, version: tuple[int, int, int, int] | None = None) -> ImageCheck:
    """Classify the bytes of a file about to be flashed. A header that is there is always checked and a bad one is always refused; an image without one is
    accepted or refused by ``policy``. ``hardware_id`` (and ``version``, three or four components: the first ones are compared) are what the caller expects: a header that states others is refused."""
    policy = parse_policy(policy)
    if bytes(data[:4]) == _MAGIC_BYTES:
        return ImageCheck(False, KIND_STAMPED_FILE, REASON_STAMPED)
    if bytes(data[EMBED_OFFSET:EMBED_OFFSET + 4]) == _MAGIC_BYTES:
        status, info = verify_embedded(data)
        if status != OK or info is None:
            return ImageCheck(False, KIND_EMBEDDED, "header_" + STATUS_NAMES[status])
        if hardware_id is not None and info.hardware_id != hardware_id:
            return ImageCheck(False, KIND_EMBEDDED, REASON_HARDWARE, info)
        if version is not None and info.version[:len(version)] != tuple(version):
            return ImageCheck(False, KIND_EMBEDDED, REASON_VERSION, info)
        return ImageCheck(True, KIND_EMBEDDED, REASON_OK, info)
    if policy == POLICY_REQUIRE:
        return ImageCheck(False, KIND_LEGACY, REASON_REQUIRED)
    return ImageCheck(True, KIND_LEGACY, REASON_OK if policy == POLICY_ACCEPT else REASON_NO_HEADER)
