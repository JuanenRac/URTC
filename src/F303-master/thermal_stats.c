// =============================================================================
// URTC Firmware - Lifetime thermal counters (pure C, no STM32 HAL): thermal_stats.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// See thermal_stats.h.
#include <stddef.h>

#include "thermal_stats.h"

void ThermalStats_Blank(ThermalStats_t *s)
{
    s->magic = THERMAL_STATS_MAGIC;
    s->struct_version = THERMAL_STATS_VERSION;
    s->fault_count = 0;
    s->heater_on_seconds = 0;
    s->last_fault_code = 0;
    ThermalStats_Seal(s);
}

// Same CRC-8/SMBUS as SavedState_Checksum and BlackBox_Checksum.
uint8_t ThermalStats_Checksum(const ThermalStats_t *s)
{
    const uint8_t *bytes = (const uint8_t *)s;
    uint8_t crc = 0x00;
    for (size_t i = 0; i < sizeof(ThermalStats_t) - 1; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void ThermalStats_Seal(ThermalStats_t *s)
{
    s->checksum = ThermalStats_Checksum(s);
}

uint8_t ThermalStats_IsValid(const ThermalStats_t *s)
{
    return s->magic == THERMAL_STATS_MAGIC
        && s->struct_version == THERMAL_STATS_VERSION
        && s->checksum == ThermalStats_Checksum(s);
}

uint8_t ThermalStats_AccumulateHeater(ThermalStats_t *s, ThermalStatsClock_t *clk,
                                      uint32_t now_ms, uint8_t heater_on)
{
    if (!clk->started) {
        clk->started = 1;
        clk->last_ms = now_ms;
        clk->last_on = heater_on ? 1 : 0;
        return 0;
    }
    uint32_t elapsed = now_ms - clk->last_ms; // unsigned: correct across the tick wrap
    uint8_t was_on = clk->last_on;
    clk->last_ms = now_ms;
    clk->last_on = heater_on ? 1 : 0;
    if (!was_on) {
        return 0;
    }
    clk->on_ms_remainder += elapsed;
    if (clk->on_ms_remainder < 1000u) {
        return 0;
    }
    uint32_t whole = clk->on_ms_remainder / 1000u;
    clk->on_ms_remainder %= 1000u;
    s->heater_on_seconds = (s->heater_on_seconds > 0xFFFFFFFFu - whole) ? 0xFFFFFFFFu : s->heater_on_seconds + whole;
    ThermalStats_Seal(s);
    return 1;
}

void ThermalStats_RecordFault(ThermalStats_t *s, uint8_t fault_code)
{
    if (fault_code == 0) {
        return;
    }
    if (s->fault_count < 0xFFFFu) {
        s->fault_count++;
    }
    s->last_fault_code = fault_code;
    ThermalStats_Seal(s);
}

void ThermalStats_Encode(const ThermalStats_t *s, uint8_t persisted, uint8_t out[8])
{
    out[0] = (uint8_t)(s->fault_count >> 8);
    out[1] = (uint8_t)(s->fault_count);
    out[2] = (uint8_t)(s->heater_on_seconds >> 24);
    out[3] = (uint8_t)(s->heater_on_seconds >> 16);
    out[4] = (uint8_t)(s->heater_on_seconds >> 8);
    out[5] = (uint8_t)(s->heater_on_seconds);
    out[6] = s->last_fault_code;
    out[7] = persisted ? 1 : 0;
}
