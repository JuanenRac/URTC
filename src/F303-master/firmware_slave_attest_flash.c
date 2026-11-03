// =============================================================================
// URTC Firmware - where the master application finds the trust table of its own bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The only place the application reads the flash of the bootloader: the public keys that the bootloader trusts are also the ones that vouch for the
// expansion slave's images, so there is a single table to rotate (docs/SLAVE_BOOT_CONFIRMATION.md). Kept apart from firmware_slave_attest.c so that the
// host tests, which have no flash at that address, can link the rest.
#include "firmware_slave_attest.h"

void SlaveAttest_LoadTrustFromBootloader(void)
{
    // A table is at most 8 + 2 * 44 + 4 = 100 bytes; a closed or damaged one leaves the module with no key, and nothing is then ever confirmed.
    (void)SlaveAttest_SetKeysFromTable((const uint8_t *)SLAVE_ATTEST_TRUST_TABLE_ADDR, 100u);
}
