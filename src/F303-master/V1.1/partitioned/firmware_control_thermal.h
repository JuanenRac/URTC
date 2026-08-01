// =============================================================================
// URTC Firmware - Thermal PID control declarations (soldering iron, 3D hotend)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CONTROL_THERMAL_H
#define FIRMWARE_CONTROL_THERMAL_H

// Bang-bang PID-style control loops, each a no-op if their tool isn't the
// active one. Both include an independent hardware safety ceiling on top
// of whatever setpoint the host sends - see the source for the exact
// temperature limits.
void Control_SolderingIron_PID(void);
void Control_3D_Hotend_PID(void);

#endif // FIRMWARE_CONTROL_THERMAL_H
