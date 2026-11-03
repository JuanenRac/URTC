// =============================================================================
// URTC Bootloader - SHA-256 declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef BOOTLOADER_CRYPTO_H
#define BOOTLOADER_CRYPTO_H

#include <stdint.h>

// SHA-256 over an arbitrary flash region, read byte-by-byte rather than
// requiring the whole thing in RAM at once (the backup slot is 112KB; RAM
// here is 32KB total) - the flash controller supports direct addressed
// reads with no special unlock needed for that.
void sha256_flash_region(uint32_t flash_addr, uint32_t len, uint8_t out[32]);

// XOR-accumulate rather than an early-exit compare - an early exit leaks how
// many leading bytes matched through timing.
uint8_t digest_constant_time_compare(const uint8_t *a, const uint8_t *b, uint32_t len);

#endif // BOOTLOADER_CRYPTO_H
