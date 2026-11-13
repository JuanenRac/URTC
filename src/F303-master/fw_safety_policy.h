// =============================================================================
// URTC Firmware - Clear-error command and communication watchdog: the decisions (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Four questions the board answers with a few integers, kept apart from the hardware so the host tests can replay the shared vectors of URTC-COMMON
// (urtc_common.safety_policy): may a latched critical error be lifted, how long may the host stay silent while a stepper move runs, must that move be
// cut, and does the electromagnet let go. The glue that reads the state of the board and drives the pins is firmware_safety.c. Every function that
// says "feature on" is given the state of a switch that is OFF unless a valid record in F-RAM says otherwise (fw_features.h).
#ifndef FW_SAFETY_POLICY_H
#define FW_SAFETY_POLICY_H

#include <stdint.h>

#define FWSAFE_TEMPERATURE_UNKNOWN 0xFFFFu  // the temperature of the tool cannot be read: a latched error is not lifted
#define FWSAFE_DEFAULT_SPEED_SPS   500u     // what a move without a speed does: one step every two milliseconds

// The outcome (URTC_CLEAR_ERROR_*) of a clear-error command that carried its magic payload. The order of the checks is part of the contract: the switch,
// whether there is an error, whether this kind can be lifted at all, the temperature, the actuators.
uint8_t FwSafe_ClearDecision(uint8_t feature_on, uint8_t error_latched, uint8_t reason, uint16_t temperature_c, uint8_t actuators_off);

// How long the host may stay silent while a move of `steps` runs at `speed_sps` steps/s (0 = the default speed): a share of the time the move should take,
// never below the floor, saturating at 32 bits.
uint32_t FwSafe_StepperSilenceLimitMs(uint32_t steps, uint32_t speed_sps);

// 1 when the move is cut: the switch on, a move running and a silence longer than the limit.
uint8_t FwSafe_StepperMustStop(uint8_t feature_on, uint8_t moving, uint32_t silence_ms, uint32_t limit_ms);

// 1 when the electromagnet lets go: both switches on, the coil energised and a silence longer than URTC_MAGNET_SILENCE_MS.
uint8_t FwSafe_MagnetMustRelease(uint8_t watchdog_on, uint8_t release_on, uint8_t energised, uint32_t silence_ms);

#endif // FW_SAFETY_POLICY_H
