// =============================================================================
// URTC Firmware - Fault/error interrupt handler declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INTERRUPTS_FAULT_H
#define FIRMWARE_INTERRUPTS_FAULT_H

// Overrides the startup file's default weak HardFault_Handler. Forces
// every actuator-capable pin low/analog before anything else - see the
// source for exactly why BRR alone isn't sufficient for the PWM pins.
void HardFault_Handler(void);

#include <stdint.h>
#include "crash_dump.h"
#if URTC_CRASH_DUMP
// With the crash capture switched on (crash_dump.h): what the fault does with the stacked frame (or NULL) and its address, short of the final wait for the watchdog.
void CrashDump_OnFault(const uint32_t *frame, uint32_t sp);
#endif

#endif // FIRMWARE_INTERRUPTS_FAULT_H
