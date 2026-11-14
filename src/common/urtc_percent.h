// =============================================================================
// URTC Firmware - urtc_percent.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The percentage of a transfer or a copy, part * 100 / total, in 32-bit arithmetic only. The 64-bit product the bootloader used to take made the compiler link the
// library routine for 64-bit division (about 700 bytes of the 28 KB region of the main bootloader) for a figure that only moves a progress bar.
//
// The answer is the same as ((uint64_t)part * 100) / total cut to a byte whenever total is at most 42949672 (the largest total whose remainder times 100 still fits 32
// bits); a larger total (no image has one: the whole flash is 256 KB) is scaled down first, which moves the bar by a rounding step at most. A total of zero gives 0.
#ifndef URTC_PERCENT_H
#define URTC_PERCENT_H

#include <stdint.h>

#define URTC_PERCENT_EXACT_TOTAL 42949672u

static inline uint8_t urtc_percent_of(uint32_t part, uint32_t total)
{
    if (total == 0u) return 0u;
    while (total > URTC_PERCENT_EXACT_TOTAL) {
        total >>= 1;
        part >>= 1;
        if (total == 0u) return 0u;
    }
    const uint32_t whole = part / total;
    const uint32_t rest = part % total;
    return (uint8_t)(whole * 100u + (rest * 100u) / total);
}

#endif
