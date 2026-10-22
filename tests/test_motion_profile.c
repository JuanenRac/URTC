// =============================================================================
// URTC Firmware - Host logic tests for motion_profile.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The planner of the stepper ramps (trapezoid and S-curve): every step has a speed, the speed
// never exceeds the cruise speed, the acceleration stays at the configured value, the ramps
// are symmetric and a short move becomes a triangle. No hardware is involved.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "motion_profile.h"

// Largest acceleration between two consecutive steps, in steps/s^2, from the intervals alone.
static double peak_accel(const MpPlan_t *p, double *peak_jerk)
{
    double max_a = 0, max_j = 0, prev_a = 0;
    for (uint32_t i = 1; i < p->steps; i++) {
        double v0 = (double)Mp_SpeedAt(p, i - 1), v1 = (double)Mp_SpeedAt(p, i);
        double dt = 1.0 / ((v0 + v1) / 2.0);              // time of the step between them
        double a = (v1 - v0) / dt;
        double aa = a < 0 ? -a : a;
        if (aa > max_a) max_a = aa;
        if (i > 1) {
            double j = (a - prev_a) / dt;
            if (j < 0) j = -j;
            if (j > max_j) max_j = j;
        }
        prev_a = a;
    }
    if (peak_jerk) *peak_jerk = max_j;
    return max_a;
}

void run_motion_profile_tests(int *failures)
{
    MpPlan_t p;

    // a request that makes no sense
    TEST_ASSERT(!Mp_Plan(&p, 0, 500, 100, 2000, MP_TRAPEZOID), "no steps is refused");
    TEST_ASSERT(!Mp_Plan(&p, 100, 0, 100, 2000, MP_TRAPEZOID), "a cruise speed of 0 is refused");
    TEST_ASSERT(!Mp_Plan(&p, 100, MP_MAX_SPS + 1, 100, 2000, MP_TRAPEZOID), "a cruise speed above the maximum is refused");
    TEST_ASSERT(!Mp_Plan(&p, 100, 500, 100, 0, MP_TRAPEZOID), "no acceleration is refused");

    // constant speed: the behaviour of earlier releases, whatever the other numbers say
    TEST_ASSERT(Mp_Plan(&p, 50, 400, 100, 2000, MP_CONSTANT), "constant plan");
    int flat = 1;
    for (uint32_t i = 0; i < 50; i++) if (Mp_SpeedAt(&p, i) != 400) flat = 0;
    TEST_ASSERT(flat, "constant speed everywhere");
    TEST_ASSERT(Mp_SpeedAt(&p, 50) == 0 && Mp_IntervalUs(&p, 50) == 0, "no speed beyond the last step");
    TEST_ASSERT(Mp_Plan(&p, 50, 400, 100, 2000, 99) && p.profile == MP_CONSTANT, "an unknown profile is constant speed");
    TEST_ASSERT(Mp_IntervalUs(&p, 0) == 2500, "400 steps/s is 2500 us per step");

    // trapezoid, long enough to cruise
    TEST_ASSERT(Mp_Plan(&p, 2000, 800, 100, 2000, MP_TRAPEZOID), "trapezoid plan");
    TEST_ASSERT(Mp_SpeedAt(&p, 0) == 100, "starts at the start speed");
    TEST_ASSERT(Mp_SpeedAt(&p, 1999) == 100, "ends at the start speed");
    TEST_ASSERT(Mp_SpeedAt(&p, 1000) == 800, "cruises at the cruise speed");
    int monotone = 1, bounded = 1, symmetric = 1;
    for (uint32_t i = 1; i < p.ramp_steps; i++) if (Mp_SpeedAt(&p, i) < Mp_SpeedAt(&p, i - 1)) monotone = 0;
    for (uint32_t i = 0; i < 2000; i++) {
        if (Mp_SpeedAt(&p, i) > 800 || Mp_SpeedAt(&p, i) < 100) bounded = 0;
        if (Mp_SpeedAt(&p, i) != Mp_SpeedAt(&p, 1999 - i)) symmetric = 0;
    }
    TEST_ASSERT(monotone, "the speed never falls on the ramp up");
    TEST_ASSERT(bounded, "the speed stays between the start and the cruise speed");
    TEST_ASSERT(symmetric, "the ramp down mirrors the ramp up");
    TEST_ASSERT(p.ramp_steps == (800u * 800u - 100u * 100u) / 4000u + 1u, "ramp length from v^2 = v0^2 + 2 a s");
    double a_peak = peak_accel(&p, NULL);
    TEST_ASSERT(a_peak < 2000.0 * 1.35, "trapezoid: the acceleration stays at the configured value");
    TEST_ASSERT(a_peak > 2000.0 * 0.60, "trapezoid: and it really accelerates");

    // S-curve: same distance, the acceleration also stays bounded, and the jerk is lower than that of the trapezoid at its corners
    MpPlan_t s;
    TEST_ASSERT(Mp_Plan(&s, 2000, 800, 100, 2000, MP_SCURVE), "S-curve plan");
    TEST_ASSERT(s.ramp_steps > p.ramp_steps, "the S-curve ramp is longer");
    TEST_ASSERT(Mp_SpeedAt(&s, 0) == 100 && Mp_SpeedAt(&s, 1999) == 100 && Mp_SpeedAt(&s, 1000) == 800, "S-curve ends and cruise");
    double j_trap = 0, j_s = 0;
    double a_s = peak_accel(&s, &j_s);
    peak_accel(&p, &j_trap);
    TEST_ASSERT(a_s < 2000.0 * 1.35, "S-curve: the peak acceleration stays at the configured value");
    TEST_ASSERT(j_s < j_trap, "S-curve: smaller jerk than the trapezoid");
    int s_mono = 1, s_sym = 1;
    for (uint32_t i = 1; i < s.ramp_steps; i++) if (Mp_SpeedAt(&s, i) < Mp_SpeedAt(&s, i - 1)) s_mono = 0;
    for (uint32_t i = 0; i < 2000; i++) if (Mp_SpeedAt(&s, i) != Mp_SpeedAt(&s, 1999 - i)) s_sym = 0;
    TEST_ASSERT(s_mono && s_sym, "S-curve: monotone ramp, symmetric");
    TEST_ASSERT(Mp_TotalMs(&s) > Mp_TotalMs(&p), "the gentler ramp takes a little longer");
    TEST_ASSERT(Mp_TotalMs(&p) > 2000u * 1000u / 800u, "a ramp takes longer than cruising all the way");

    // short moves become a triangle and never exceed what the distance allows
    for (unsigned prof = MP_TRAPEZOID; prof <= MP_SCURVE; prof++) {
        TEST_ASSERT(Mp_Plan(&p, 40, 800, 100, 2000, (uint8_t)prof), "short plan");
        TEST_ASSERT(p.peak_sps < 800 && p.peak_sps >= 100, "a short move does not reach the cruise speed");
        TEST_ASSERT(p.ramp_steps * 2 <= 40, "the two ramps fit in the move");
        int ok = 1;
        for (uint32_t i = 0; i < 40; i++) if (Mp_SpeedAt(&p, i) > p.peak_sps || Mp_SpeedAt(&p, i) < 100) ok = 0;
        TEST_ASSERT(ok, "short move: speeds inside the limits");
        TEST_ASSERT(peak_accel(&p, NULL) < 2000.0 * 1.25, "short move: acceleration bounded");
    }
    TEST_ASSERT(Mp_Plan(&p, 1, 800, 100, 2000, MP_TRAPEZOID) && Mp_SpeedAt(&p, 0) >= 100, "a move of one step has a speed");
    TEST_ASSERT(Mp_Plan(&p, 2, 800, 100, 2000, MP_SCURVE) && Mp_SpeedAt(&p, 1) >= 100, "a move of two steps has speeds");

    // a start speed above the cruise speed is lowered to it (a flat plan)
    TEST_ASSERT(Mp_Plan(&p, 100, 300, 900, 2000, MP_TRAPEZOID) && Mp_SpeedAt(&p, 50) == 300 && Mp_SpeedAt(&p, 0) == 300, "start speed capped at the cruise speed");

    // every step of a long move, every profile: no zero interval, no overflow in the total
    for (unsigned prof = MP_CONSTANT; prof <= MP_SCURVE; prof++) {
        TEST_ASSERT(Mp_Plan(&p, 100000, MP_MAX_SPS, 1, 1, (uint8_t)prof), "extreme plan");
        int zero = 0;
        for (uint32_t i = 0; i < 100000; i += 997) if (Mp_SpeedAt(&p, i) == 0) zero = 1;
        TEST_ASSERT(!zero, "no step has speed 0");
    }

    // the 0x120 extension
    uint32_t speed;
    uint8_t profile;
    const uint8_t frame8[8] = { 1, 0, 0, 3, 0xE8, 0x03, 0x20, 2 };
    TEST_ASSERT(Mp_ParseExtension(8, frame8, &speed, &profile) && speed == 0x0320 && profile == 2, "eight bytes: speed 800, S-curve");
    TEST_ASSERT(!Mp_ParseExtension(5, frame8, &speed, &profile), "five bytes: no extension");
    TEST_ASSERT(!Mp_ParseExtension(7, frame8, &speed, &profile), "seven bytes: no extension");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - motion_profile.c\n");
    run_motion_profile_tests(&failures);
    if (failures == 0) {
        printf("  OK   all motion profile host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
