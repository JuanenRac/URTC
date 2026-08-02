// =============================================================================
// URTC Firmware - CAN command handler: timed weld pulse (Spot Welder
// 0x1C0, Ultrasonic Welder 0x200)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #4 and doc #15 (15_herramientas.txt) - both fire a timed pulse on
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

void Handle_CAN_WeldPulse(void) {
    if (weld_pulse_active) return; // ignore a new fire command while a pulse is already running - not queued, just dropped

    if (rxHeader.StdId == 0x1C0 && rxHeader.DLC >= 3 && !boot_sequence_active
        && active_tool == TOOL_SPOT_WELDER) {
        if (rxData[0] != 0x01) return; // only an explicit fire command starts a pulse - anything else on this ID is ignored, not treated as a malformed-but-tolerated variant
        // Contact sensor gate - doc #4's own "ensure pressure before
        // firing" requirement. TCRT_D0_DIG_PIN reads HIGH when contact
        // is made (same polarity convention already established for
        // this pin's other digital-input uses elsewhere in this
        // firmware) - refuses to fire without it rather than trusting
        // the master to have checked first.
        if (HAL_GPIO_ReadPin(GPIOB, TCRT_D0_DIG_PIN) != GPIO_PIN_SET) return;

        weld_pulse_duration_ms = ((uint16_t)rxData[1] << 8) | rxData[2];
        weld_pulse_start_tick = HAL_GetTick();
        weld_pulse_active = 1;
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_SET);
    } else if (rxHeader.StdId == 0x200 && rxHeader.DLC >= 3 && !boot_sequence_active
               && active_tool == TOOL_ULTRASONIC_WELDER) {
        if (rxData[0] != 0x01) return;
        // No contact-sensor gate - doc #15 doesn't call for one the way
        // doc #4 explicitly does.
        weld_pulse_duration_ms = ((uint16_t)rxData[1] << 8) | rxData[2];
        weld_pulse_start_tick = HAL_GetTick();
        weld_pulse_active = 1;
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_SET);
    }
}

void WeldPulse_Tick(void) {
    if (!weld_pulse_active) return;
    if (HAL_GetTick() - weld_pulse_start_tick >= weld_pulse_duration_ms) {
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
        weld_pulse_active = 0;
    }
}
