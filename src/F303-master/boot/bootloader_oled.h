// =============================================================================
// URTC Bootloader - Boot-screen OLED renderer declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef BOOTLOADER_OLED_H
#define BOOTLOADER_OLED_H

#include <stdint.h>

// Every function here checks an internal oled_present flag first (set once
// by OLED_Init_Boot via HAL_I2C_IsDeviceReady) and returns immediately if
// the display never responded, rather than paying a real I2C timeout on
// every single command for the rest of the session.

void OLED_Init_Boot(void);
void OLED_ClearAll_Boot(void);
void OLED_PrintStr2x_Boot(uint8_t page, uint8_t col, const char *str);
void OLED_DrawProgressBar_Boot(uint8_t percent);

#endif // BOOTLOADER_OLED_H
