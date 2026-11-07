// =============================================================================
// URTC Firmware - CAN command handler: timed weld pulse (Spot Welder
// 0x1C0, Ultrasonic Welder 0x200)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
//
// doc #4 and doc #15 - both fire a timed pulse on
// CONN_T12, sharing this one handler since the protocol and mechanism
// are otherwise identical: only one of them is ever the active tool at
// a time, so sharing pulse state between them is safe. doc #4's own
// contact sensor gate (CONN_SEN, TCRT_D0_DIG_PIN - already configured
// as a digital input for this tool by the existing PB3 GPIO logic in
// firmware_init_gpio.c/STM32F303CC_main.c, since neither of these tools
// is in that logic's motor-tool exclusion list) is the one real
// difference: doc #15 has no equivalent sensor requirement, so it skips
// that check entirely rather than this handler pretending both tools
// share a gate that only one of them actually has.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_weldpulse.h"
#include "urtc_can_ids.h"
#include "weld_pulse_timing.h"

#if URTC_WELD_PULSE_HW_TIMER
// Timer 17 in one-pulse mode ends the pulse in its own interrupt, whatever the main loop is doing; WeldPulse_Tick stays as the second way to end it.
static void WeldPulse_HwStart(uint16_t duration_ms) {
    uint32_t psc = 0, arr = 0;
    if (urtc_weld_timer_params(SystemCoreClock, duration_ms, &psc, &arr) != 0) return;
    __HAL_RCC_TIM17_CLK_ENABLE();
    TIM17->CR1 = 0;
    TIM17->PSC = psc;
    TIM17->ARR = arr;
    TIM17->EGR = TIM_EGR_UG;
    TIM17->SR = 0;
    TIM17->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM1_TRG_COM_TIM17_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM17_IRQn);
    TIM17->CR1 = TIM_CR1_OPM | TIM_CR1_CEN;
}

void TIM1_TRG_COM_TIM17_IRQHandler(void) {
    if (TIM17->SR & TIM_SR_UIF) {
        TIM17->SR = 0;
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
        weld_pulse_active = 0;
    }
}
#define WELD_HW_START(ms) WeldPulse_HwStart(ms)
#else
#define WELD_HW_START(ms) ((void)0)
#endif

void Handle_CAN_WeldPulse(void) {
    if (weld_pulse_active) return; // ignore a new fire command while a pulse is already running - not queued, just dropped

    if (rxHeader.StdId == URTC_CAN_ID_SPOT_WELD_CMD && rxHeader.DLC >= 3 && !boot_sequence_active
        && active_tool == TOOL_SPOT_WELDER) {
        if (rxData[0] != 0x01) return; // only an explicit fire command starts a pulse - anything else on this ID is ignored, not treated as a malformed-but-tolerated variant
        // Contact sensor gate ("ensure pressure before firing"). The pin is
        // configured for TOOL_SPOT_WELDER by the same GPIO_MODE_INPUT +
        // GPIO_PULLUP branch in STM32F303CC_main.c ("PB3 reconfiguration") as
        // the vacuum pickup comparator and the endstop input, and is
        // active-low everywhere: idle reads HIGH, contact reads LOW. So the
        // pulse may fire only on GPIO_PIN_RESET.
        if (HAL_GPIO_ReadPin(GPIOB, TCRT_D0_DIG_PIN) != GPIO_PIN_RESET) return;

        // Clamp: weld_pulse_duration_ms comes straight from a CAN command
        // with no prior validation, same reasoning as aoi_strobe_period's
        // own clamp above. Real spot-welding/ultrasonic-welding pulses are
        // sub-second; without a cap, a garbled or malicious frame could
        // hold CONN_T12 energized for up to 65 seconds (0xFFFF) instead of
        // a normal short pulse.
        uint16_t raw_duration = ((uint16_t)rxData[1] << 8) | rxData[2];
        weld_pulse_duration_ms = urtc_weld_clamp_ms(raw_duration);
        weld_pulse_start_tick = HAL_GetTick();
        weld_pulse_active = 1;
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_SET);
        WELD_HW_START(weld_pulse_duration_ms);
    } else if (rxHeader.StdId == URTC_CAN_ID_ULTRASONIC_WELD_CMD && rxHeader.DLC >= 3 && !boot_sequence_active
               && active_tool == TOOL_ULTRASONIC_WELDER) {
        if (rxData[0] != 0x01) return;
        // No contact-sensor gate - doc #15 doesn't call for one the way
        // doc #4 explicitly does.
        uint16_t raw_duration = ((uint16_t)rxData[1] << 8) | rxData[2];
        weld_pulse_duration_ms = urtc_weld_clamp_ms(raw_duration); // see the same clamp's own comment above
        weld_pulse_start_tick = HAL_GetTick();
        weld_pulse_active = 1;
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_SET);
        WELD_HW_START(weld_pulse_duration_ms);
    }
}

void WeldPulse_Tick(void) {
    if (!weld_pulse_active) return;
    if (HAL_GetTick() - weld_pulse_start_tick >= weld_pulse_duration_ms) {
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
        weld_pulse_active = 0;
    }
}
