// =============================================================================
// URTC Firmware - CAN command handler: PCB Advanced Inspection
// (0x250-0x255, MLX9064x access - direct (either MLX90641 or MLX90642)
// or via the expansion slave bridge, depending on which board variant
// and sensor variant is configured)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// This tool's own thermal imaging spots PCB shorts by temperature
// signature. Ring-LED illumination isn't handled here - it's the same
// generic ring-LED control every tool already has via 0x100, nothing
// thermal-inspection-specific about it.
//
// Same dual-path reasoning as firmware_can_flyingprobe.c's own ADS1115
// access: a host talks to the same 0x250-0x255 IDs regardless of which
// board variant is actually populated.
//
//   expansion_board_type==6 (Basic+MLX9064x): the sensor is wired
//   directly onto this board's own bit-banged I2C bus, no slave MCU
//   involved. Which of the 2 supported sensors is actually behind it
//   (MLX90641 or MLX90642 - MLX90640 has no direct-path support, only
//   the slave-relayed path below) is decided by mlx_sensor_variant, the
//   same setting that also gets relayed to the slave chip on Advanced
//   boards.
//
//   expansion_board_type==3 or 4 (either Advanced variant): the sensor
//   lives on the expansion slave chip's own LOCAL sensor bus, relayed
//   over firmware_can_slavebridge.c's own I2C bridge - the slave chip's
//   own generic MLX_* dispatch (slave_i2c_sensors.c) picks the right
//   sensor there, this board doesn't need to know which.
//
// The one real complication this tool has that no other advanced-board
// tool does: a pixel chunk is 32 bytes, which doesn't fit in one CAN
// frame's own 8-byte ceiling the way every other advanced-board
// register here does - so a single chunk request gets answered across
// 4 consecutive CAN frames on the same response ID (same convention
// this project's own bootloader HMAC chunk transfer already
// established: 4 frames of 8 bytes, concatenated in the order
// received). Applies identically across every path above - only where
// the 32 bytes actually come from differs.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_thermalinspection.h"
#include "melexis_mlx90641/firmware_mlx90641_app.h"
#include "melexis_mlx90642/firmware_mlx90642_app.h"

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
        if (expansion_board_type == 6 && mlx_sensor_variant == MLX_VARIANT_90642) {
            MLX90642_Direct_TriggerCapture();
        } else if (expansion_board_type == 6) {
            MLX90641_Direct_TriggerCapture(); // MLX_VARIANT_90641, and the safe default (0) - MLX90640 has no direct-path support, see this file's own header comment
        } else {
            ExpansionI2C_SlaveWriteRegister(REG_APP_MLX_TRIGGER_CAPTURE, rxData, 0);
        }

    } else if (rxHeader.StdId == 0x251) {
        uint8_t status = 0;
        uint8_t got_status = 0;
        if (expansion_board_type == 6 && mlx_sensor_variant == MLX_VARIANT_90642) {
            status = MLX90642_Direct_GetCaptureStatus();
            got_status = 1;
        } else if (expansion_board_type == 6) {
            status = MLX90641_Direct_GetCaptureStatus();
            got_status = 1;
        } else {
            got_status = ExpansionI2C_SlaveReadRegister(REG_APP_MLX_CAPTURE_STATUS, &status, 1);
        }
        if (got_status && HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
            CAN_TxHeaderTypeDef txH;
            txH.StdId = 0x251;
            txH.IDE = CAN_ID_STD;
            txH.RTR = CAN_RTR_DATA;
            txH.DLC = 1;
            txH.TransmitGlobalTime = DISABLE;
            uint32_t mb;
            HAL_CAN_AddTxMessage(&hcan, &txH, &status, &mb);
        }

    } else if (rxHeader.StdId == 0x252 && rxHeader.DLC >= 1) {
        uint8_t chunk[32];
        if (expansion_board_type == 6 && mlx_sensor_variant == MLX_VARIANT_90642) {
            MLX90642_Direct_GetRawChunk(rxData[0], chunk);
            SendChunkFrames(0x253, chunk);
        } else if (expansion_board_type == 6) {
            MLX90641_Direct_GetRawChunk(rxData[0], chunk);
            SendChunkFrames(0x253, chunk);
        } else if (ExpansionI2C_SlaveReadRegisterWithParam(REG_APP_MLX_RAW_CHUNK, rxData[0], chunk, 32)) {
            SendChunkFrames(0x253, chunk);
        }

    } else if (rxHeader.StdId == 0x254 && rxHeader.DLC >= 1) {
        uint8_t chunk[32];
        if (expansion_board_type == 6 && mlx_sensor_variant == MLX_VARIANT_90642) {
            MLX90642_Direct_GetCalibratedChunk(rxData[0], chunk);
            SendChunkFrames(0x255, chunk);
        } else if (expansion_board_type == 6) {
            MLX90641_Direct_GetCalibratedChunk(rxData[0], chunk);
            SendChunkFrames(0x255, chunk);
        } else if (ExpansionI2C_SlaveReadRegisterWithParam(REG_APP_MLX_CALIBRATED_CHUNK, rxData[0], chunk, 32)) {
            SendChunkFrames(0x255, chunk);
        }
    }
}
