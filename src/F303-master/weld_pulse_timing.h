// =============================================================================
// URTC Firmware - limits and timer arithmetic of the weld / soldering pulse (pure logic, no HAL)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The pulse that fires the spot welder, the ultrasonic welder and the soldering pulse lasts as long as the CAN command says, clamped. By default the end
// of the pulse is decided in the main loop (WeldPulse_Tick), so its length can be late by one pass of the loop. With URTC_WELD_PULSE_HW_TIMER=1 a
// hardware timer in one-pulse mode also ends it, in its own interrupt, independent of the main loop. The duration limits are configuration:
//   URTC_WELD_PULSE_MAX_MS  longest pulse accepted (default 2000, the value the firmware has always used);
//   URTC_WELD_PULSE_MIN_MS  shortest pulse accepted (default 0, as before);
// and URTC_WELD_PULSE_HARD_MAX_MS is the ceiling of the ceiling: a build that sets the maximum above it does not compile.
// =============================================================================
#ifndef URTC_WELD_PULSE_TIMING_H
#define URTC_WELD_PULSE_TIMING_H

#include <stdint.h>

#define URTC_WELD_PULSE_HARD_MAX_MS 5000u

#ifndef URTC_WELD_PULSE_MAX_MS
#define URTC_WELD_PULSE_MAX_MS 2000u
#endif
#ifndef URTC_WELD_PULSE_MIN_MS
#define URTC_WELD_PULSE_MIN_MS 0u
#endif
#ifndef URTC_WELD_PULSE_HW_TIMER
#define URTC_WELD_PULSE_HW_TIMER 0
#endif

#if URTC_WELD_PULSE_MAX_MS > URTC_WELD_PULSE_HARD_MAX_MS
#error "URTC_WELD_PULSE_MAX_MS is above the hard limit URTC_WELD_PULSE_HARD_MAX_MS"
#endif
#if URTC_WELD_PULSE_MIN_MS > URTC_WELD_PULSE_MAX_MS
#error "URTC_WELD_PULSE_MIN_MS is above URTC_WELD_PULSE_MAX_MS"
#endif

// The duration actually used for a requested one: inside [min, max], with the hard limit applied whatever the configuration.
static inline uint16_t urtc_weld_clamp_ms(uint16_t raw_ms)
{
    uint32_t v = raw_ms;
    if (v > URTC_WELD_PULSE_MAX_MS) v = URTC_WELD_PULSE_MAX_MS;
    if (v > URTC_WELD_PULSE_HARD_MAX_MS) v = URTC_WELD_PULSE_HARD_MAX_MS;
    if (v < URTC_WELD_PULSE_MIN_MS) v = URTC_WELD_PULSE_MIN_MS;
    return (uint16_t)v;
}

// One-pulse timer of 16 bits: the counter runs at 10 kHz (a tick of 100 microseconds), which covers 6553 ms, more than the hard limit.
// Gives the prescaler and the auto-reload register (both "value - 1" as the registers want them). Returns 0 on success, 1 for a clock that cannot make
// 10 kHz exactly enough (below 10 kHz or a prescaler above 16 bits) or a duration of 0 or above the hard limit.
static inline int urtc_weld_timer_params(uint32_t timer_hz, uint16_t duration_ms, uint32_t *psc, uint32_t *arr)
{
    if (timer_hz < 10000u || duration_ms == 0 || duration_ms > URTC_WELD_PULSE_HARD_MAX_MS) return 1;
    uint32_t div = timer_hz / 10000u;
    if (div == 0 || div > 65536u) return 1;
    *psc = div - 1u;
    *arr = (uint32_t)duration_ms * 10u - 1u;
    return 0;
}

#endif // URTC_WELD_PULSE_TIMING_H
