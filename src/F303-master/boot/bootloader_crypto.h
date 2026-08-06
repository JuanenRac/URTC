// =============================================================================
// URTC Bootloader - SHA-256 / HMAC-SHA256 declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef BOOTLOADER_CRYPTO_H
#define BOOTLOADER_CRYPTO_H

#include <stdint.h>

// HMAC-SHA256 over an arbitrary flash region, read byte-by-byte rather than
// requiring the whole thing in RAM at once (the backup slot is 112KB; RAM
// here is 32KB total) - the flash controller supports direct addressed
// reads with no special unlock needed for that.
void hmac_sha256_flash_region(const uint8_t *key, uint32_t key_len,
                              uint32_t flash_addr, uint32_t len,
                              uint8_t out[32]);

// XOR-accumulate rather than an early-exit compare - an early exit
// leaks how many leading bytes matched through timing, which matters
// for a signature check even on a small embedded target listening on a
// shared bus.
uint8_t hmac_constant_time_compare(const uint8_t *a, const uint8_t *b, uint32_t len);

#endif // BOOTLOADER_CRYPTO_H
