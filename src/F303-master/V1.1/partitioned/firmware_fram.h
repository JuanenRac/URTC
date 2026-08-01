// =============================================================================
// URTC Firmware - FM24CL64B F-RAM parameter-persistence declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_FRAM_H
#define FIRMWARE_FRAM_H

#include <stdint.h>

// Low-level chunked F-RAM read/write. addr/len trusted to be within
// FRAM_SIZE_BYTES - internal helpers, not exposed over CAN directly.
// Return 1 on success, 0 on any HAL error.
uint8_t FRAM_WriteBytes(uint16_t addr, const uint8_t *data, uint16_t len);
uint8_t FRAM_ReadBytes(uint16_t addr, uint8_t *data, uint16_t len);

#endif // FIRMWARE_FRAM_H
