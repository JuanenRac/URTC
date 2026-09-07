// =============================================================================
// URTC Firmware - CAN interrupt handler declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INTERRUPTS_CAN_H
#define FIRMWARE_INTERRUPTS_CAN_H

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan_e);
void CAN_RX0_IRQHandler(void);
void CAN_SCE_IRQHandler(void);

#endif // FIRMWARE_INTERRUPTS_CAN_H
