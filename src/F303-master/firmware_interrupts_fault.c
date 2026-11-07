// =============================================================================
// URTC Firmware - HardFault handler (all-actuator emergency shutdown)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_interrupts_fault.h"
#include "crash_dump.h"

// First action of every HardFault, with or without the crash capture: force every pin on GPIOA and GPIOB low or analog, physically cutting every actuator
// this board drives, before anything else - including before any debug/reset attempt.
//
// BRR only affects pins in GPIO output mode; a pin in Alternate Function mode (PA8, TIM1_CH1: the laser/drill/layer-fan PWM) ignores ODR/BSRR/BRR, so a
// HardFault with the 10W laser firing would leave it firing. Forcing MODER to all-analog (0b11) disconnects every pin from the GPIO driver and any AF
// peripheral, whatever its mode was.
// EXCEPTION: PA13/PA14 (SWDIO/SWCLK) stay in AF mode (0b10) so the debugger can still read the fault registers.
static void cut_every_actuator(void) {
    GPIOA->MODER = 0xEBFFFFFF; // all Port A analog except PA13/PA14 -> AF
    GPIOB->MODER = 0xFFFFFFFF;
}

#if URTC_CRASH_DUMP

// What the fault does once it has the stacked frame (NULL when the stack pointer was not inside RAM): cut the outputs first (nothing is allowed to come
// before that), then copy the registers into the record that survives the reset. It returns so the host tests can call it; the handler then waits for the
// watchdog like the one without the capture.
void CrashDump_OnFault(const uint32_t *frame, uint32_t sp) {
    cut_every_actuator();
    CrashDump_Capture(&crash_dump_store, frame, sp, SCB->CFSR, SCB->HFSR, SCB->MMFAR, SCB->BFAR);
}

void HardFault_Main(uint32_t sp);
void HardFault_Main(uint32_t sp) {
    CrashDump_OnFault(CrashDump_FrameInRam(sp) ? (const uint32_t *)(uintptr_t)sp : (const uint32_t *)0, sp);
    while (1) { }
}

#ifdef __arm__
// The processor stacks r0-r3, r12, lr, pc and xpsr on the stack that was in use (bit 2 of LR says which); this picks that stack pointer and hands it on.
__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b HardFault_Main\n");
}
#else
void HardFault_Handler(void) { HardFault_Main(0); }
#endif

#else // URTC_CRASH_DUMP == 0: the handler the firmware has always had

// Overrides the startup file's default weak HardFault_Handler (an empty infinite loop). If a memory fault or illegal instruction ever lands here, none of
// the PWM timers or software watchdogs get a chance to react on their own - whatever was last commanded (heater, laser, motor) would otherwise stay
// latched on indefinitely.
void HardFault_Handler(void) {
    cut_every_actuator();
    while (1) { }
}

#endif
