// =============================================================================
// URTC Firmware - CAN command handler: Soldering Iron (0x130)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The wire feeder's own move command (0x120, forward/reverse + step count)
// is NOT handled here - it's Handle_CAN_MotionTools()'s own shared
// mechanism, called separately for this same tool ID from the dispatcher
// (see firmware_can_dispatch.c) since CONN_MOT's own STEP/DIR/ENN protocol
// is identical to the 7 other motor-based tools that already use it. Only
// this tool's own heater setpoint (0x130) and the feeder's own position
// reset/query (0x131/0x132) live here.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_soldering.h"

void Handle_CAN_SolderingIron(void) {
                if (rxHeader.StdId == 0x130 && rxHeader.DLC >= 2) {
                    // Big-endian reconstruction (MSB | LSB)
                    target_temperature = (rxData[0] << 8) | rxData[1];
                    solder_iron_last_kick_tick = HAL_GetTick();
                }

                // Reset the wire feeder's own open-loop position tracker to
                // zero - typically sent by the operator right after loading
                // a fresh spool, so 0x132 reads something meaningful again
                // rather than an arbitrary leftover value from whatever was
                // fed before. Magic payload (spells "RSP0" in ASCII - Reset
                // Spool Position 0) for the same reason 0x192's own erase
                // command has one: a corrupted or malformed frame landing on
                // this ID by chance shouldn't be able to silently discard a
                // real tracked position.
                if (rxHeader.StdId == 0x131 && rxHeader.DLC == 4 &&
                    rxData[0] == 0x52 && rxData[1] == 0x53 && rxData[2] == 0x50 && rxData[3] == 0x30) {
                    solder_spool_position = 0;
                }

                // Query/response: current wire feeder position. Payload
                // ignored as a query (any DLC). Same pairing style as
                // 0x1A2/0x1A3 elsewhere in this firmware - answering 0x132
                // both right after a reset above and as a bare query,
                // rather than needing a separate response ID.
                if (rxHeader.StdId == 0x131 || rxHeader.StdId == 0x132) {
                    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                        CAN_TxHeaderTypeDef txH;
                        uint32_t pos = (uint32_t)solder_spool_position;
                        uint8_t txD[4] = {
                            (uint8_t)((pos >> 24) & 0xFF),
                            (uint8_t)((pos >> 16) & 0xFF),
                            (uint8_t)((pos >> 8) & 0xFF),
                            (uint8_t)(pos & 0xFF)
                        };
                        txH.StdId = 0x132;
                        txH.IDE = CAN_ID_STD;
                        txH.RTR = CAN_RTR_DATA;
                        txH.DLC = 4;
                        txH.TransmitGlobalTime = DISABLE;
                        uint32_t mb;
                        HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                    }
                }

}
