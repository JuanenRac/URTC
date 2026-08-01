// =============================================================================
// URTC Firmware - SysTick and TIM3 (step-pulse) interrupts
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_interrupts_timer.h"

// Advances the millisecond tick HAL_GetTick() reads. Without this handler
// calling HAL_IncTick(), HAL_GetTick() would always return 0 and every
// HAL_Delay() call in this file would spin forever, since its exit
// condition compares HAL_GetTick() against a start value that's also 0.
void SysTick_Handler(void) {
    HAL_IncTick();
}

// Drives the stepper motors: every 1ms it toggles STEP once, so a full
// pulse (high+low edge, what nearly every TMC/generic driver counts as
// "one step") takes 2 ticks = 2ms -> max ~500 steps/second. Enough for
// dispensers/screwdriver/grippers; if the 3D extruder needs a higher
// print speed, this is the place to speed up (shorter TIM3 period).
void TIM3_IRQHandler(void) {
    HAL_TIM_IRQHandler(&htim3);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance != TIM3) return;

    // Blocking new CAN commands during system_error_flag alone does
    // nothing to stop a motion already in progress. A long extrusion or
    // dispense move started before a thermal fault or fan stall tripped
    // the flag would keep running to completion - potentially tens of
    // seconds of a motor driven blind while the OLED already reads SYSTEM
    // BLOCKED. An emergency stop needs to halt physical motion the instant
    // the fault is detected, not just refuse new instructions.
    if (system_error_flag) {
        steps_remaining = 0;
        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET); // stepper tools disabled
        // Cutting stepper current does nothing for the laser/drill PWM
        // channel or the two fan PWMs - force every PWM output to 0 too.
        // Guarded: htim1/htim2 are only initialized for certain tools (see
        // main()) - their .Instance is NULL otherwise.
        if (htim1.Instance != NULL) {
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0); // drill/laser/layer-fan PWM
        }
        // The hotend fan is a cooling fan, not an actuator that can cause
        // harm by continuing to run - left running (htim2 intentionally not
        // zeroed) so it can keep dissipating residual heat.
        if (active_tool == TOOL_LASER_ENGRAVER) {
            HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // interlock locked
        }
        // PWM=0 alone doesn't stop a spinning drill bit - it coasts freely.
        // DRILL_ENN's polarity is LOW=brake, HIGH=spin. Its own pin now, so
        // this can't interact with the stepper ENN write above regardless
        // of ordering.
        if (active_tool == TOOL_DRILL) {
            HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET);
        }
        return;
    }

    // Explicitly tracks which half of the pulse we're in, rather than a
    // plain TogglePin: stepper drivers (TMC2209 included) only count a
    // step on the rising edge, so steps_remaining decrements once per real
    // step (matching the OLED display and the CAN protocol) - still 2
    // ticks (2ms) per real step, ~500 steps/sec ceiling.

    if (steps_remaining > 0) {
        if (!step_pulse_high) {
            HAL_GPIO_WritePin(GPIOB, STEP_PIN, GPIO_PIN_SET); // rising edge = the actual step
            step_pulse_high = 1;
        } else {
            HAL_GPIO_WritePin(GPIOB, STEP_PIN, GPIO_PIN_RESET);
            step_pulse_high = 0;
            // CONCURRENCY FIX (separate from the one below): steps_remaining--
            // is a non-atomic read-modify-write. The higher-priority CAN
            // interrupt can preempt right in the middle of it with a fresh
            // move command (a new nonzero steps_remaining), and this ISR
            // resuming afterward would overwrite that fresh value with its
            // own stale decremented one - silently erasing the new command.
            __disable_irq();
            steps_remaining--;
            __enable_irq();
            if (steps_remaining == 0) {
                // A higher-priority CAN interrupt can
                // preempt this ISR right here, process a new move command
                // (fresh nonzero steps_remaining, driver re-enabled), then
                // return control back to this exact point. Without a fresh
                // re-check, the code below would still see the earlier
                // (now stale) "just reached zero" result and disable the
                // driver it was just re-enabled for - the new move would
                // count down doing nothing, since no current reaches the
                // motor. Re-reading right before acting catches that.
                if (steps_remaining != 0) {
                    return;
                }
                // Cutting power unconditionally here would release
                // holding torque even for tools that need to keep gripping or
                // resisting backpressure after the move finishes - the NEMA
                // and gimbal grippers would relax their jaws and could drop
                // whatever they just grasped, and the 3D printer's extruder
                // would let molten filament pressure push the gear backward
                // between moves. Only cut power for the tools where holding
                // torque genuinely isn't needed once idle.
                if (active_tool != TOOL_GRIPPER_NEMA && active_tool != TOOL_GRIPPER_GIMBAL &&
                    active_tool != TOOL_3D_PRINTER) {
                    HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET); // Cut power once finished
                }
            }
        }
    }
}
