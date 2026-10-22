// =============================================================================
// URTC Firmware - Health frame of the board (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The numbers of the 0x1B8 frame and how they are made from raw readings. Reading the CAN error
// counters, the internal temperature sensor and the internal voltage reference is the job of
// firmware_features.c (and of a platform file for the two analogue channels); here there is no
// register access, so the arithmetic and the frame layout are tested on the host.
#ifndef HEALTH_PROFILE_H
#define HEALTH_PROFILE_H

#include <stdint.h>

#define HEALTH_TEMP_NA   (-128)   // no internal temperature available
#define HEALTH_VDDA_NA   0u       // no analogue supply reading available

typedef struct {
    uint8_t  tec, rec;
    uint8_t  bus_off_count;
    uint8_t  hsi_trim;
    uint8_t  loop_max_ms;
    int16_t  mcu_temp_c;          // HEALTH_TEMP_NA when unknown
    uint16_t vdda_mv;             // HEALTH_VDDA_NA when unknown
} HealthFrame_t;

// 0x1B8 payload: tec, rec, bus_off_count, hsi_trim, loop_max_ms, temperature plus 40 (0 = not available), vdda (big-endian).
void Health_Encode(const HealthFrame_t *h, uint8_t out[8]);

// Count a bus-off: call on the rising edge of the bus-off flag.
uint8_t Health_CountBusOff(uint8_t previous_count);

// Longest main-loop pass since the last frame, in milliseconds, saturating at 255.
uint8_t Health_LoopMax(uint8_t previous_max, uint32_t pass_ms);

// Supply voltage in millivolts from the internal reference: the factory calibration was read at 3300 mV.
uint16_t Health_VddaMv(uint16_t vrefint_raw, uint16_t vrefint_cal_raw);

// Chip temperature in degrees from the internal sensor, with the two factory points (30 and 110 degrees C, both read at 3300 mV).
// raw is the reading at the actual supply, so it is scaled to 3300 mV first with vdda_mv.
int16_t Health_McuTempC(uint16_t raw, uint16_t vdda_mv, uint16_t cal30, uint16_t cal110);

#endif // HEALTH_PROFILE_H
