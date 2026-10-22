// =============================================================================
// URTC Firmware - Optional firmware features: glue with the board
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Four functions that earlier releases did not have, each behind its own switch that is OFF
// unless a valid record in F-RAM says otherwise (fw_features.h): ramps for the stepper, a periodic
// health frame, a trim of the internal oscillator and a PID for the heaters. The switches are
// read once at power-up; a change made with 0x1AE acts after the next power-up, never in the
// middle of a job. While a switch is off the function does nothing and the board behaves as in
// earlier releases.
//
// Nothing here has run on a board: the pure logic is tested on the host, and the three places
// that need a bench (the step timer, the analogue channels, the oscillator measurement) are
// marked below.
#ifndef FIRMWARE_FEATURES_H
#define FIRMWARE_FEATURES_H

#include <stdint.h>
#include "fw_features.h"
#include "motion_profile.h"

// Once at boot, before the CAN peripheral starts.
void Features_Init(void);

// The mask in force (what the record said at power-up).
uint8_t Features_Running(void);
uint8_t Features_Active(uint8_t feature_bit);

// 0x1AE: change the persistent switches; 0x1AF: query (and the answer to a change).
void Features_HandleCommand(void);

// Main loop: the health frame once a second and the oscillator trim. No-op while both switches are off.
void Features_Service(void);

// 0x120 with eight bytes while MOTION_RAMPS runs: plans the move. The step generator does not use the plan yet (needs a timing bench).
void Features_NoteMove(uint32_t steps);
const MpPlan_t *Features_MotionPlan(void);

// 0x1B9 (gains, autotune) and 0x1BA (query); both answered with 0x1BA.
void Features_HandlePidCommand(void);

// Called by the heater loop every cycle while THERMAL_PID runs; returns whether the heater is on during this cycle.
uint8_t ThermalPid_Heater(uint16_t current_c, uint16_t target_c, uint32_t now_ms);

// Supplied by a platform file that does not exist yet; the defaults say "not available" / do nothing.
int16_t  Features_PlatformMcuTempC(uint16_t *vdda_mv);                                  // internal sensor and reference (bench: ADC channels)
uint8_t  Features_PlatformHsiMeasure(uint32_t *expected_ticks, uint32_t *measured_ticks); // 1 when a measurement of the CAN bit time is ready (bench)
void     Features_PlatformHsiWriteTrim(uint8_t trim);                                   // RCC_CR HSITRIM (bench)

#endif // FIRMWARE_FEATURES_H
