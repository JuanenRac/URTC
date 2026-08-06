// =============================================================================
// URTC Firmware - CAN telemetry and communication-watchdog declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_TELEMETRY_WATCHDOG_H
#define FIRMWARE_TELEMETRY_WATCHDOG_H

// RPM telemetry - each computes actual_rpm from an edge counter accumulated
// since the last call, then resets the counter for the next cycle.
void Telemetry_Drill(void);
void Telemetry_LayerFan(void);
void Telemetry_HotendFan(void);

// Communication watchdogs - each is a no-op unless its tool is active and
// hasn't heard its own keepalive/refresh command recently enough, in which
// case it forces that tool's output to a safe state.
void Watchdog_Safety_Laser(void);
void Watchdog_Safety_Drill(void);
void Watchdog_Safety_SolderIron(void);
void Watchdog_Safety_HotendHeater(void);
void Watchdog_Safety_LayerFan(void);
void Watchdog_Safety_HotendFan(void);

#endif // FIRMWARE_TELEMETRY_WATCHDOG_H
