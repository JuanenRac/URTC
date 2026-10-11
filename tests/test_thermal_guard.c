// =============================================================================
// URTC Firmware - Host logic tests for thermal_guard.c (thermal runaway)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
// Drives ThermalGuard_Check() with a small simulated plant - a heater, a
// block with thermal mass losing heat to ambient, and a sensor that can
// come loose - through the same bang-bang decision firmware_control_thermal.c
// makes every 20 ms. Proves both directions: a healthy heater is never
// tripped (heat-up, holding, setpoint changes, a heavy load dip, the HAL
// tick wrapping), and a loose sensor IS caught, while heating and while
// holding, within the configured window.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "test_runner.h"
#include "thermal_guard.h"

#define TICK_MS 20u

typedef struct {
    double block_c;        // real temperature of the heater block
    double ambient_c;
    double heat_c_per_s;   // rise rate at full power (minus losses)
    double loss_per_s;     // fraction of (block - ambient) lost per second
    double extra_load_c_per_s; // e.g. soldering a large pad
    int sensor_loose;      // sensor reads ambient instead of the block
} Plant_t;

static uint16_t plant_read(const Plant_t *p)
{
    double v = p->sensor_loose ? p->ambient_c : p->block_c;
    if (v < 0) v = 0;
    return (uint16_t)(v + 0.5);
}

static void plant_step(Plant_t *p, int heater_on)
{
    const double dt = TICK_MS / 1000.0;
    double d = -p->loss_per_s * (p->block_c - p->ambient_c) * dt - p->extra_load_c_per_s * dt;
    if (heater_on) d += p->heat_c_per_s * dt;
    p->block_c += d;
}

typedef struct {
    ThermalGuard_t guard;
    uint8_t heater_on;
    uint32_t now_ms;
} Loop_t;

// One 20 ms control cycle exactly like firmware_control_thermal.c: bang-bang
// with +/-2 C hysteresis, then the guard, then (if OK) drive the heater.
static ThermalGuardFault_t loop_step(Loop_t *l, Plant_t *p, const ThermalGuardLimits_t *lim, uint16_t target)
{
    uint16_t current = plant_read(p);
    if ((int32_t)current < (int32_t)target - 2) l->heater_on = 1;
    else if ((int32_t)current > (int32_t)target + 2) l->heater_on = 0;
    ThermalGuardFault_t f = ThermalGuard_Check(&l->guard, lim, l->now_ms, l->heater_on, current, target);
    if (f != THERMAL_GUARD_OK) l->heater_on = 0;
    plant_step(p, l->heater_on);
    l->now_ms += TICK_MS;
    return f;
}

// Runs `seconds` of control; returns the first fault (or OK) and how long it took.
static ThermalGuardFault_t run_for(Loop_t *l, Plant_t *p, const ThermalGuardLimits_t *lim, uint16_t target,
                                   uint32_t seconds, uint32_t *fault_after_ms)
{
    uint32_t steps = seconds * 1000u / TICK_MS;
    uint32_t start = l->now_ms;
    for (uint32_t i = 0; i < steps; i++) {
        ThermalGuardFault_t f = loop_step(l, p, lim, target);
        if (f != THERMAL_GUARD_OK) {
            if (fault_after_ms) *fault_after_ms = l->now_ms - start;
            return f;
        }
    }
    return THERMAL_GUARD_OK;
}

// The same limits firmware_control_thermal.c uses.
static const ThermalGuardLimits_t T12 = {
    .max_temp_c = 449, .watch_period_ms = 20000, .watch_min_rise_c = 10, .watch_band_c = 20,
    .hold_period_ms = 45000, .hold_hysteresis_c = 40,
};
static const ThermalGuardLimits_t HOTEND = {
    .max_temp_c = 310, .watch_period_ms = 40000, .watch_min_rise_c = 2, .watch_band_c = 10,
    .hold_period_ms = 40000, .hold_hysteresis_c = 15,
};

static Plant_t t12_plant(void)    { Plant_t p = {25, 25, 60, 0.05, 0, 0}; return p; }
static Plant_t hotend_plant(void) { Plant_t p = {25, 25, 4.0, 0.006, 0, 0}; return p; }

static Loop_t new_loop(uint32_t start_ms)
{
    Loop_t l;
    ThermalGuard_Reset(&l.guard);
    l.heater_on = 0;
    l.now_ms = start_ms;
    return l;
}

void run_thermal_guard_tests(int *failures)
{
    uint32_t after = 0;

    // 1. Healthy T12: heat to 350, hold 5 minutes - never trips.
    {
        Plant_t p = t12_plant(); Loop_t l = new_loop(0);
        TEST_ASSERT(run_for(&l, &p, &T12, 350, 300, NULL) == THERMAL_GUARD_OK, "healthy T12 heat-up + 5 min hold never trips");
        TEST_ASSERT(p.block_c > 340 && p.block_c < 360, "healthy T12 actually regulates around 350");
    }

    // 2. Healthy hotend: heat to 240, hold 10 minutes - never trips.
    {
        Plant_t p = hotend_plant(); Loop_t l = new_loop(0);
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 240, 600, NULL) == THERMAL_GUARD_OK, "healthy hotend heat-up + 10 min hold never trips");
        TEST_ASSERT(p.block_c > 230 && p.block_c < 250, "healthy hotend actually regulates around 240");
    }

    // 3. Loose hotend thermistor from the start (reads ambient forever):
    //    caught by the heating watch within one watch period (+1 tick).
    {
        Plant_t p = hotend_plant(); p.sensor_loose = 1; Loop_t l = new_loop(0);
        ThermalGuardFault_t f = run_for(&l, &p, &HOTEND, 220, 120, &after);
        TEST_ASSERT(f == THERMAL_GUARD_HEATING_NO_RISE, "loose hotend sensor while heating -> HEATING_NO_RISE");
        TEST_ASSERT(after <= HOTEND.watch_period_ms + 2 * TICK_MS, "loose hotend sensor caught within one watch period");
    }

    // 4. Hotend thermistor falls out WHILE holding at target: caught by the
    //    holding check within its period, and the heater is off afterwards.
    {
        Plant_t p = hotend_plant(); Loop_t l = new_loop(0);
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 220, 300, NULL) == THERMAL_GUARD_OK, "hotend reaches and holds 220 first");
        p.sensor_loose = 1;
        ThermalGuardFault_t f = run_for(&l, &p, &HOTEND, 220, 300, &after);
        TEST_ASSERT(f == THERMAL_GUARD_DROPPED_AT_TARGET, "sensor falling out while holding -> DROPPED_AT_TARGET");
        TEST_ASSERT(after <= HOTEND.hold_period_ms + 2 * TICK_MS, "fall-out caught within one hold period");
        TEST_ASSERT(l.heater_on == 0, "heater forced off after the fault");
    }

    // 5. Loose T12 contact from the start: caught within the watch period.
    {
        Plant_t p = t12_plant(); p.sensor_loose = 1; Loop_t l = new_loop(0);
        ThermalGuardFault_t f = run_for(&l, &p, &T12, 350, 60, &after);
        TEST_ASSERT(f == THERMAL_GUARD_HEATING_NO_RISE, "loose T12 sensor while heating -> HEATING_NO_RISE");
        TEST_ASSERT(after <= T12.watch_period_ms + 2 * TICK_MS, "loose T12 sensor caught within one watch period");
    }

    // 6. A heavy load (big pad) pulls the T12 down 25 C for a while: within
    //    the 40 C hysteresis, must NOT trip.
    {
        Plant_t p = t12_plant(); Loop_t l = new_loop(0);
        TEST_ASSERT(run_for(&l, &p, &T12, 350, 60, NULL) == THERMAL_GUARD_OK, "T12 at temperature before the load");
        p.extra_load_c_per_s = 45; // the heater can still keep up, but with a real dip
        TEST_ASSERT(run_for(&l, &p, &T12, 350, 60, NULL) == THERMAL_GUARD_OK, "heavy soldering load does not trip the guard");
        p.extra_load_c_per_s = 0;
        TEST_ASSERT(run_for(&l, &p, &T12, 350, 30, NULL) == THERMAL_GUARD_OK, "recovery after the load does not trip");
    }

    // 7. Setpoint raised from 200 to 300 after holding: a fresh heating
    //    phase, not a "drop at target".
    {
        Plant_t p = hotend_plant(); Loop_t l = new_loop(0);
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 200, 300, NULL) == THERMAL_GUARD_OK, "hold at 200");
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 290, 400, NULL) == THERMAL_GUARD_OK, "raising the setpoint to 290 does not trip");
        TEST_ASSERT(p.block_c > 280, "and the block really reaches the new setpoint");
    }

    // 8. Setpoint lowered (cool-down with the heater off): never a fault.
    {
        Plant_t p = hotend_plant(); Loop_t l = new_loop(0);
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 250, 300, NULL) == THERMAL_GUARD_OK, "hold at 250");
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 150, 600, NULL) == THERMAL_GUARD_OK, "cooling down to 150 does not trip");
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 0, 300, NULL) == THERMAL_GUARD_OK, "heater commanded off does not trip");
    }

    // 9. MAXTEMP on the measured value (stuck MOSFET overshooting).
    {
        ThermalGuard_t g; ThermalGuard_Reset(&g);
        TEST_ASSERT(ThermalGuard_Check(&g, &HOTEND, 0, 0, 311, 200) == THERMAL_GUARD_MAXTEMP, "311 C on the hotend -> MAXTEMP");
        TEST_ASSERT(ThermalGuard_Check(&g, &HOTEND, 0, 0, 310, 200) == THERMAL_GUARD_OK, "310 C is still allowed");
        TEST_ASSERT(ThermalGuard_Check(&g, &HOTEND, 0, 0, 400, 0) == THERMAL_GUARD_MAXTEMP, "MAXTEMP applies even with the heater commanded off");
    }

    // 10. HAL tick wrapping past 2^32 in the middle of a heat-up.
    {
        Plant_t p = hotend_plant(); Loop_t l = new_loop(0xFFFFFFFFu - 15000u);
        TEST_ASSERT(run_for(&l, &p, &HOTEND, 240, 400, NULL) == THERMAL_GUARD_OK, "healthy heat-up across a tick wrap never trips");
        Plant_t q = hotend_plant(); q.sensor_loose = 1; Loop_t m = new_loop(0xFFFFFFFFu - 15000u);
        TEST_ASSERT(run_for(&m, &q, &HOTEND, 240, 120, NULL) == THERMAL_GUARD_HEATING_NO_RISE, "loose sensor across a tick wrap is still caught");
    }

    // 11. Reset forgets everything.
    {
        ThermalGuard_t g; ThermalGuard_Reset(&g);
        (void)ThermalGuard_Check(&g, &HOTEND, 0, 1, 25, 200);
        ThermalGuard_Reset(&g);
        TEST_ASSERT(g.watch_armed == 0 && g.reached_target == 0 && g.last_target_c == 0, "reset clears the guard state");
    }
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - thermal_guard.c\n");
    run_thermal_guard_tests(&failures);
    if (failures == 0) {
        printf("  OK   all thermal guard host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
