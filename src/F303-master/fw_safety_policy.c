// =============================================================================
// URTC Firmware - Clear-error command and communication watchdog: the decisions (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "fw_safety_policy.h"
#include "urtc_can_ids.h"

uint8_t FwSafe_ClearDecision(uint8_t feature_on, uint8_t error_latched, uint8_t reason, uint16_t temperature_c, uint8_t actuators_off)
{
    if (!feature_on) return URTC_CLEAR_ERROR_REFUSED_SWITCH_OFF;
    if (!error_latched) return URTC_CLEAR_ERROR_REFUSED_NO_ERROR;
    if (reason == URTC_ERROR_REASON_TOOL_ID) return URTC_CLEAR_ERROR_REFUSED_NOT_CLEARABLE;
    if (temperature_c == FWSAFE_TEMPERATURE_UNKNOWN || temperature_c >= URTC_CLEAR_ERROR_MAX_TEMP_C) return URTC_CLEAR_ERROR_REFUSED_HOT;
    if (!actuators_off) return URTC_CLEAR_ERROR_REFUSED_ACTUATOR_ON;
    return URTC_CLEAR_ERROR_CLEARED;
}

uint32_t FwSafe_StepperSilenceLimitMs(uint32_t steps, uint32_t speed_sps)
{
    uint64_t speed = speed_sps > 0 ? speed_sps : FWSAFE_DEFAULT_SPEED_SPS;
    uint64_t expected_ms = ((uint64_t)steps * 1000u) / speed;
    uint64_t limit = (expected_ms * URTC_STEPPER_SILENCE_PERCENT) / 100u;
    if (limit < URTC_STEPPER_SILENCE_MIN_MS) limit = URTC_STEPPER_SILENCE_MIN_MS;
    if (limit > 0xFFFFFFFFu) limit = 0xFFFFFFFFu;
    return (uint32_t)limit;
}

uint8_t FwSafe_StepperMustStop(uint8_t feature_on, uint8_t moving, uint32_t silence_ms, uint32_t limit_ms)
{
    return (uint8_t)(feature_on && moving && silence_ms > limit_ms);
}

uint8_t FwSafe_MagnetMustRelease(uint8_t watchdog_on, uint8_t release_on, uint8_t energised, uint32_t silence_ms)
{
    return (uint8_t)(watchdog_on && release_on && energised && silence_ms > URTC_MAGNET_SILENCE_MS);
}
