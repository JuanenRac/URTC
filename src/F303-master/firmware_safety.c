// =============================================================================
// URTC Firmware - Clear-error command and communication watchdog of the steppers and the electromagnet: glue with the board
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_safety.h"
#include "firmware_features.h"
#include "firmware_control_thermal.h"
#include "fw_safety_policy.h"
#include "urtc_can_ids.h"

// The real one is in firmware_control_thermal.c; a suite that does not link the thermal loops keeps this empty one.
__attribute__((weak)) void Control_ThermalGuardsReset(void) {}

static volatile uint32_t host_last_frame_tick;   // the last frame of the host, written by the receive interrupt
static uint32_t move_silence_limit_ms = URTC_STEPPER_SILENCE_MIN_MS;
static uint8_t last_clear_result;                // URTC_CLEAR_ERROR_*, 0 = none since power-up

// Milliseconds since `kick`; a kick that is newer than the clock (the interrupt ran between the two reads) is age zero.
static uint32_t silence_ms(uint32_t now, uint32_t kick)
{
    uint32_t age = now - kick;
    return ((int32_t)age < 0) ? 0u : age;
}

void Safety_NoteHostFrame(void)
{
    host_last_frame_tick = HAL_GetTick();
}

void Safety_NoteStepperMove(uint32_t steps, uint32_t speed_sps)
{
    move_silence_limit_ms = FwSafe_StepperSilenceLimitMs(steps, speed_sps);
    host_last_frame_tick = HAL_GetTick();   // the command that starts the move is itself a sign of life
}

static uint8_t magnet_energised(void)
{
    return (uint8_t)(active_tool == TOOL_ELECTROMAGNET && HAL_GPIO_ReadPin(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_SET);
}

// Every actuator at zero. The hotend fan only cools, so it may run.
static uint8_t actuators_off(void)
{
    return (uint8_t)(laser_power_setpoint == 0 && drill_speed == 0 && target_temperature == 0 && layer_fan_duty == 0 && uv_curing_duty == 0 &&
                     hotair_blower_duty == 0 && steps_remaining == 0 && !magnet_energised());
}

// The temperature that has to be below the limit: that of the heater tools; a reading that could not be trusted counts as unknown; a tool without
// a heater has nothing that can be hot.
static uint16_t tool_temperature_c(void)
{
    if (active_tool != TOOL_SOLDERING_IRON && active_tool != TOOL_HOTAIR_REWORK && active_tool != TOOL_3D_PRINTER) return 0;
    if (system_error_reason == URTC_ERROR_REASON_SENSOR) return FWSAFE_TEMPERATURE_UNKNOWN;
    return current_temperature;
}

uint8_t Safety_HandleClearError(void)
{
    if (rxHeader.StdId != URTC_CAN_ID_CLEAR_ERROR || rxHeader.DLC != 4 ||
        !(rxData[0] == 0x43 && rxData[1] == 0x4C && rxData[2] == 0x45 && rxData[3] == 0x52)) return 0;   // "CLER", written out like the other magic payloads so the comparison with the specification can read it
    uint8_t reason = system_error_reason ? system_error_reason : (uint8_t)URTC_ERROR_REASON_UNKNOWN;
    last_clear_result = FwSafe_ClearDecision(Features_Active(FEAT_CLEAR_ERROR), system_error_flag, reason, tool_temperature_c(), actuators_off());
    if (last_clear_result == URTC_CLEAR_ERROR_CLEARED) {
        system_error_reason = URTC_ERROR_REASON_NONE;
        thermal_fault_code = 0;
        Control_ThermalGuardsReset();   // the watch windows start afresh, as after a power-up
        system_error_flag = 0;
    }
    return 1;
}

uint8_t Safety_ErrorReason(void)
{
    if (!system_error_flag) return URTC_ERROR_REASON_NONE;
    return system_error_reason ? system_error_reason : (uint8_t)URTC_ERROR_REASON_UNKNOWN;
}

uint8_t Safety_LastClearResult(void)
{
    return last_clear_result;
}

void Safety_Service(void)
{
    if (!Features_Active(FEAT_COMMS_WATCHDOG) || system_error_flag) return;
    uint32_t silence = silence_ms(HAL_GetTick(), host_last_frame_tick);
    if (FwSafe_StepperMustStop(1, steps_remaining > 0, silence, move_silence_limit_ms)) {
        steps_remaining = 0;   // the step interrupt also disables the driver as soon as it sees the error
        RAISE_SYSTEM_ERROR(URTC_ERROR_REASON_STEPPER_SILENCE);
        return;
    }
    if (FwSafe_MagnetMustRelease(1, Features_Active(FEAT_MAGNET_RELEASE), magnet_energised(), silence)) {
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
        RAISE_SYSTEM_ERROR(URTC_ERROR_REASON_MAGNET_RELEASE);
    }
}
