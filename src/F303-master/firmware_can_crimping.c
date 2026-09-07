// =============================================================================
// URTC Firmware - CAN command handler: Wire Harnessing/Crimping
// Actuator (0x1F0)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #12 - high-torque jaw for stripping wires or
// crimping terminals, driven off a BASIC expansion board's own driver
// (TMC2209 or TMC5160A) rather than the onboard one - the first tool in
// this project actually using CONN_EXPANSION's own STEP/DIR/EN pins
// (EXP_TMC_STEP/DIR/EN, PB12/13/14) instead of just passing SPI/I2C
// through to something on that board.
//
// Same command byte layout as the shared 0x120 the onboard-driver tools
// use (direction byte + 32-bit step count, big-endian) - a different CAN
// ID here isn't a different protocol, just this tool's own address in
// the CANBUS.TXT command space, same as every other tool profile has.
// The actual step generation is the SAME ISR every other stepper tool
// uses (firmware_interrupts_timer.c's own HAL_TIM_PeriodElapsedCallback) -
// it resolves which port/pin pair (onboard vs. expansion) to write to
// based on active_tool, so this handler only needs to set up
// steps_remaining/total_steps_setpoint/direction correctly, not
// duplicate the pulse-generation logic itself.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_crimping.h"

void Handle_CAN_CrimpingActuator(void) {
    if (rxHeader.StdId == 0x1F0 && rxHeader.DLC >= 5 && !boot_sequence_active) {
        __disable_irq();
        // Same stale-steps-under-a-new-direction reasoning as
        // Handle_CAN_MotionTools() - see that file's own comment for the
        // full explanation, unchanged here beyond which physical DIR pin
        // gets read/written.
        GPIO_PinState new_dir = (rxData[0] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        if (HAL_GPIO_ReadPin(EXP_TMC_DIR_PORT, EXP_TMC_DIR_PIN) != new_dir) {
            steps_remaining = 0;
        }
        HAL_GPIO_WritePin(EXP_TMC_DIR_PORT, EXP_TMC_DIR_PIN, new_dir);

        uint32_t steps = ((uint32_t)rxData[1] << 24) |
                         ((uint32_t)rxData[2] << 16) |
                         ((uint32_t)rxData[3] << 8)  |
                         rxData[4];
        if (steps > 0) {
            total_steps_setpoint = steps;
            steps_remaining = steps;
            step_pulse_high = 0;
            HAL_GPIO_WritePin(EXP_TMC_STEP_PORT, EXP_TMC_STEP_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(EXP_TMC_EN_PORT, EXP_TMC_EN_PIN, GPIO_PIN_RESET); // Enable the expansion board's own driver
        } else {
            steps_remaining = 0;
        }
        __enable_irq();
    }
}
