// =============================================================================
// URTC Firmware - Open-loop position of the solder wire feeder (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The feeder has no encoder: its position is the sum of the steps that were commanded, forward minus backward. A command carries up to 2^32 - 1 steps, so
// the plain addition could pass the range of the 32-bit position, which is undefined behaviour in C and in practice wraps to the other sign. This adds with
// saturation: the position stops at the end of its range instead of jumping to the other end.
#ifndef SPOOL_POSITION_H
#define SPOOL_POSITION_H

#include <stdint.h>

// forward != 0 adds the steps, forward == 0 subtracts them; the result stays inside the range of int32_t.
int32_t SpoolPosition_Add(int32_t position, uint32_t steps, int forward);

#endif // SPOOL_POSITION_H
