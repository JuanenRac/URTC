// =============================================================================
// URTC Firmware - Physical tool identification (ID-jumper matrix readout)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_identify.h"

// =============================================================================
// 8. AUTOMATIC MUTANT IDENTIFICATION (TOOL ECOSYSTEM HARDWARE READOUT)
// =============================================================================
void Identify_PhysicalTool(void) {
    // Read multiple times with a short settling delay between each,
    // requiring 3 consecutive identical readings before accepting one.
    // This runs exactly once, here at boot, never again during operation
    // (active_tool is a fixed, per-board hardware property, not something
    // re-read mid-session) - so this isn't guarding against vibration
    // during a move the way a continuously-read input would need to. What
    // it does guard against: a single noisy or unsettled reading right at
    // power-up latching the wrong tool for the entire session, since nothing
    // else ever gets a chance to correct it before the next power cycle.
    uint8_t id = 0;
    uint8_t stable_count = 0;
    uint8_t last_id = 0xFF; // sentinel - never a valid 5-bit reading, guarantees the first pass never matches
    for (uint8_t attempt = 0; attempt < 10 && stable_count < 3; attempt++) {
        id = 0;
        if (HAL_GPIO_ReadPin(ID0_PORT, ID0_PIN) == GPIO_PIN_RESET) id |= 0x01;
        if (HAL_GPIO_ReadPin(ID1_PORT, ID1_PIN) == GPIO_PIN_RESET) id |= 0x02;
        if (HAL_GPIO_ReadPin(ID2_PORT, ID2_PIN) == GPIO_PIN_RESET) id |= 0x04;
        if (HAL_GPIO_ReadPin(ID3_PORT, ID3_PIN) == GPIO_PIN_RESET) id |= 0x08;
        if (HAL_GPIO_ReadPin(ID4_PORT, ID4_PIN) == GPIO_PIN_RESET) id |= 0x10;
        if (id == last_id) {
            stable_count++;
        } else {
            stable_count = 1;
            last_id = id;
        }
        if (stable_count < 3) {
            HAL_Delay(2);
        }
    }
    // Falls through with whatever the last reading was even if 10 attempts
    // never produced 3 matching in a row (a genuinely floating/unconnected
    // jumper bank could oscillate indefinitely) - the id<=11 validity check
    // right below still applies exactly as before, so an unstable reading
    // most likely just lands on TOOL_INVALID rather than a wrong-but-valid
    // tool, which is the safe direction for this to fail in.
    
    raw_id_pin_value = id; // kept for the id==31 case below, and readable
                            // over CAN (0x1A3) afterward for diagnostics

    if (id <= 11) {
        active_tool = (ToolMode_t)id;
    } else if (id == 31) {
        // 0x1F/11111b - every jumper installed - is the "free
        // configuration" address (see EEPROM.TXT/CANBUS.TXT): rather than
        // a fixed tool, the actual selection lives in the F-RAM's
        // free_tool_selection register, set ahead of time via CAN
        // (0x1A2, see CANBUS.TXT) - normally through the Tester, since
        // the Flasher is the one that can actually write it. Read
        // directly here (not through SavedState_Load(), which hasn't run
        // yet and reads the same F-RAM this same way, but for the wider
        // set of restorable settings) - only this one register matters
        // for this decision, and MX_I2C2_Init_Early() (called just before
        // this function in main()) made that read possible this early in
        // boot.
        SavedState_t fram_check;
        uint8_t selection = 0; // 0 = no tool selected, same fail-safe default as an invalid reading
        if (FRAM_ReadBytes(SAVEDSTATE_FRAM_ADDR, (uint8_t *)&fram_check, sizeof(fram_check))
            && fram_check.magic == SAVEDSTATE_MAGIC
            && fram_check.struct_version == SAVEDSTATE_VERSION
            && SavedState_Checksum(&fram_check) == fram_check.checksum) {
            selection = fram_check.free_tool_selection;
        }
        if (selection >= 1 && selection <= 12) {
            active_tool = (ToolMode_t)(selection - 1);
        } else {
            active_tool = TOOL_INVALID;
            system_error_flag = 1;
            HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET);   // steppers disabled
            HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // drill brake engaged
            HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // laser interlock locked
            HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET); // heater off
        }
    } else {
        active_tool = TOOL_INVALID;
        system_error_flag = 1;
        // Explicit, redundant safety: force every actuator output to its
        // inert state right now, rather than trusting that no other code
        // path will ever touch them for a tool ID nothing else recognizes.
        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET);   // steppers disabled
        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // drill brake engaged
        HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // laser interlock locked
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET); // heater off
    }
}
