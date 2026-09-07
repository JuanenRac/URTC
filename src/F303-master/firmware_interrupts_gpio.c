// =============================================================================
// URTC Firmware - GPIO/EXTI interrupts (endstops, scan probe, tachometers)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_interrupts_gpio.h"

// =============================================================================
// 10. ASSOCIATED INTERRUPTS AND VECTOR HANDLERS (ISR)
// =============================================================================
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == TOUCH_IN_PIN) {
        if (active_tool == TOOL_SCAN_PROBE) {
            // Debounces the probe: a mechanical contact genuinely bounces
            // (unlike the tachometer inputs elsewhere, which are about
            // electrical noise) - 30-40 bounces within a millisecond would
            // each fire this whole handler, flooding the bus with duplicate
            // critical-priority messages. This doesn't delay the genuine
            // first impact (it always passes immediately, since nothing
            // recent precedes it) - it only suppresses the bounce-induced
            // duplicates that follow within 2ms.
            static uint32_t last_probe_tick = 0;
            uint32_t now_probe = HAL_GetTick();
            if (now_probe - last_probe_tick < 2) {
                return;
            }
            last_probe_tick = now_probe;
            // Collision frame with critical-priority ID (0x095), injected immediately
            CAN_TxHeaderTypeDef txHead;
            uint8_t txDataLocal[1] = {0x01};
            uint32_t mailbox;
            
            txHead.StdId = 0x095;
            txHead.RTR = CAN_RTR_DATA;
            txHead.IDE = CAN_ID_STD;
            txHead.DLC = 1;
            txHead.TransmitGlobalTime = DISABLE;

            // This is the one message documented as never allowed to fail - if
            // all 3 hardware mailboxes are occupied, clear them out rather than
            // risk AddTxMessage silently dropping the impact report.
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
                HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX0);
                HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX1);
                HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX2);
                // HAL_CAN_AbortTxRequest only sets the hardware's abort-request
                // bit and returns immediately - it doesn't wait for the
                // hardware to actually finish releasing the mailbox, so a
                // short poll here (not a fixed delay) confirms it's really
                // free before trusting the send below to succeed. Bounded
                // iteration count, not a real-time wait, so this can't hang.
                for (uint16_t wait = 0; wait < 1000 && HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0; wait++);
            }

            HAL_CAN_AddTxMessage(&hcan, &txHead, txDataLocal, &mailbox);
            probe_impact_counter++; // to show it live on the OLED
        }
        else if (active_tool == TOOL_DRILL) {
            // BL4260 motor tachometer pulse count. Debounced the same way as
            // the hotend fan (2ms minimum spacing) - a good general defense
            // for any tachometer input even without a specific known noise
            // source, and it's free.
            static uint32_t last_drill_pulse_tick = 0;
            uint32_t now_drill = HAL_GetTick();
            if (now_drill - last_drill_pulse_tick >= 2) {
                drill_rpm_pulses++;
                last_drill_pulse_tick = now_drill;
            }
        }
        else if (active_tool == TOOL_3D_PRINTER) {
            // Layer fan FG pulse count (same PA3 pin, same EXTI3). Same 2ms
            // debounce as above.
            static uint32_t last_layerfan_pulse_tick = 0;
            uint32_t now_layerfan = HAL_GetTick();
            if (now_layerfan - last_layerfan_pulse_tick >= 2) {
                layer_fan_rpm_pulses++;
                last_layerfan_pulse_tick = now_layerfan;
            }
        }
    }
    else if (GPIO_Pin == HOTEND_FAN_FG_PIN) {
        // Hotend fan FG pulse count (PA6, EXTI6 - its own line, no sharing/routing
        // needed unlike EXTI3). Debounced: PA5 (this fan's own PWM output) sits
        // on the adjacent pin and switches at 25kHz - capacitive crosstalk
        // between closely-routed traces on this package could inject noise
        // edges into PA6 that look like real tachometer pulses, which would
        // mask a genuine stall right when the stall detector needs to catch
        // it. 2ms is a clean separation: even an unrealistically fast small
        // fan (15,000+ RPM at 2 pulses/rev) doesn't produce genuine pulses
        // faster than that, while 25kHz noise (40us period) is two orders of
        // magnitude quicker and gets rejected.
        static uint32_t last_hotend_fan_pulse_tick = 0;
        if (active_tool == TOOL_3D_PRINTER) {
            uint32_t now = HAL_GetTick();
            if (now - last_hotend_fan_pulse_tick >= 2) {
                hotend_fan_rpm_pulses++;
                last_hotend_fan_pulse_tick = now;
            }
        }
    }
}

// EXTI3 - the touch scan probe and the drill/3D-printer tachometer share
// this line (see PB3's shared usage elsewhere), routed to Port B in
// MX_GPIO_Post_Init.
void EXTI3_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

// Hotend fan FG tachometer (PA6, EXTI6) - falls in this chip's combined
// 5-9 vector.
void EXTI9_5_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
}
