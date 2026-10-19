// =============================================================================
// URTC Firmware - Several tool heads on one CAN bus, hardware-only helpers
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Kept apart from firmware_multinode.c so that file compiles on the host with
// the fake HAL; the host suite supplies recorders for these two functions.
#include "firmware_common.h"
#include "firmware_multinode.h"

// The safe state a SAFE_STOP (or a conflict) puts the tool in: heaters off, motion
// stopped, outputs released. The same outputs the bootloader-entry command forces
// before it resets, but without the reset, and without latching system_error_flag:
// the node leaves this state through its own conditions and the normal commands.
void MultiNode_PlatformSafeState(void) {
    target_temperature = 0;
    laser_power_setpoint = 0;
    __disable_irq();
    steps_remaining = 0;
    __enable_irq();
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
    HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET);              // stepper tools: HIGH disables
    HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET);    // drill: LOW brakes
    HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);            // heater off
    if (active_tool == TOOL_LASER_ENGRAVER) {
        // PB6 is the stepper enable and the laser interlock at once; the laser needs LOW (locked).
        HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET);
    }
}

// A 32-bit identifier from the 96-bit unique device ID, so two boards on one address can be told apart.
uint32_t MultiNode_PlatformSerial(void) {
    uint32_t w0 = HAL_GetUIDw0(), w1 = HAL_GetUIDw1(), w2 = HAL_GetUIDw2();
    return w0 ^ ((w1 << 11) | (w1 >> 21)) ^ ((w2 << 22) | (w2 >> 10));
}
