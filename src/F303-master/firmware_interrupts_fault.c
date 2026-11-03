// =============================================================================
// URTC Firmware - HardFault handler (all-actuator emergency shutdown)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
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
    // BRR only affects pins in GPIO output mode; a pin in Alternate Function
    // mode (PA8, TIM1_CH1: the laser/drill/layer-fan PWM) ignores ODR/BSRR/BRR,
    // so a HardFault with the 10W laser firing would leave it firing. Forcing
    // MODER to all-analog (0b11) disconnects every pin from the GPIO driver and
    // any AF peripheral, whatever its mode was.
    // EXCEPTION: PA13/PA14 (SWDIO/SWCLK) stay in AF mode (0b10) so the
    // debugger can still read the fault registers.
    GPIOA->MODER = 0xEBFFFFFF; // all Port A analog except PA13/PA14 -> AF
    GPIOB->MODER = 0xFFFFFFFF;
    while (1) { }
}
