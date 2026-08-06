// =============================================================================
// URTC Firmware - HardFault handler (all-actuator emergency shutdown)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_interrupts_fault.h"

// Overrides the startup file's default weak HardFault_Handler (an empty
// infinite loop). If a memory fault or illegal instruction ever lands here,
// none of the PWM timers or software watchdogs get a chance to react on
// their own - whatever was last commanded (heater, laser, motor) would
// otherwise stay latched on indefinitely. First action: force every pin on
// GPIOA and GPIOB low, physically cutting every actuator this board drives,
// before anything else - including before any debug/reset attempt.
void HardFault_Handler(void) {
    // BRR only affects pins currently in GPIO output mode - a pin in
    // Alternate Function mode is driven by that peripheral's own output
    // logic instead, completely bypassing ODR/BSRR/BRR. PA8 (TIM1_CH1 -
    // the laser/drill/layer-fan PWM) is exactly such a pin, so writing to
    // BRR alone wouldn't touch it - a HardFault while the 10W laser was
    // firing would leave it firing indefinitely. Forcing MODER to
    // all-analog (0b11 per pin) electrically disconnects every pin from
    // both the GPIO driver and any AF peripheral - the only state that's
    // safe regardless of whatever mode each pin was in at the moment of
    // the fault.
    // EXCEPTION: PA13/PA14 (SWDIO/SWCLK) are deliberately spared and forced
    // to AF mode (0b10) instead of analog - blanking Port A entirely would
    // cut the debugger connection at exactly the moment someone would most
    // want to read the fault status registers and call stack to find out
    // why this handler was even entered.
    GPIOA->MODER = 0xEBFFFFFF; // all Port A analog except PA13/PA14 -> AF
    GPIOB->MODER = 0xFFFFFFFF;
    while (1) { }
}
