// =============================================================================
// URTC Firmware - CAN command handler: Soldering Iron (0x130)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_soldering.h"

void Handle_CAN_SolderingIron(void) {
                if (rxHeader.StdId == 0x130 && rxHeader.DLC >= 2) {
                    // Big-endian reconstruction (MSB | LSB)
                    target_temperature = (rxData[0] << 8) | rxData[1];
                    solder_iron_last_kick_tick = HAL_GetTick();
                }

}
