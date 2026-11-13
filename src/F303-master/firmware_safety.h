// =============================================================================
// URTC Firmware - Clear-error command and communication watchdog of the steppers and the electromagnet: glue with the board
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Two functions behind their own switches (fw_features.h), both OFF unless a valid record in F-RAM says otherwise:
//   CLEAR_ERROR     the command 0x1B2 lifts a latched critical error without a power cycle, only when the tool is cool and every actuator is at zero;
//   COMMS_WATCHDOG  a stepper move is cut when the host falls silent for longer than a share of the time the move should take; with MAGNET_RELEASE also on,
//                   the electromagnet lets go after a long silence (with it off, the coil is held, as before).
// While a switch is off the function does nothing and the board behaves as in earlier releases. The decisions are in fw_safety_policy.c, tested on the
// host against the vectors of URTC-COMMON; nothing here has run on a board.
#ifndef FIRMWARE_SAFETY_H
#define FIRMWARE_SAFETY_H

#include <stdint.h>

// Every standard data frame that reaches the dispatcher: the host is alive.
void Safety_NoteHostFrame(void);

// A move was accepted (steps > 0) with this cruise speed in steps/s (0: the default speed): the silence the host may keep while it runs is worked out.
void Safety_NoteStepperMove(uint32_t steps, uint32_t speed_sps);

// 0x1B2: 1 when the frame carried the magic payload (the caller answers with the quick state), 0 when it is to be ignored. Lifts the error when the
// decision says so.
uint8_t Safety_HandleClearError(void);

// Bytes 6 and 7 of the quick state 0x111: why the error is latched (0 when none), and what the last clear-error command did (0 when none since power-up).
uint8_t Safety_ErrorReason(void);
uint8_t Safety_LastClearResult(void);

// Main loop, every 20 ms: the silence of the host against the move that runs and the coil that is energised. No-op while the switch is off.
void Safety_Service(void);

#endif // FIRMWARE_SAFETY_H
