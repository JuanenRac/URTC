// =============================================================================
// URTC Firmware - a synthetic MLX90640 calibration and frame for host-side tests: mlx_synthetic.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef HOST_MLX_SYNTHETIC_H
#define HOST_MLX_SYNTHETIC_H

#include <stdint.h>
#include <string.h>

#include "MLX90640_API.h"

// An EEPROM written word by word from the layout the extraction reads (the comments say which word means what), with the values of a nominal sensor: no
// row or column differences, the same pixel word everywhere, a supply voltage of 3.3 V and an ambient temperature near 37 degrees at the values the
// frame gives. It is not a real sensor's calibration, but it is a consistent one: the results have to be finite, monotonic and reproducible.
static inline void synthetic_eeprom(uint16_t *ee)
{
    memset(ee, 0, MLX90640_EEPROM_DUMP_NUM * sizeof *ee);
    ee[16] = 0x4321;   // alpha PTAT 9.0 (nibble 4), the offset scales of rows, columns and remainder (3, 2, 1)
    ee[17] = 0;        // offset reference
    ee[32] = 0x4000;   // the alpha scale (nibble 4 + 30) and no row or column scales
    ee[33] = 0x4000;   // alpha reference
    ee[48] = 0x1900;   // gain
    ee[49] = 0x6D60;   // PTAT at 25 degrees
    ee[50] = 0x4950;   // Kv PTAT and Kt PTAT
    ee[51] = 0xCCE0;   // Kvdd -52 (x32) and Vdd at 25 degrees
    ee[52] = 0x2222;   // Kv of the four pixel types
    ee[54] = 0x4040; ee[55] = 0x4040;   // Kta of the four pixel types
    ee[56] = 0x2A90;   // resolution 2, Kv scale 10, Kta scale 9 + 8
    ee[57] = 0x0040;   // compensation pixel alpha
    ee[58] = 0x0010;   // compensation pixel offset
    ee[59] = 0x2020;   // compensation pixel Kta and Kv
    ee[60] = 0x1E00;   // TGC 0 and Ks Ta
    ee[61] = 0xCCCC; ee[62] = 0xCCCC;   // Ks To of the four ranges
    ee[63] = 0x1A58;   // the range step, the corner temperatures and the Ks To scale
    for (int i = 64; i < 64 + 768; i++) ee[i] = 0x0020;   // a pixel word that is not zero and not flagged as an outlier
}

static inline void synthetic_frame(uint16_t *frame, uint16_t pixel)
{
    for (int i = 0; i < 768; i++) frame[i] = pixel;
    for (int i = 768; i < 832; i++) frame[i] = 0x0100;
    frame[768] = 0x0A00;                  // the VBE word
    frame[778] = 0x1900;                  // gain: the same as the EEPROM, so a factor of 1
    frame[800] = 0x2000;                  // PTAT
    frame[810] = 0xDC00;                  // Vdd at its 25-degree value (-9216): 3.3 V
    frame[832] = 0x1901;                  // control register: resolution 2, the one of the EEPROM
    frame[833] = 0;
}

#endif // HOST_MLX_SYNTHETIC_H
