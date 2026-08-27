// =============================================================================
// URTC Firmware - Timer peripheral init declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INIT_TIMERS_H
#define FIRMWARE_INIT_TIMERS_H

#include <stdint.h>

void MX_TIM3_Full_Init(void);
void MX_TIM1_DrillLaserFan_Init(uint32_t period);
void MX_TIM2_HotendFan_Init(void);
void MX_ADC_Init(uint32_t channel);

#endif // FIRMWARE_INIT_TIMERS_H
