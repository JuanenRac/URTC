// =============================================================================
// URTC Firmware - Timer interrupt handler declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INTERRUPTS_TIMER_H
#define FIRMWARE_INTERRUPTS_TIMER_H

void SysTick_Handler(void);
void TIM3_IRQHandler(void);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

#endif // FIRMWARE_INTERRUPTS_TIMER_H
