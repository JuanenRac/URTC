// =============================================================================
// URTC Firmware - CAN command handler: Functional Testing Head
// (0x240-0x242, ADS1115 access - either direct or via the expansion
// slave bridge, depending on which board variant is populated)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #7 - the only tool with TWO distinct reading paths, not an
// either/or choice this board makes for the tool head: this tool's own
// "basic" onboard-ADC reading (CONN_SEN, same shared channel vacuum
// pickup's own 0x145 already uses - Control_SensorTelemetry extended to
// cover this tool too, sent on its own 0x243) works standalone, no
// expansion board needed at all. This file covers the "advanced
// reading" path - an ADS1115 16-bit ADC - which itself now has TWO
// ways to be reached, both ending up at the same 0x240-0x242 CAN IDs
// so a host never needs to know or care which is actually populated:
//
//   expansion_board_type==5 (Basic+ADS1115): the chip is wired directly
//   onto this board's own bit-banged I2C bus, no slave MCU involved at
//   all - firmware_ads1115.c's own ADS1115_Direct_* functions talk to
//   it directly.
//
//   expansion_board_type==3 or 4 (either Advanced variant): the ADS1115
//   lives on the expansion slave chip's own LOCAL sensor bus, reached
//   the same way every other advanced-board tool reaches its own
//   sensor - relayed over firmware_can_slavebridge.c's own I2C bridge.
//
// Any other expansion_board_type (0, 1, 2, or 6 - none, either basic
// driver board, or the basic MLX9064x board) means no ADS1115 is
// actually reachable at all; both branches below already fail silently
// in that case (the direct path's own bit-bang read fails against
// nothing responding, the bridge's own read returns 0 from a timed-out
// transaction) rather than needing a third explicit "not present" case.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_flyingprobe.h"
#include "firmware_ads1115.h"
#include "firmware_expansion_i2c.h"

#define REG_APP_ADS_CONFIGURE 0x20
#define REG_APP_ADS_TRIGGER   0x21
#define REG_APP_ADS_READ      0x22

void Handle_CAN_FlyingProbe(void) {
    if (rxHeader.StdId == 0x240 && rxHeader.DLC == 2 && !boot_sequence_active) {
        uint16_t cfg = ((uint16_t)rxData[0] << 8) | rxData[1];
        if (expansion_board_type == 5) {
            ADS1115_Direct_Configure(cfg);
        } else {
            // Forwarded near-verbatim into the ADS1115's own 16-bit config
            // register - neither this board nor the slave chip interprets
            // the bitfields (gain, mux, data rate), same "generic
            // passthrough" reasoning as the SPI passthrough to the
            // expansion driver.
            ExpansionI2C_SlaveWriteRegister(REG_APP_ADS_CONFIGURE, rxData, 2);
        }

    } else if (rxHeader.StdId == 0x241 && !boot_sequence_active) {
        if (expansion_board_type == 5) {
            ADS1115_Direct_TriggerConversion();
        } else {
            ExpansionI2C_SlaveWriteRegister(REG_APP_ADS_TRIGGER, rxData, 0);
        }

    } else if (rxHeader.StdId == 0x242) {
        uint8_t result[2];
        uint8_t got_result = 0;
        if (expansion_board_type == 5) {
            int16_t raw = ADS1115_Direct_ReadResult();
            result[0] = (uint8_t)(((uint16_t)raw) >> 8);
            result[1] = (uint8_t)(((uint16_t)raw) & 0xFF);
            got_result = 1;
        } else {
            got_result = ExpansionI2C_SlaveReadRegister(REG_APP_ADS_READ, result, 2);
        }
        if (got_result && HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
            CAN_TxHeaderTypeDef txH;
            txH.StdId = 0x242;
            txH.IDE = CAN_ID_STD;
            txH.RTR = CAN_RTR_DATA;
            txH.DLC = 2;
            txH.TransmitGlobalTime = DISABLE;
            uint32_t mb;
            HAL_CAN_AddTxMessage(&hcan, &txH, result, &mb);
        }
    }
}
