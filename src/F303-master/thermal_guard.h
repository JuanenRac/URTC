// =============================================================================
// URTC Firmware - Thermal runaway guard (pure C, no STM32 HAL): thermal_guard.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The two bang-bang loops in firmware_control_thermal.c already catch a
// sensor pinned to a rail (open/shorted), an out-of-range setpoint and a
// heater that keeps heating while switched OFF (stuck MOSFET/SSR). None of
// those catches the classic thermal runaway: the sensor comes loose from
// the block (or the T12 tip loses contact) and keeps reading a plausible
// room temperature, so the loop keeps asking for heat and the heater runs
// at 100% with no limit. This guard is the same two-part protection
// Marlin calls WATCH_TEMP (heating must actually raise the temperature)
// and THERMAL_PROTECTION (once at the setpoint, the temperature must not
// sag far below it while the heater keeps pushing), plus a MAXTEMP on the
// measured value itself, not only on the requested setpoint.
//
// Pure C on purpose, like firmware_can_dispatch_routing.c: no HAL, no
// globals - every call gets the guard state, the limits and the current
// tick, so tests/test_thermal_guard.c exercises it on the host with a
// simulated heater, block and loose sensor.
#ifndef THERMAL_GUARD_H
#define THERMAL_GUARD_H

#include <stdint.h>

typedef enum {
    THERMAL_GUARD_OK = 0,
    THERMAL_GUARD_MAXTEMP = 1,          // measured temperature above the hard ceiling
    THERMAL_GUARD_HEATING_NO_RISE = 2,  // heater on while far below target, but no real rise
    THERMAL_GUARD_DROPPED_AT_TARGET = 3 // reached the target, then sagged with the heater on
} ThermalGuardFault_t;

typedef struct {
    uint16_t max_temp_c;          // hard ceiling on the MEASURED temperature
    uint32_t watch_period_ms;     // heating phase: window in which...
    uint16_t watch_min_rise_c;    // ...the temperature must rise at least this much
    uint16_t watch_band_c;        // heating phase = more than this below the target
    uint32_t hold_period_ms;      // holding phase: how long a sag is tolerated
    uint16_t hold_hysteresis_c;   // holding phase: how far below target counts as a sag
} ThermalGuardLimits_t;

typedef struct {
    uint16_t last_target_c;
    uint8_t  watch_armed;
    uint32_t watch_start_ms;
    uint16_t watch_start_temp_c;
    uint8_t  reached_target;
    uint8_t  sag_armed;
    uint32_t sag_start_ms;
} ThermalGuard_t;

// Forget everything (call when the tool's loop is inactive or after a fault
// has been handled) - the next check starts a fresh heating window.
void ThermalGuard_Reset(ThermalGuard_t *g);

// One check per control cycle, AFTER the loop has decided `heater_on` for
// this cycle and BEFORE driving the pin. A non-OK result means: force the
// heater off and raise the board's system error.
ThermalGuardFault_t ThermalGuard_Check(ThermalGuard_t *g, const ThermalGuardLimits_t *lim,
                                       uint32_t now_ms, uint8_t heater_on,
                                       uint16_t current_c, uint16_t target_c);

#endif // THERMAL_GUARD_H
