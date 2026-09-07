// =============================================================================
// URTC Firmware - Fault/error interrupt handler declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INTERRUPTS_FAULT_H
#define FIRMWARE_INTERRUPTS_FAULT_H

// Overrides the startup file's default weak HardFault_Handler. Forces
// every actuator-capable pin low/analog before anything else - see the
// source for exactly why BRR alone isn't sufficient for the PWM pins.
void HardFault_Handler(void);

#endif // FIRMWARE_INTERRUPTS_FAULT_H
