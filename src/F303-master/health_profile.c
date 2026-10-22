// =============================================================================
// URTC Firmware - Health frame of the board (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "health_profile.h"

void Health_Encode(const HealthFrame_t *h, uint8_t out[8]) {
    out[0] = h->tec;
    out[1] = h->rec;
    out[2] = h->bus_off_count;
    out[3] = h->hsi_trim;
    out[4] = h->loop_max_ms;
    int t = h->mcu_temp_c;
    if (t == HEALTH_TEMP_NA || t < -39 || t > 215) out[5] = 0;      // plus 40 must fit 1..255
    else out[5] = (uint8_t)(t + 40);
    out[6] = (uint8_t)(h->vdda_mv >> 8);
    out[7] = (uint8_t)(h->vdda_mv & 0xFF);
}

uint8_t Health_CountBusOff(uint8_t previous_count) {
    return previous_count == 0xFF ? 0xFF : (uint8_t)(previous_count + 1u);
}

uint8_t Health_LoopMax(uint8_t previous_max, uint32_t pass_ms) {
    uint8_t now = pass_ms > 255u ? 255u : (uint8_t)pass_ms;
    return now > previous_max ? now : previous_max;
}

uint16_t Health_VddaMv(uint16_t vrefint_raw, uint16_t vrefint_cal_raw) {
    if (vrefint_raw == 0 || vrefint_cal_raw == 0) return HEALTH_VDDA_NA;
    uint32_t mv = (3300u * (uint32_t)vrefint_cal_raw + vrefint_raw / 2u) / vrefint_raw;
    return mv > 0xFFFFu ? 0xFFFFu : (uint16_t)mv;
}

int16_t Health_McuTempC(uint16_t raw, uint16_t vdda_mv, uint16_t cal30, uint16_t cal110) {
    if (vdda_mv == HEALTH_VDDA_NA || cal110 <= cal30) return HEALTH_TEMP_NA;
    int32_t scaled = ((int32_t)raw * (int32_t)vdda_mv + 1650) / 3300;   // the reading the sensor would give at 3300 mV
    int32_t span = (int32_t)cal110 - (int32_t)cal30;
    int32_t t = 30 + ((scaled - (int32_t)cal30) * 80 + (span / 2)) / span;
    if (scaled < (int32_t)cal30) t = 30 + ((scaled - (int32_t)cal30) * 80 - (span / 2)) / span;
    if (t < -127) return HEALTH_TEMP_NA;
    if (t > 32767) return HEALTH_TEMP_NA;
    return (int16_t)t;
}
