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
