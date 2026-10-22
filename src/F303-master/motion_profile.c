// =============================================================================
// URTC Firmware - Stepper speed profiles: trapezoid and S-curve (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "motion_profile.h"

static uint32_t isqrt64(uint64_t n) {
    uint64_t x = n, y = (x + 1) / 2;
    if (n == 0) return 0;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return (uint32_t)x;
}

// Length of an S-curve ramp from v0 to v1 so that the steepest acceleration is a: the speed is v0 + (v1 - v0) * S(x) with the
// smooth-step S(x) = 3x^2 - 2x^3 of the fraction x of the distance, so the acceleration at x is (v0 + (v1 - v0) S) * (v1 - v0) * S'(x) / L.
// The largest value of (v0 + (v1 - v0) S) * S' is found by sampling (Q16 fixed point); L follows from it.
static uint32_t scurve_length(uint32_t v0, uint32_t v1, uint32_t a) {
    uint64_t delta = (uint64_t)(v1 - v0);
    uint64_t best = 0;
    for (uint64_t i = 1; i < 64; i++) {
        uint64_t x = i * 1024u;                                  // i/64 in Q16
        uint64_t x2 = (x * x) >> 16;
        uint64_t smooth = 3u * x2 - 2u * ((x2 * x) >> 16);       // S(x), Q16
        uint64_t slope = (6u * x * (65536u - x)) >> 16;          // S'(x), Q16
        uint64_t term = ((uint64_t)v0 * 65536u + delta * smooth) * slope;   // Q32 speed
        if (term > best) best = term;
    }
    uint64_t len = (delta * (best >> 16)) / ((uint64_t)a << 16);
    return (uint32_t)len;
}

static uint32_t ramp_length(uint32_t v0, uint32_t v1, uint32_t a, uint8_t profile) {
    if (profile == MP_SCURVE) return scurve_length(v0, v1, a);
    uint64_t d = ((uint64_t)v1 * v1 - (uint64_t)v0 * v0) / (2u * (uint64_t)a);
    return (uint32_t)(d + 1u);                                   // rounded up: the last step of the ramp reaches the cruise speed
}

uint8_t Mp_Plan(MpPlan_t *plan, uint32_t steps, uint32_t cruise_sps, uint32_t start_sps, uint32_t accel_sps2, uint8_t profile) {
    if (steps == 0 || cruise_sps == 0 || cruise_sps > MP_MAX_SPS || accel_sps2 == 0) return 0;
    if (start_sps == 0) start_sps = 1;
    if (start_sps > cruise_sps) start_sps = cruise_sps;
    plan->steps = steps;
    plan->start_sps = start_sps;
    plan->accel_sps2 = accel_sps2;
    plan->profile = (profile == MP_TRAPEZOID || profile == MP_SCURVE) ? profile : MP_CONSTANT;
    if (plan->profile == MP_CONSTANT) {
        plan->start_sps = plan->peak_sps = cruise_sps;
        plan->ramp_steps = 0;
        return 1;
    }
    uint32_t ramp = ramp_length(start_sps, cruise_sps, accel_sps2, plan->profile);
    plan->peak_sps = cruise_sps;
    if ((uint64_t)ramp * 2u > steps) {
        // too short to reach the cruise speed: two ramps meeting in the middle, the peak speed is what that distance allows
        ramp = steps / 2u;
        if (plan->profile == MP_TRAPEZOID) {
            plan->peak_sps = isqrt64((uint64_t)start_sps * start_sps + 2u * (uint64_t)accel_sps2 * ramp);
        } else {
            // the highest peak whose S-curve ramp still fits in `ramp` steps
            uint32_t lo = start_sps, hi = cruise_sps;
            for (int i = 0; i < 20 && lo < hi; i++) {
                uint32_t mid = lo + (hi - lo + 1u) / 2u;
                if (scurve_length(start_sps, mid, accel_sps2) <= ramp) lo = mid; else hi = mid - 1u;
            }
            plan->peak_sps = lo;
        }
        if (plan->peak_sps > cruise_sps) plan->peak_sps = cruise_sps;
        if (plan->peak_sps < start_sps) plan->peak_sps = start_sps;
    }
    plan->ramp_steps = ramp;
    return 1;
}

// Speed on a ramp of `len` steps at distance `s` from its slow end.
static uint32_t ramp_speed(const MpPlan_t *p, uint32_t s) {
    if (p->ramp_steps == 0 || p->peak_sps == p->start_sps) return p->peak_sps;
    if (p->profile == MP_TRAPEZOID) {
        uint64_t v2 = (uint64_t)p->start_sps * p->start_sps + 2u * (uint64_t)p->accel_sps2 * s;
        uint32_t v = isqrt64(v2);
        return v > p->peak_sps ? p->peak_sps : v;
    }
    // smooth-step 3x^2 - 2x^3 of the fraction x = s / ramp, in 16-bit fixed point
    uint64_t x = ((uint64_t)s << 16) / p->ramp_steps;
    if (x > 65536u) x = 65536u;
    uint64_t x2 = (x * x) >> 16;
    uint64_t x3 = (x2 * x) >> 16;
    uint64_t smooth = 3u * x2 - 2u * x3; // 0 .. 65536
    return p->start_sps + (uint32_t)(((uint64_t)(p->peak_sps - p->start_sps) * smooth) >> 16);
}

uint32_t Mp_SpeedAt(const MpPlan_t *plan, uint32_t index) {
    if (index >= plan->steps) return 0;
    if (plan->profile == MP_CONSTANT) return plan->peak_sps;
    uint32_t from_end = plan->steps - 1u - index;
    if (index < plan->ramp_steps && index <= from_end) return ramp_speed(plan, index);
    if (from_end < plan->ramp_steps) return ramp_speed(plan, from_end);
    return plan->peak_sps;
}

uint32_t Mp_IntervalUs(const MpPlan_t *plan, uint32_t index) {
    uint32_t v = Mp_SpeedAt(plan, index);
    return v == 0 ? 0 : 1000000u / v;
}

uint32_t Mp_TotalMs(const MpPlan_t *plan) {
    uint64_t us = 0;
    for (uint32_t i = 0; i < plan->steps; i++) us += Mp_IntervalUs(plan, i);
    return (uint32_t)(us / 1000u);
}

uint8_t Mp_ParseExtension(uint8_t dlc, const uint8_t *data, uint32_t *speed_sps, uint8_t *profile) {
    if (dlc != 8) return 0;
    *speed_sps = ((uint32_t)data[5] << 8) | data[6];
    *profile = data[7];
    return 1;
}
