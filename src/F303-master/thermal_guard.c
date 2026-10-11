// =============================================================================
// URTC Firmware - Thermal runaway guard (pure C, no STM32 HAL): thermal_guard.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
// See thermal_guard.h for what this adds on top of the existing checks in
// firmware_control_thermal.c, and tests/test_thermal_guard.c for the
// scenarios it is proven against (normal heat-up, loose sensor while
// heating, loose sensor while holding, setpoint changes, tick wrap).
#include "thermal_guard.h"

void ThermalGuard_Reset(ThermalGuard_t *g)
{
    g->last_target_c = 0;
    g->watch_armed = 0;
    g->watch_start_ms = 0;
    g->watch_start_temp_c = 0;
    g->reached_target = 0;
    g->sag_armed = 0;
    g->sag_start_ms = 0;
}

ThermalGuardFault_t ThermalGuard_Check(ThermalGuard_t *g, const ThermalGuardLimits_t *lim,
                                       uint32_t now_ms, uint8_t heater_on,
                                       uint16_t current_c, uint16_t target_c)
{
    // MAXTEMP on the measured value, whatever the heater is doing.
    if (current_c > lim->max_temp_c) {
        return THERMAL_GUARD_MAXTEMP;
    }

    // A new setpoint (or the heater being commanded off entirely) starts
    // over: raising the target legitimately drops us back into a heating
    // phase, and lowering it legitimately means a long cool-down.
    if (target_c != g->last_target_c) {
        ThermalGuard_Reset(g);
        g->last_target_c = target_c;
    }
    if (target_c == 0) {
        return THERMAL_GUARD_OK;
    }

    const uint32_t band = lim->watch_band_c;
    const uint8_t far_below = ((uint32_t)current_c + band) < (uint32_t)target_c;

    if (!far_below) {
        g->reached_target = 1;
        g->watch_armed = 0;
    }

    // Heating phase (WATCH_TEMP): only judged while the heater really is on
    // and we are still far below the target. Each window that shows the
    // required rise re-arms from the new temperature.
    if (!g->reached_target && heater_on && far_below) {
        if (!g->watch_armed) {
            g->watch_armed = 1;
            g->watch_start_ms = now_ms;
            g->watch_start_temp_c = current_c;
        } else if ((uint32_t)(now_ms - g->watch_start_ms) >= lim->watch_period_ms) {
            if ((uint32_t)current_c < (uint32_t)g->watch_start_temp_c + lim->watch_min_rise_c) {
                return THERMAL_GUARD_HEATING_NO_RISE;
            }
            g->watch_start_ms = now_ms;
            g->watch_start_temp_c = current_c;
        }
    } else if (!heater_on) {
        // The heater being off is not "failing to heat" - judge the next
        // heating stretch on its own.
        g->watch_armed = 0;
    }

    // Holding phase (THERMAL_PROTECTION): once the target was reached, a
    // sag of more than the hysteresis that persists for the whole hold
    // period while the loop keeps heating means the sensor no longer sees
    // the heater.
    if (g->reached_target) {
        const uint8_t sagging = ((uint32_t)current_c + lim->hold_hysteresis_c) < (uint32_t)target_c;
        if (sagging && heater_on) {
            if (!g->sag_armed) {
                g->sag_armed = 1;
                g->sag_start_ms = now_ms;
            } else if ((uint32_t)(now_ms - g->sag_start_ms) >= lim->hold_period_ms) {
                return THERMAL_GUARD_DROPPED_AT_TARGET;
            }
        } else if (!sagging) {
            g->sag_armed = 0;
        }
    }

    return THERMAL_GUARD_OK;
}
