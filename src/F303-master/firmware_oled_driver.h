// =============================================================================
// URTC Firmware - Low-level OLED (SSD1306/SSD1315) driver declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_OLED_DRIVER_H
#define FIRMWARE_OLED_DRIVER_H

#include <stdint.h>

// Low-level I2C2 transactions to the SSD1306/SSD1315 OLED controller.
// Every function here is a no-op if the display never responded to
// OLED_Init's own presence probe - callers don't need to check this
// themselves.

void OLED_WriteCmd(uint8_t cmd);
void OLED_WriteData(uint8_t *buf, uint16_t len);
void OLED_SetCursor(uint8_t page, uint8_t col);
void MX_I2C2_Init_Early(void);
void OLED_Init(void);

#endif // FIRMWARE_OLED_DRIVER_H
