// =============================================================================
// URTC Firmware - CAN dispatch declarations - shared motion tools (0x120)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_MOTION_H
#define FIRMWARE_CAN_MOTION_H

// Handles the current CAN message for any of the 8 shared plain-stepper
// tool IDs sharing the 0x120 protocol (Paste/Liquid Dispenser,
// Screwdriver, Gripper Gimbal/NEMA, SMT Pick&Place, Large-Format Vacuum
// Gripper, and the Soldering Iron's own solder-wire-feeder motor) - see
// this function's own definition in firmware_can_motion.c for the full
// per-tool breakdown.
void Handle_CAN_MotionTools(void);

#endif // FIRMWARE_CAN_MOTION_H
