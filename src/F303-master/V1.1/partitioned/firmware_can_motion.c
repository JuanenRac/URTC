// =============================================================================
// URTC Firmware - CAN command handler: shared motion tools (0x120)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Covers Paste Dispenser, Liquid Dispenser, Screwdriver, Gripper (Gimbal),
// Gripper (NEMA), SMT Pick&Place (rotary A-axis), and Large-Format Vacuum
// Gripper - 7 tool IDs sharing the exact same plain-stepper STEP/DIR
// protocol, differing only in what's physically attached, not in the
// protocol.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_motion.h"

void Handle_CAN_MotionTools(void) {
                if (rxHeader.StdId == 0x120 && rxHeader.DLC >= 5 && !boot_sequence_active) {
                    __disable_irq();
                    // Clears any steps left over from the previous command
                    // before a direction change takes effect - not a
                    // physical pause before reversing (the DIR pin and the
                    // new step count both change together, in this same
                    // critical section, with no STEP pulse generated
                    // between them, so the actual motion is an instant
                    // reversal, not stop-then-reverse). What this prevents
                    // is stale steps queued under the old direction
                    // running under the new one instead - without this,
                    // a command that both reverses direction and asks for
                    // fewer steps than were still queued would execute
                    // some of that leftover count in the wrong direction.
                    GPIO_PinState new_dir = (rxData[0] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;
                    if (HAL_GPIO_ReadPin(DIR_PORT, DIR_PIN) != new_dir) {
                        steps_remaining = 0;
                    }
                    HAL_GPIO_WritePin(DIR_PORT, DIR_PIN, new_dir);
                    
                    // 32-bit motor steps, big-endian (MSB to LSB)
                    uint32_t steps = ((uint32_t)rxData[1] << 24) | 
                                     ((uint32_t)rxData[2] << 16) | 
                                     ((uint32_t)rxData[3] << 8)  | 
                                     rxData[4];
                    if (steps > 0) {
                        total_steps_setpoint = steps;
                        steps_remaining = steps;
                        // Fresh move: force the step ISR to start with a
                        // rising edge. Left at 1 from the tail end of a
                        // previous move, the first tick of this new move
                        // would perform a falling edge instead, misaligning
                        // the very first step.
                        step_pulse_high = 0;
                        HAL_GPIO_WritePin(STEP_PORT, STEP_PIN, GPIO_PIN_RESET); // force the physical pin low too, not just the flag above
                        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_RESET); // Enable TMC2209
                    } else {
                        // steps==0 is a stop request - the direction-change
                        // branch above already clears steps_remaining when
                        // direction flips, but a stop sent with the same
                        // direction as an already-in-progress move needs
                        // this too, or that move would otherwise run to
                        // completion unaffected by a command that asked
                        // for zero steps.
                        steps_remaining = 0;
                    }
                    __enable_irq();
                }

}
