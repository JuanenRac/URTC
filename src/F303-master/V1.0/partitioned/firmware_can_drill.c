// =============================================================================
// URTC Firmware - CAN command handler: Drill (0x140)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_drill.h"

void Handle_CAN_Drill(void) {
                if (rxHeader.StdId == 0x140 && rxHeader.DLC >= 2 && !boot_sequence_active) {
                    drill_speed = rxData[0];
                    drill_last_kick_tick = HAL_GetTick();
                    HAL_GPIO_WritePin(DRILL_FRIN_PORT, DRILL_FRIN_PIN, (rxData[1] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                    
                    // drill_speed is 0-255 (full byte) per CANBUS.TXT,
                    // not a 0-100 percentage. Scaled the same way as the laser to cover the full range.
                    uint32_t duty = (drill_speed * 3199) / 255; // scaled for TIM1's 20kHz period
                    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty);
                    
                    if (drill_speed > 0) {
                        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_SET); // Release brake
                    } else {
                        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // Brake
                    }
                }

}
