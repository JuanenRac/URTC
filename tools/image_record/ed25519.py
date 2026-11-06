# =============================================================================
# URTC-COMMON - src/urtc_common/ed25519.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Ed25519 (RFC 8032) in plain Python: the host reference of ``firmware_base/urtc_ed25519.c``.

Standard library only (``hashlib``). It is written for clarity and for checking the C verifier, not for speed or for protecting a secret key against
timing attacks: signing here belongs on the release machine, never on a device. ``verify`` is strict in the same ways the C code is: it refuses a
signature whose scalar is not below the group order and a public key or a ``R`` that is not a point of the curve.
"""
from __future__ import annotations

import hashlib

P = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
D = (-121665 * pow(121666, P - 2, P)) % P
_SQRT_M1 = pow(2, (P - 1) // 4, P)

Point = tuple[int, int, int, int]  # extended coordinates X, Y, Z, T


def _sha512(*parts: bytes) -> bytes:
    return hashlib.sha512(b"".join(parts)).digest()


def _add(a: Point, b: Point) -> Point:
    aa = (a[1] - a[0]) * (b[1] - b[0]) % P
    bb = (a[1] + a[0]) * (b[1] + b[0]) % P
    cc = 2 * a[3] * b[3] * D % P
    dd = 2 * a[2] * b[2] % P
    e, f, g, h = bb - aa, dd - cc, dd + cc, bb + aa
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(s: int, a: Point) -> Point:
    result: Point = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            result = _add(result, a)
        a = _add(a, a)
        s >>= 1
    return result


def _recover_x(y: int, sign: int) -> int | None:
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * _SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_GY = 4 * pow(5, P - 2, P) % P
_GX = _recover_x(_GY, 0)
assert _GX is not None
G: Point = (_GX, _GY, 1, _GX * _GY % P)


def compress(point: Point) -> bytes:
    zinv = pow(point[2], P - 2, P)
    x, y = point[0] * zinv % P, point[1] * zinv % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def decompress(data: bytes) -> Point | None:
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % P)


def _expand(seed: bytes) -> tuple[int, bytes]:
    h = _sha512(seed)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed: bytes) -> bytes:
    if len(seed) != 32:
        raise ValueError("an Ed25519 seed is 32 bytes")
    a, _ = _expand(seed)
    return compress(_mul(a, G))


def sign(seed: bytes, message: bytes) -> bytes:
    a, prefix = _expand(seed)
    pk = compress(_mul(a, G))
    r = int.from_bytes(_sha512(prefix, message), "little") % L
    big_r = compress(_mul(r, G))
    h = int.from_bytes(_sha512(big_r, pk, message), "little") % L
    s = (r + h * a) % L
    return big_r + int.to_bytes(s, 32, "little")


def verify(pk: bytes, message: bytes, signature: bytes) -> bool:
    """True only for a signature the C verifier also accepts."""
    if len(pk) != 32 or len(signature) != 64:
        return False
    a = decompress(pk)
    r = decompress(signature[:32])
    if a is None or r is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    h = int.from_bytes(_sha512(signature[:32], pk, message), "little") % L
    left = _mul(s, G)
    right = _add(r, _mul(h, a))
    return compress(left) == compress(right)
