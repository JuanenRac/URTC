// =============================================================================
// URTC Firmware - the Ed25519 verifier, compiled into the application
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The master verifies the signature record of the expansion slave's image (firmware_slave_attest.c) because the slave bootloader has no room for the
// verifier. This is the same shared source the master's bootloader compiles (src/common/urtc_ed25519.c, kept byte for byte by the lock file of that folder);
// the only thing added is the call that feeds the watchdog during a verification, which is two scalar multiplications long.
#include "firmware_common.h"

#define URTC_ED25519_YIELD() HAL_IWDG_Refresh(&hiwdg)
#include "urtc_ed25519.c"
