// =============================================================================
// URTC-COMMON - firmware_base/urtc_ed25519.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed: this is portable C that the host tests exercise (tests/c/test_ed25519.c, against RFC 8032 and the shared
// vectors in conformance/ed25519_vectors.json). Nothing in a bootloader calls it yet.
//
// Ed25519 signature VERIFICATION (RFC 8032) for the bootloaders: no signing, no secret key, no heap, no tables that depend on the data. The field and group
// arithmetic follows the compact TweetNaCl design (D. J. Bernstein, T. Lange, P. Schwabe and others, public domain) in 16 limbs of 16 bits; it was
// rewritten here, checked against the RFC 8032 vectors and against the Python reference (src/urtc_common/ed25519.py), and adds what the original leaves to the
// caller: the scalar of the signature must be below the group order, and a public key whose y coordinate is not reduced is refused. The SHA-512 it needs is
// included. See docs/BOOTLOADER_AUTH.md for the licence, the size and the time budget.
//
// Time: one verification is two scalar multiplications; every 8 bits of the scalar the code calls URTC_ED25519_YIELD(), which a bootloader defines to feed
// its watchdog (the default does nothing).
#ifndef URTC_ED25519_H
#define URTC_ED25519_H

#include <stddef.h>
#include <stdint.h>

#ifndef URTC_ED25519_YIELD
#define URTC_ED25519_YIELD() ((void)0)
#endif

#define URTC_ED25519_PUBLIC_KEY_SIZE 32u
#define URTC_ED25519_SIGNATURE_SIZE  64u
#define URTC_SHA512_DIGEST_SIZE      64u

typedef struct {
    uint64_t h[8];
    uint8_t block[128];
    uint32_t used;        // bytes waiting in block
    uint64_t total;       // bytes fed so far
} urtc_sha512_t;

void urtc_sha512_init(urtc_sha512_t *ctx);
void urtc_sha512_update(urtc_sha512_t *ctx, const uint8_t *data, size_t length);
void urtc_sha512_final(urtc_sha512_t *ctx, uint8_t out[URTC_SHA512_DIGEST_SIZE]);
void urtc_sha512(uint8_t out[URTC_SHA512_DIGEST_SIZE], const uint8_t *data, size_t length);

// Returns 0 when `signature` is a valid Ed25519 signature of `message` under `public_key`, and a non-zero value for every other case (a bad length of
// argument, a point that is not on the curve, a scalar at or above the group order, a signature that does not match). Never reads past its arguments.
int urtc_ed25519_verify(const uint8_t signature[URTC_ED25519_SIGNATURE_SIZE], const uint8_t *message, size_t message_length,
                        const uint8_t public_key[URTC_ED25519_PUBLIC_KEY_SIZE]);

#endif // URTC_ED25519_H
