// =============================================================================
// URTC Firmware - WS2812B ring LED bit-bang driver declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_LED_H
#define FIRMWARE_LED_H

#include <stdint.h>

// Automatic status LED coloring (fault=red > host override > blue/green
// activity heuristic) - call every main loop iteration before sending.
void Update_StatusLED_AutoColor(void);

// Sends the current led_state_pixel out over SPI1+DMA. Returns 1 if a
// transfer was actually started, 0 if the previous one hasn't completed
// yet (caller should skip and retry next iteration).
uint8_t Update_StatusLED_SPI_DMA(void);

// Bit-banged WS2812B protocol for the 8-pixel ring on PB1.
void Update_RingLEDs_BitBang(void);

#endif // FIRMWARE_LED_H
