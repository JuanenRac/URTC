// =============================================================================
// URTC Firmware - CAN dispatch declarations - shared motion tools (0x120)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_MOTION_H
#define FIRMWARE_CAN_MOTION_H

// Handles the current CAN message for any of the 5 shared plain-stepper
// tools (Paste/Liquid Dispenser, Screwdriver, Gripper Gimbal/NEMA).
void Handle_CAN_MotionTools(void);

#endif // FIRMWARE_CAN_MOTION_H
