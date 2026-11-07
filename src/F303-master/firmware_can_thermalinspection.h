// =============================================================================
// URTC Firmware - CAN command handler: PCB Advanced Inspection
// (0x250-0x255, thin wrapper over the expansion slave bridge's MLX90640
// access)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_THERMALINSPECTION_H
#define FIRMWARE_CAN_THERMALINSPECTION_H

void Handle_CAN_ThermalInspection(void);

// The capture of a sensor wired straight to this board takes most of a second. The CAN handler (which runs in the receive interrupt) only marks it requested
// and answers BUSY to a status query; this runs it from the main loop, once per pass, after the watchdog has been refreshed.
void ThermalInspection_Service(void);

#endif // FIRMWARE_CAN_THERMALINSPECTION_H
