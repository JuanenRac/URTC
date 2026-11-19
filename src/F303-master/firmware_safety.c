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
#include "fw_estop_follow.h"
#include "firmware_multinode.h"
#include "urtc_can_ids.h"

// The real one is in firmware_control_thermal.c; a suite that does not link the thermal loops keeps this empty one.
__attribute__((weak)) void Control_ThermalGuardsReset(void) {}
// The real one is in firmware_multinode_platform.c (the outputs that a stop puts in the safe state); a suite that does not link it keeps this empty one.
__attribute__((weak)) void MultiNode_PlatformSafeState(void) {}

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
    // With WATCHDOG_HOST_ONLY on, only a frame of the host side is a sign of life (the identifier is the one the receive interrupt just stored).
    if (!FwSafe_WatchdogFeeds(Features_ActiveExt(FEAT_EXT_WATCHDOG_HOST_ONLY), (uint16_t)rxHeader.StdId)) return;
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
uint8_t Safety_ActuatorsOff(void)
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
    // The identifier, the length and the four bytes "CLER" are written out like those of the other commands, so that the comparison with the specification can read them.
    if (!(rxHeader.StdId == URTC_CAN_ID_CLEAR_ERROR && rxHeader.DLC == 4 &&
          rxData[0] == 0x43 && rxData[1] == 0x4C && rxData[2] == 0x45 && rxData[3] == 0x52)) return 0;
    uint8_t reason = system_error_reason ? system_error_reason : (uint8_t)URTC_ERROR_REASON_UNKNOWN;
    last_clear_result = FwSafe_ClearDecision(Features_Active(FEAT_CLEAR_ERROR), system_error_flag, reason, tool_temperature_c(), Safety_ActuatorsOff());
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

// ---- following the emergency stop frame of the stack (ESTOP_FOLLOW) -------------------------------------------------------------------------
static EstopFollow_t estop;
static uint8_t estop_bus_off_seen;

static uint8_t estop_reason_is_ours(uint8_t reason)
{
    return (uint8_t)(reason == URTC_ERROR_REASON_ESTOP || reason == URTC_ERROR_REASON_ESTOP_SILENCE);
}

// What the logic decided: a stop puts the outputs in the safe state (the same one a SAFE_STOP of several heads uses) and latches the critical error, which
// blocks every command until the release; a release lifts the error only when it came from a stop, as the clear-error command would, and never another one.
static void estop_apply(EstopFollowEffect_t effect)
{
    if (effect.stop_reason) {
        MultiNode_PlatformSafeState();
        RAISE_SYSTEM_ERROR(effect.stop_reason);
    } else if (effect.released && system_error_flag && estop_reason_is_ours(system_error_reason)) {
        system_error_reason = URTC_ERROR_REASON_NONE;
        thermal_fault_code = 0;
        Control_ThermalGuardsReset();   // the watch windows start afresh, as after a power-up
        system_error_flag = 0;
    }
}

void Safety_EstopInit(void)
{
    estop_bus_off_seen = 0;
    EstopFollow_Reset(&estop);
    if (!Features_Active(FEAT_ESTOP_FOLLOW)) return;
    EstopFollow_SetTimeout(&estop, Features_EstopTimeoutMs());   // 0 = 100 ms; a value outside the limits is rejected and the default is used
    estop_apply((EstopFollowEffect_t){URTC_ERROR_REASON_ESTOP, 0u});   // like every node of the stack, the head powers up latched
}

uint8_t Safety_HandleEstopFrame(void)
{
    if (!Features_Active(FEAT_ESTOP_FOLLOW)) return 0;
    estop_apply(EstopFollow_OnFrame(&estop, HAL_GetTick(), rxData, (uint8_t)rxHeader.DLC));
    return 1;
}

static void estop_service(void)
{
    if (!Features_Active(FEAT_ESTOP_FOLLOW)) return;
    if (hcan.Instance != NULL) {
        uint8_t bus_off = (uint8_t)((hcan.Instance->ESR >> 2) & 1u);   // BOFF
        if (bus_off && !estop_bus_off_seen) estop_apply(EstopFollow_OnBusOff(&estop));
        estop_bus_off_seen = bus_off;
    }
    __disable_irq();   // the receive interrupt feeds the same state; the decision is taken between two of its frames
    EstopFollowEffect_t effect = EstopFollow_Tick(&estop, HAL_GetTick());
    __enable_irq();
    estop_apply(effect);
}

void Safety_Service(void)
{
    estop_service();
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
