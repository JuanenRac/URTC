// =============================================================================
// URTC Bootloader - the Ed25519 verifier, compiled with the other files of the bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The verifier itself is src/common/urtc_ed25519.c (shared with the other URTC programs and kept byte for byte by the lock file of that folder). The build
// scripts compile every .c file of this folder, so this one includes it; the only thing added is the call that feeds the watchdog while a verification runs
// (two scalar multiplications, longer than the watchdog period at the clock of the bootloader).
#include "stm32f3xx_hal.h"
#include "bootloader_common.h"

#define URTC_ED25519_YIELD() HAL_IWDG_Refresh(&hiwdg)
#include "urtc_ed25519.c"
