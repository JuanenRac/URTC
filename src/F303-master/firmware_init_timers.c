// =============================================================================
// URTC Firmware - Timer and ADC initialization
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_init_timers.h"

void MX_TIM3_Full_Init(void) {
    // A pure time-base - the drill/laser/layer-fan PWM lives on
    // TIM1/PA8 instead, so TIM3 doesn't need to double as a PWM source and
    // isn't constrained to a period that also has to work as a
    // usable PWM base for those tools.
    __HAL_RCC_TIM3_CLK_ENABLE();
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 63; // 64MHz/64 = 1MHz tick, same resolution as before
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 1000;  // Fine step resolution (0.1%)
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&htim3);

    // NVIC vector for TIM3's update interrupt - this is what actually
    // drives HAL_TIM_PeriodElapsedCallback, which consumes steps_remaining
    // (set via CAN) to move the 6 stepper-motor tools.
    HAL_NVIC_SetPriority(TIM3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    HAL_TIM_Base_Start_IT(&htim3);
}

// Drill/laser/layer-fan PWM on TIM1_CH1 (PA8), a fully independent timer
// from TIM3's step-tick duty. Takes the target period so each tool gets
// an appropriate rate: the layer fan needs its documented exact 25kHz
// (EFB0424VHD-CP0, see PINOUT_CONNECTORS.TXT), drill/laser run at ~20kHz.
// HAL_TIM_PWM_Start
// automatically enables TIM1's Master Output Enable (MOE) bit for
// break-capable advanced timers like this one - no extra step needed beyond
// the standard PWM start call.
void MX_TIM1_DrillLaserFan_Init(uint32_t period) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = period;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&htim1);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    // Belt-and-suspenders: HAL_TIM_PWM_Start already enables MOE automatically
    // for break-capable advanced timers like this one (confirmed in this
    // exact HAL version's source), but that behavior isn't part of the
    // documented public contract - an explicit call costs nothing and
    // survives a future HAL version changing that internal detail.
    __HAL_TIM_MOE_ENABLE(&htim1);
}

// Hotend fan PWM (CONN_FAN1, PA5). Same 25kHz target as the layer fan, on its
// own timer (TIM2, confirmed free - AF2 on PA5, verified against ST's official
// datasheet Table 14) so it doesn't interfere with TIM3's stepper tick or the
// layer fan's TIM17.
void MX_TIM2_HotendFan_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 2559; // 64MHz / 2560 = 25.000 kHz exactly
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim2);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
}

void MX_ADC_Init(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    __HAL_RCC_ADC12_CLK_ENABLE();
    hadc.Instance = ADC1;
    // Synchronous clock derived directly from AHB (64MHz/2=32MHz) - simpler
    // than routing through the shared ADC12 PLL clock block, and comfortably
    // under this chip's ADC clock ceiling.
    hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc.Init.LowPowerAutoWait = DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.DMAContinuousRequests = DISABLE;
    hadc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    HAL_ADC_Init(&hadc);

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    // This chip's sampling-time steps are entirely different from the F0
    // equivalent (no 41.5-cycle option exists here) - 61.5 cycles is the
    // nearest available step that still errs toward more settling time
    // rather than less, for a resistive-divider/thermocouple-amplifier
    // source that doesn't need fast conversions anyway.
    sConfig.SamplingTime = ADC_SAMPLETIME_61CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc, &sConfig);

    // This chip's calibration call takes an explicit single-ended/differential
    // parameter that F0's equivalent didn't need.
    HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);
}
