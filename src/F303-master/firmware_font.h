// =============================================================================
// URTC Firmware - OLED font table declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_FONT_H
#define FIRMWARE_FONT_H

#include <stdint.h>

// 5 bytes per glyph, 64 glyphs (ASCII 32-95, space through '_'). Indexed as
// Font5x7[(c - 32) * 5] by the renderer - see firmware_render.c.
extern const uint8_t Font5x7[];

#endif // FIRMWARE_FONT_H
