// =============================================================================
// URTC Firmware - CAN command handler: PCB Advanced Inspection
// (0x250-0x255, thin wrapper over the expansion slave bridge's MLX90640
// access)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// This tool's own thermal imaging spots PCB shorts by temperature
// signature. Ring-LED illumination isn't handled here - it's the same
// generic ring-LED control every tool already has via 0x100, nothing
// thermal-inspection-specific about it.
//
// The one real complication this tool has that no other advanced-board
// tool does: each of the slave's own 48 pixel chunks is 32 bytes
// (slave_common.h's own REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK),
// which doesn't fit in one CAN frame's own 8-byte ceiling the way every
// other advanced-board register here does - so a single chunk request
// gets answered across 4 consecutive CAN frames on the same response ID
// (same convention this project's own bootloader HMAC chunk transfer
// already established: 4 frames of 8 bytes, concatenated in the order
// received, no separate sub-index needed since I2C already delivered
// them in order and this board relays that same order onward).
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_thermalinspection.h"

#define REG_APP_MLX_TRIGGER_CAPTURE  0x10
#define REG_APP_MLX_CAPTURE_STATUS   0x11
#define REG_APP_MLX_RAW_CHUNK        0x12
#define REG_APP_MLX_CALIBRATED_CHUNK 0x13

static void SendChunkFrames(uint32_t id, const uint8_t *data32) {
    for (uint8_t frame = 0; frame < 4; frame++) {
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return; // stop rather than send an out-of-order remainder if mailboxes run out mid-transfer - a host seeing fewer than 4 frames for this request already knows to treat it as incomplete, same as it would a dropped CAN frame from any other cause
        CAN_TxHeaderTypeDef txH;
        txH.StdId = id;
        txH.IDE = CAN_ID_STD;
        txH.RTR = CAN_RTR_DATA;
        txH.DLC = 8;
        txH.TransmitGlobalTime = DISABLE;
        uint32_t mb;
        HAL_CAN_AddTxMessage(&hcan, &txH, (uint8_t*)&data32[frame * 8], &mb);
    }
}

void Handle_CAN_ThermalInspection(void) {
    if (rxHeader.StdId == 0x250 && !boot_sequence_active) {
        ExpansionI2C_SlaveWriteRegister(REG_APP_MLX_TRIGGER_CAPTURE, rxData, 0);

    } else if (rxHeader.StdId == 0x251) {
        uint8_t status;
        if (ExpansionI2C_SlaveReadRegister(REG_APP_MLX_CAPTURE_STATUS, &status, 1)) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                txH.StdId = 0x251;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, &status, &mb);
            }
        }

    } else if (rxHeader.StdId == 0x252 && rxHeader.DLC >= 1) {
        uint8_t chunk[32];
        if (ExpansionI2C_SlaveReadRegisterWithParam(REG_APP_MLX_RAW_CHUNK, rxData[0], chunk, 32)) {
            SendChunkFrames(0x253, chunk);
        }

    } else if (rxHeader.StdId == 0x254 && rxHeader.DLC >= 1) {
        uint8_t chunk[32];
        if (ExpansionI2C_SlaveReadRegisterWithParam(REG_APP_MLX_CALIBRATED_CHUNK, rxData[0], chunk, 32)) {
            SendChunkFrames(0x255, chunk);
        }
    }
}
