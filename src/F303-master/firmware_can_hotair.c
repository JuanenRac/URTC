// =============================================================================
// URTC Firmware - CAN command handler: Hot Air Rework Nozzle (0x1E0)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #10 - heating element, air turbine blower,
// thermocouple feedback for reflowing misaligned SMD parts.
//
// Heating element + thermocouple: physically the same T12 cartridge
// mechanism the soldering iron already uses (heater current and
// thermocouple signal share the same 2 conductors) - rather than
// duplicating that control loop, firmware_control_thermal.c's own
// Control_SolderingIron_PID() and firmware_telemetry_watchdog.c's own
// Watchdog_Safety_SolderIron() were both extended to also run for
// TOOL_HOTAIR_REWORK, so every safety property already verified there
// (445C ceiling, stuck-heater detection, ADC-fault handling, comms-loss
// shutdown) applies here unchanged rather than needing its own,
// separately-verified copy. This handler only sets target_temperature -
// same field, same command-byte layout the soldering iron's own 0x130
// already uses, just a different CAN ID for this tool.
//
// Blower: TIM1_CH1, same channel already shared between drill/laser/UV-
// curing/3D-printer's-own-layer-fan (only one tool is ever active at a
// time) - requires STM32F303CC_main.c's own TIM1 init condition to
// include TOOL_HOTAIR_REWORK, which it does as of this same change.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_hotair.h"

void Handle_CAN_HotAirRework(void) {
    if (rxHeader.StdId == 0x1E0 && rxHeader.DLC >= 3 && !boot_sequence_active) {
        target_temperature = ((uint16_t)rxData[0] << 8) | rxData[1];
        solder_iron_last_kick_tick = HAL_GetTick(); // same watchdog timer field the shared thermal loop already reads - see this file's own top note on why this tool reuses it rather than needing its own

        uint32_t blower_duty = (rxData[2] * 3199) / 255;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, blower_duty);
    }
}
