// =============================================================================
// URTC Firmware - Open-loop position of the solder wire feeder (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "spool_position.h"

int32_t SpoolPosition_Add(int32_t position, uint32_t steps, int forward)
{
    // 64 bits hold every sum: the position is 32 bits and so are the steps.
    int64_t moved = (int64_t)position + (forward ? (int64_t)steps : -(int64_t)steps);
    if (moved > (int64_t)INT32_MAX) {
        return INT32_MAX;
    }
    if (moved < (int64_t)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)moved;
}
