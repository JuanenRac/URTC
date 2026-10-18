// =============================================================================
// URTC Firmware - Lifetime thermal counters, F-RAM and CAN side
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef FIRMWARE_THERMAL_STATS_H
#define FIRMWARE_THERMAL_STATS_H

#include <stdint.h>

void ThermalStats_LoadFromFram(void);           // once at boot, after SavedState_Load()
void ThermalStats_Service(uint8_t may_write);   // every main-loop pass
void ThermalStats_NoteFault(uint8_t fault_code); // from the thermal guard's fault path
void ThermalStats_HandleQuery(void);            // CAN 0x1AA -> answers 0x1AB

#endif // FIRMWARE_THERMAL_STATS_H
