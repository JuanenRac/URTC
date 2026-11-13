// =============================================================================
// URTC Firmware - Settling time of the T12 thermocouple reading (pure logic, no HAL)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The heater current and the thermocouple of a T12 cartridge share two conductors, so each 20 ms cycle the heater is switched off and the thermocouple is read after
// a short wait for the line and the amplifier to recover. The wait is a build option, in microseconds:
//   URTC_T12_SETTLE_US  wait between switching the heater off and starting the conversion (default 10, the wait the firmware has always had);
// a cartridge whose amplifier needs longer (a few hundred microseconds to a few milliseconds is usual) builds with a larger value. The wait blocks the control
// pass, so it is bounded: a build above URTC_T12_SETTLE_MAX_US does not compile. The value to use is the one measured with an oscilloscope on the amplifier output.
// =============================================================================
#ifndef URTC_T12_SETTLE_H
#define URTC_T12_SETTLE_H

#include <stdint.h>

#define URTC_T12_SETTLE_MAX_US 5000u      // a quarter of the 20 ms control cycle at most
#define URTC_T12_LOOPS_PER_US 64u         // passes of the empty counting loop per microsecond at 64 MHz (640 passes were the 10 microseconds of the first version)

#ifndef URTC_T12_SETTLE_US
#define URTC_T12_SETTLE_US 10u
#endif

#if URTC_T12_SETTLE_US > URTC_T12_SETTLE_MAX_US
#error "URTC_T12_SETTLE_US is above URTC_T12_SETTLE_MAX_US"
#endif

// The passes of the counting loop for a wait of `us` microseconds, never more than the maximum.
static inline uint32_t urtc_t12_settle_loops(uint32_t us)
{
    if (us > URTC_T12_SETTLE_MAX_US) us = URTC_T12_SETTLE_MAX_US;
    return us * URTC_T12_LOOPS_PER_US;
}

#endif // URTC_T12_SETTLE_H
