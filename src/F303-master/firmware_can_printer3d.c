// =============================================================================
// URTC Firmware - CAN command handler: 3D Printer (0x170-0x178)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_printer3d.h"

void Handle_CAN_3DPrinter(void) {
                if (rxHeader.StdId == 0x170 && rxHeader.DLC >= 6) {
                    // Hotend temperature, big-endian (MSB | LSB)
                    // Not gated on boot_sequence_active - starting the heater
                    // pre-warming during the splash is fine (no physical
                    // motion involved); only the extruder steps below need
                    // to wait for the splash to finish.
                    target_temperature = (rxData[0] << 8) | rxData[1];
                    hotend_heater_last_kick_tick = HAL_GetTick();
                    if (!boot_sequence_active) {
                    __disable_irq();
                    // Clear residual queued steps before a direction change - same reasoning as the 0x120 handler above
                    GPIO_PinState new_dir = (rxData[2] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;
                    if (HAL_GPIO_ReadPin(DIR_PORT, DIR_PIN) != new_dir) {
                        steps_remaining = 0;
                    }
                    HAL_GPIO_WritePin(DIR_PORT, DIR_PIN, new_dir);
                    
                    // Extruder steps, big-endian (Bytes 3, 4, and 5)
                    uint32_t extruder_steps = ((uint32_t)rxData[3] << 16) | 
                                         ((uint32_t)rxData[4] << 8)  | 
                                         rxData[5];
                    if (extruder_steps > 0) {
                        total_steps_setpoint = extruder_steps;
                        steps_remaining = extruder_steps;
                        step_pulse_high = 0; // see the 0x120 handler above
                        HAL_GPIO_WritePin(STEP_PORT, STEP_PIN, GPIO_PIN_RESET); // physical pin low too, not just the flag
                        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_RESET);
                    } else {
                        steps_remaining = 0; // stop request - see the 0x120 handler above
                    }
                    __enable_irq();
                    }
                }
                // Layer fan (0x173): duty 0-255 towards TIM1_CH1/PA8 at 25kHz,
                // sharing the same channel as drill/laser (reconfigured to
                // this tool's exact rate in main()) - independent from TIM3.
                else if (rxHeader.StdId == 0x173 && rxHeader.DLC >= 1) {
                    layer_fan_duty = rxData[0];
                    uint32_t fan_duty = (layer_fan_duty * 2559) / 255;
                    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, fan_duty);
                    layer_fan_last_kick_tick = HAL_GetTick();
                }
                // Hotend fan (0x178): duty 0-255 towards TIM2_CH1/PA5 at
                // 25kHz. Separate from the layer fan - cools the heatsink/heat
                // break, not the printed part.
                else if (rxHeader.StdId == 0x178 && rxHeader.DLC >= 1) {
                    hotend_fan_duty = rxData[0];
                    uint32_t hfan_duty = (hotend_fan_duty * 2559) / 255;
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, hfan_duty);
                    hotend_fan_last_kick_tick = HAL_GetTick();
                }
                
}
