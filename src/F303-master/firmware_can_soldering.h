// =============================================================================
// URTC Firmware - CAN dispatch declarations - soldering iron tool
// (0x130 heater, 0x131 feeder position reset, 0x132 feeder position query/response)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_SOLDERING_H
#define FIRMWARE_CAN_SOLDERING_H

// Handles the current CAN message (rxHeader/rxData) for TOOL_SOLDERING_IRON.
// Called from the main dispatcher's switch(active_tool) only when this
// tool is the one currently active.
void Handle_CAN_SolderingIron(void);

#endif // FIRMWARE_CAN_SOLDERING_H
