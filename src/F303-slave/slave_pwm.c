// =============================================================================
// URTC Expansion Slave Application Firmware - Local PWM generation
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "stm32f3xx_hal.h"
#include "slave_common.h"
#include "slave_pwm.h"

// TIM1 counts at 1MHz regardless of frequency requested (PSC fixed,
// derived from SystemClock_Config's own 64MHz - see slave_main.c) - ARR
// is what actually varies per-channel to hit a given frequency. 1MHz
// resolution (1us per tick) comfortably covers every tool this firmware
// currently targets (solder paste jetting explicitly wants sub-
// millisecond precision - 1us resolution is 1000x finer than that).
#define TIM1_COUNTER_CLOCK_HZ 1000000UL

static uint8_t pulse_remaining_ms[PWM_CHANNEL_COUNT] = {0};

void PWM_Init(void) {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // TIM1 CH1-CH4 on PA8-PA11 (this chip's default AF6 mapping, same
    // alternate function this project already uses for TIM1 on the main
    // board's own STM32F303CC - not a coincidence, both chips are the
    // same F3 family and share this peripheral's pin mapping convention).
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = (SystemCoreClock / TIM1_COUNTER_CLOCK_HZ) - 1;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 999; // placeholder 1kHz - PWM_Configure overwrites this per-channel-request before any channel actually starts
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0; // 0% duty until PWM_Pulse sets a real value
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4);

    // TIM1 is an advanced-control timer - its outputs stay disabled at
    // the break/dead-time-generator level until this is set, even with a
    // channel individually started, unlike this project's other, simpler
    // timers.
    __HAL_TIM_MOE_ENABLE(&htim1);
}

static uint32_t ChannelToHalChannel(uint8_t channel) {
    switch (channel) {
        case 0: return TIM_CHANNEL_1;
        case 1: return TIM_CHANNEL_2;
        case 2: return TIM_CHANNEL_3;
        default: return TIM_CHANNEL_4;
    }
}

void PWM_Configure(uint8_t channel, uint16_t frequency_hz) {
    if (channel >= PWM_CHANNEL_COUNT || frequency_hz == 0) return;
    uint32_t period = (TIM1_COUNTER_CLOCK_HZ / frequency_hz);
    if (period > 0) period -= 1;
    __HAL_TIM_SET_AUTORELOAD(&htim1, period);
}

void PWM_Pulse(uint8_t channel, uint8_t duty_percent, uint8_t duration_ms) {
    if (channel >= PWM_CHANNEL_COUNT) return;
    if (duty_percent > 100) duty_percent = 100;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    uint32_t ccr = ((uint32_t)duty_percent * (arr + 1)) / 100;
    __HAL_TIM_SET_COMPARE(&htim1, ChannelToHalChannel(channel), ccr);
    HAL_TIM_PWM_Start(&htim1, ChannelToHalChannel(channel));
    pulse_remaining_ms[channel] = duration_ms; // 0 stays 0 - PWM_Tick only ever counts down a nonzero value, so a continuous request (duration_ms=0) is correctly never auto-stopped
}

void PWM_Stop(uint8_t channel) {
    if (channel >= PWM_CHANNEL_COUNT) return;
    HAL_TIM_PWM_Stop(&htim1, ChannelToHalChannel(channel));
    pulse_remaining_ms[channel] = 0;
}

void PWM_Tick(void) {
    // Called once per main loop iteration, which slave_main.c paces at
    // 1ms via HAL_Delay - each call here represents 1ms elapsed, so
    // decrementing by 1 per call is a millisecond countdown without a
    // dedicated hardware timer interrupt for it.
    for (uint8_t ch = 0; ch < PWM_CHANNEL_COUNT; ch++) {
        if (pulse_remaining_ms[ch] > 0) {
            pulse_remaining_ms[ch]--;
            if (pulse_remaining_ms[ch] == 0) {
                PWM_Stop(ch);
            }
        }
    }
}
