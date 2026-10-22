// =============================================================================
// URTC Firmware - Stepper speed profiles: trapezoid and S-curve (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Plans one move of N steps: speed as a function of the step index, with a ramp up, a cruise
// and a ramp down. No timer and no HAL here; the glue that would load the interval of each
// step into the step timer is not wired (it needs a timing bench), so with the MOTION_RAMPS
// switch on the plan is computed and published but the step generator is unchanged.
//
//   trapezoid: constant acceleration, v(s) = sqrt(v0^2 + 2 a s)
//   S-curve:   the speed follows a smooth-step of the distance (zero slope at both ends), so
//              the acceleration itself rises and falls: no jump of acceleration at the start
//              or at the end of a ramp. Its ramp is longer so that the peak acceleration
//              stays at the configured value.
#ifndef MOTION_PROFILE_H
#define MOTION_PROFILE_H

#include <stdint.h>

#define MP_CONSTANT 0u
#define MP_TRAPEZOID 1u
#define MP_SCURVE 2u

#define MP_DEFAULT_START_SPS   100u
#define MP_DEFAULT_CRUISE_SPS  500u   // what the 1 kHz step interrupt does today (one step per 2 ms)
#define MP_DEFAULT_ACCEL_SPS2  2000u
#define MP_MAX_SPS             20000u

typedef struct {
    uint32_t steps;
    uint32_t start_sps;      // speed of the first step
    uint32_t peak_sps;       // the speed actually reached (below the cruise speed on a short move)
    uint32_t accel_sps2;
    uint32_t ramp_steps;     // steps of the ramp up; the ramp down has as many
    uint8_t  profile;
} MpPlan_t;

// 1 when the plan was made; 0 for a request that makes no sense (no steps, a speed of 0 or above the maximum, no acceleration).
// A profile of MP_CONSTANT (or an unknown one) gives a plan whose speed is the cruise speed everywhere.
uint8_t Mp_Plan(MpPlan_t *plan, uint32_t steps, uint32_t cruise_sps, uint32_t start_sps, uint32_t accel_sps2, uint8_t profile);

// Speed in steps per second at the start of step `index` (0-based); 0 beyond the last step.
uint32_t Mp_SpeedAt(const MpPlan_t *plan, uint32_t index);

// Time of that step in microseconds, 1000000 / speed (0 beyond the last step).
uint32_t Mp_IntervalUs(const MpPlan_t *plan, uint32_t index);

// Total time of the move in milliseconds, summing every interval (for the tests and for the host).
uint32_t Mp_TotalMs(const MpPlan_t *plan);

// The fixed 0x120 form: bytes 5 and 6 are the cruise speed (big-endian), byte 7 the profile. Returns 1 and fills the outputs for a DLC of 8, else 0.
uint8_t Mp_ParseExtension(uint8_t dlc, const uint8_t *data, uint32_t *speed_sps, uint8_t *profile);

#endif // MOTION_PROFILE_H
