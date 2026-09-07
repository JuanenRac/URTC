// =============================================================================
// URTC Firmware - OLED screen rendering declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_RENDER_H
#define FIRMWARE_RENDER_H

#include <stdint.h>

// 4 frames x 5 OLED pages x 128 bytes each - the boot splash face used both
// by Render_SplashScreen below and by main()'s own boot-time animation
// cycling loop, which indexes this directly rather than calling
// Render_SplashScreen repeatedly (needs finer control over the frame timing
// than that function's own single-call-per-frame interface provides).
extern const uint8_t SplashFace[4][5][128];

void OLED_PrintStr(uint8_t page, uint8_t col, const char* str);
void OLED_ClearPage(uint8_t page);
void OLED_DrawHorizontalBar(uint8_t page, uint8_t col_start, uint8_t max_width, uint8_t percent);
void OLED_DrawTempSparkline(uint8_t page, uint8_t col_start, uint8_t width, uint16_t current_value, uint16_t min_value, uint16_t max_value);
void OLED_DrawToolIcon(uint8_t tool);
void OLED_Render_YellowStrip(void);
void Render_SplashScreen(uint8_t face_frame);
void Process_OLED_NightModeChange(void);
void Render_ToolScreen(void);

#endif // FIRMWARE_RENDER_H
