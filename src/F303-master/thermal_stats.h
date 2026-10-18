// =============================================================================
// URTC Firmware - Lifetime thermal counters (pure C, no STM32 HAL): thermal_stats.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// How many thermal runaway faults this board has ever latched, the last one,
// and the total time its heater output has been ON. The black box
// (firmware_blackbox.c) keeps only the last ~10 s before ONE fault; these
// counters keep the long-term picture - a tip that trips every few weeks, or
// a heater approaching its rated life.
//
// Kept in its OWN F-RAM record (THERMAL_STATS_FRAM_ADDR), not inside
// SavedState_t: growing that struct means bumping SAVEDSTATE_VERSION, which
// makes every board discard its saved state on the next boot - including
// free_tool_selection, which leaves an 11111-jumper board in TOOL_INVALID
// until it is reconfigured.
//
// Pure on purpose (like thermal_guard.c): tests/test_thermal_stats.c drives
// it on the host. firmware_thermal_stats.c does the F-RAM and CAN side.
#ifndef THERMAL_STATS_H
#define THERMAL_STATS_H

#include <stdint.h>

#define THERMAL_STATS_MAGIC   0x54484D53UL // 'THMS'
#define THERMAL_STATS_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  struct_version;
    uint16_t fault_count;        // saturates at 0xFFFF
    uint32_t heater_on_seconds;  // saturates at 0xFFFFFFFF (~136 years)
    uint8_t  last_fault_code;    // ThermalGuardFault_t of the most recent fault, 0 = never
    uint8_t  checksum;           // CRC-8/SMBUS over every byte above
} ThermalStats_t; // 13 bytes

// RAM-only accumulator for the sub-second part of the heater-on time.
typedef struct {
    uint32_t last_ms;
    uint32_t on_ms_remainder;
    uint8_t  last_on;
    uint8_t  started;
} ThermalStatsClock_t;

void    ThermalStats_Blank(ThermalStats_t *s);
uint8_t ThermalStats_Checksum(const ThermalStats_t *s);
void    ThermalStats_Seal(ThermalStats_t *s);
uint8_t ThermalStats_IsValid(const ThermalStats_t *s);

// Call on every main-loop pass with the heater output's current state. The
// time since the previous call is credited to the state seen AT that
// previous call (the state the output held during the interval). Returns 1
// when a whole second was added (the record changed). Tick-wrap safe.
uint8_t ThermalStats_AccumulateHeater(ThermalStats_t *s, ThermalStatsClock_t *clk,
                                      uint32_t now_ms, uint8_t heater_on);

// Records one latched thermal fault (code != 0).
void ThermalStats_RecordFault(ThermalStats_t *s, uint8_t fault_code);

// The 8-byte CAN answer (0x1AB): fault_count BE16, heater_on_seconds BE32,
// last_fault_code, valid flag (1 = loaded from F-RAM or counting since a
// blank start, 0 = the F-RAM is missing/unreadable and these are RAM-only).
void ThermalStats_Encode(const ThermalStats_t *s, uint8_t persisted, uint8_t out[8]);

#endif // THERMAL_STATS_H
