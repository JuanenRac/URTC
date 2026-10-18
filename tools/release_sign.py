#!/usr/bin/env python3
# =============================================================================
# URTC - tools/release_sign.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Signs and verifies the assets of a URTC release with the Ed25519 key of the releases.

    python tools/release_sign.py public            the public key derived from the private key (64 hex digits)
    python tools/release_sign.py sign FILE...      writes FILE.sig (128 hex digits) next to each file
    python tools/release_sign.py verify FILE...    checks FILE.sig against the public key pinned below (no private key needed)

What is signed is the text `urtc-rel/1|<file name>|<sha256 of the file>`: the name carries the
component and the version, so a signed image cannot be passed off as another one. The private key
is never in a repository and is never printed: it is read from the file named by URTC_RELEASE_KEY,
or from ~/.urtc-keys/urtc_release_ed25519.pem. Signing needs the `cryptography` package; verifying
needs nothing but the standard library (the same verifier is shipped inside the flasher).
"""
from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

SCHEME = "urtc-rel/1"
PUBLIC_KEY_HEX = "4020c46d76c8ebacb64c5c7b9f3e7c62475afeb133d7421591988174e0f5dc90"

# --- Ed25519 verification (RFC 8032, standard library only) -------------------
_P = 2**255 - 19
_Q = 2**252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_SQRT_M1 = pow(2, (_P - 1) // 4, _P)


def _add(a, b):
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    pa = (y1 - x1) * (y2 - x2) % _P
    pb = (y1 + x1) * (y2 + x2) % _P
    pc = 2 * t1 * t2 * _D % _P
    pd = 2 * z1 * z2 % _P
    e, f, g, h = pb - pa, pd - pc, pd + pc, pb + pa
    return (e * f % _P, g * h % _P, f * g % _P, e * h % _P)


def _mul(scalar, point):
    result = (0, 1, 1, 0)
    while scalar > 0:
        if scalar & 1:
            result = _add(result, point)
        point = _add(point, point)
        scalar >>= 1
    return result


def _equal(a, b):
    return (a[0] * b[2] - b[0] * a[2]) % _P == 0 and (a[1] * b[2] - b[1] * a[2]) % _P == 0


def _recover_x(y, sign):
    if y >= _P:
        return None
    x2 = (y * y - 1) * pow(_D * y * y + 1, _P - 2, _P) % _P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P:
        x = x * _SQRT_M1 % _P
    if (x * x - x2) % _P:
        return None
    return _P - x if (x & 1) != sign else x


def _decompress(data):
    y = int.from_bytes(data, "little")
    sign, y = y >> 255, y & ((1 << 255) - 1)
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % _P)


_GY = 4 * pow(5, _P - 2, _P) % _P
_GX = _recover_x(_GY, 0)
_BASE = (_GX, _GY, 1, _GX * _GY % _P)


def ed25519_verify(public: bytes, message: bytes, signature: bytes) -> bool:
    """True only for a valid Ed25519 signature of `message` by the 32-byte key `public`."""
    if len(public) != 32 or len(signature) != 64:
        return False
    a_point, r_point = _decompress(public), _decompress(signature[:32])
    s = int.from_bytes(signature[32:], "little")
    if a_point is None or r_point is None or s >= _Q:
        return False
    h = int.from_bytes(hashlib.sha512(signature[:32] + public + message).digest(), "little") % _Q
    return _equal(_mul(s, _BASE), _add(r_point, _mul(h, a_point)))


# --- the release signature -----------------------------------------------------
def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def release_message(name: str, digest: str) -> bytes:
    if any(ch in name for ch in "|\r\n"):
        raise ValueError("a file name for a release signature cannot hold '|' or a line break")
    return f"{SCHEME}|{name}|{digest}".encode()


def verify_file(path: Path, signature_text: str | None = None, public_hex: str | None = None) -> bool:
    """True when `<path>.sig` (or `signature_text`) is the release signature of the file, by `public_hex`."""
    try:
        text = signature_text if signature_text is not None else Path(str(path) + ".sig").read_text(encoding="ascii")
        signature = bytes.fromhex(text.strip())
        return ed25519_verify(bytes.fromhex(public_hex or PUBLIC_KEY_HEX), release_message(path.name, sha256_file(path)), signature)
    except (OSError, ValueError):
        return False


def _private_key():
    try:
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        raise SystemExit("signing needs the `cryptography` package (pip install cryptography)")
    key_path = Path(os.environ.get("URTC_RELEASE_KEY") or Path.home() / ".urtc-keys" / "urtc_release_ed25519.pem")
    if not key_path.is_file():
        raise SystemExit("the release key was not found (set URTC_RELEASE_KEY or place it in ~/.urtc-keys/urtc_release_ed25519.pem)")
    return serialization.load_pem_private_key(key_path.read_bytes(), password=None)


def public_hex_of(key) -> str:
    from cryptography.hazmat.primitives import serialization
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def sign_file(path: Path, key=None) -> Path:
    """Writes `<path>.sig`. Refuses to sign with a key that is not the pinned one, since no tool would accept the result."""
    key = key or _private_key()
    if public_hex_of(key) != PUBLIC_KEY_HEX:
        raise SystemExit("the key that would sign is not the release key pinned in this repository")
    signature = key.sign(release_message(path.name, sha256_file(path))).hex()
    target = Path(str(path) + ".sig")
    target.write_text(signature + "\n", encoding="ascii", newline="\n")
    return target


def main(argv: list[str]) -> int:
    if not argv or argv[0] not in {"public", "sign", "verify"} or (argv[0] != "public" and len(argv) < 2):
        print(__doc__)
        return 2
    command, files = argv[0], [Path(item) for item in argv[1:]]
    if command == "public":
        print(public_hex_of(_private_key()))
        return 0
    if command == "sign":
        key = _private_key()
        for path in files:
            print(f"{sign_file(path, key).name}  ({path.name})")
        return 0
    failed = 0
    for path in files:
        good = verify_file(path)
        failed += 0 if good else 1
        print(("OK    " if good else "FAIL  ") + path.name)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
