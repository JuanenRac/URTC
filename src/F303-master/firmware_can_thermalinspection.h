// =============================================================================
// URTC Firmware - CAN command handler: PCB Advanced Inspection
// (0x250-0x255, thin wrapper over the expansion slave bridge's MLX90640
// access)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_THERMALINSPECTION_H
#define FIRMWARE_CAN_THERMALINSPECTION_H

void Handle_CAN_ThermalInspection(void);

#endif // FIRMWARE_CAN_THERMALINSPECTION_H
