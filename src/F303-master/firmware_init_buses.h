// =============================================================================
// URTC Firmware - Peripheral bus init declarations - I2C2, DMA, SPI1
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INIT_BUSES_H
#define FIRMWARE_INIT_BUSES_H

// MX_I2C2_Init is also declared in firmware_common.h (needed by the OLED
// driver module) - both declarations are identical, so including both
// headers in the same file is harmless.
void MX_I2C2_Init(void);
void MX_DMA_Init(void);
void MX_SPI1_Init(void);

#endif // FIRMWARE_INIT_BUSES_H
