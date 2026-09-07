// =============================================================================
// URTC Firmware - GPIO/EXTI interrupt handler declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INTERRUPTS_GPIO_H
#define FIRMWARE_INTERRUPTS_GPIO_H

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);
void EXTI3_IRQHandler(void);
void EXTI9_5_IRQHandler(void);

#endif // FIRMWARE_INTERRUPTS_GPIO_H
