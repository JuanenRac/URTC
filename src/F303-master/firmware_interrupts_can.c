// =============================================================================
// URTC Firmware - CAN peripheral interrupts (RX0, error)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_interrupts_can.h"

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan_e) {
    can_bus_error_flag = 1;
}

void CAN_RX0_IRQHandler(void) {
    HAL_CAN_IRQHandler(&hcan);
}

// Error/bus-off interrupts (CAN_IT_ERROR | CAN_IT_BUSOFF, activated in
// MX_CAN_Init) are routed by hardware to this separate NVIC vector, not
// CAN_RX0 - without this handler calling HAL_CAN_IRQHandler, the vector
// falls back to the startup file's weak Default_Handler (an unconditional
// infinite loop, confirmed against startup_stm32f303xc.s), which never
// returns and never calls HAL_CAN_ErrorCallback below. A real bus fault
// (electrically plausible next to a BLDC drill, per MX_CAN_Init's own
// comment) would permanently freeze this MCU - every actuator latched at
// whatever state it was last commanded to - instead of just setting
// can_bus_error_flag for the main loop to react to.
void CAN_SCE_IRQHandler(void) {
    HAL_CAN_IRQHandler(&hcan);
}
