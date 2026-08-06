// =============================================================================
// URTC Firmware - CAN command handler: Functional Testing Head
// (0x240-0x242, thin wrapper over the expansion slave bridge's ADS1115
// access)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #7 (documented in the tool catalog) - the only one of the 13
// newer tools with TWO distinct reading paths, not an either/or choice
// this board makes for the tool head: this tool's own "basic" onboard-
// ADC reading (CONN_SEN, same shared channel vacuum pickup's own 0x145
// already uses - Control_SensorTelemetry extended to cover this tool
// too, sent on its own 0x243 rather than reusing 0x145) works
// standalone, no expansion board needed at all. This file covers the
// "advanced" path specifically - an ADS1115 16-bit ADC on an ADVANCED
// expansion board, reached the same way every other advanced-board tool
// reaches its own sensor: relayed over firmware_can_slavebridge.c's own
// I2C bridge.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_flyingprobe.h"

#define REG_APP_ADS_CONFIGURE 0x20
#define REG_APP_ADS_TRIGGER   0x21
#define REG_APP_ADS_READ      0x22

void Handle_CAN_FlyingProbe(void) {
    if (rxHeader.StdId == 0x240 && rxHeader.DLC == 2 && !boot_sequence_active) {
        // Forwarded near-verbatim into the ADS1115's own 16-bit config
        // register - this board doesn't interpret the bitfields (gain,
        // mux, data rate), same "generic passthrough" reasoning as the
        // SPI passthrough to the expansion driver.
        ExpansionI2C_SlaveWriteRegister(REG_APP_ADS_CONFIGURE, rxData, 2);

    } else if (rxHeader.StdId == 0x241 && !boot_sequence_active) {
        ExpansionI2C_SlaveWriteRegister(REG_APP_ADS_TRIGGER, rxData, 0);

    } else if (rxHeader.StdId == 0x242) {
        uint8_t result[2];
        if (ExpansionI2C_SlaveReadRegister(REG_APP_ADS_READ, result, 2)) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
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
}
