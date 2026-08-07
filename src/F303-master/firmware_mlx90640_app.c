// =============================================================================
// URTC Firmware - MLX90640 application-level capture (Basic+MLX9064x
// expansion board, direct connection, expansion_board_type==6)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Same capture/chunk-serving design as the expansion slave chip's own
// MLX90640 support (slave_i2c_sensors.c), and as this board's own
// MLX90641/MLX90642 direct drivers - trigger a measurement, run the
// library's own calibration math once per capture, serve 32-byte pixel
// chunks. 48 chunks (768 pixels at 16 per chunk) - same resolution as
// this board's own MLX90642 direct driver, since both sensors are
// 32x24.
// =============================================================================
#include "firmware_common.h"
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"

#define MLX90640_I2C_ADDR 0x33 // fixed factory address, not configurable - same constant as the expansion slave chip's own copy (slave_common.h)

static uint8_t mlx90640_capture_status = MLX_CAPTURE_IDLE;
static paramsMLX90640 mlx90640_params;
static uint16_t mlx90640_frame_data[834]; // 768 pixels + 64 aux words + frameData[832]/[833] control/subpage metadata, same buffer size already verified for the expansion slave chip's own copy of this same library
static float mlx90640_calibrated_frame[768];

void MLX90640_Direct_Init(void) {
    static uint16_t ee_data[MLX90640_EEPROM_DUMP_NUM]; // static, not a stack local - same reasoning as this board's own MLX90641 direct driver
    MLX90640_I2CInit();
    if (MLX90640_DumpEE(MLX90640_I2C_ADDR, ee_data) == MLX90640_NO_ERROR) {
        MLX90640_ExtractParameters(ee_data, &mlx90640_params);
    }
    // A DumpEE failure here (sensor not present/not populated on this
    // board variant) leaves mlx90640_params zero-initialized -
    // MLX_CAPTURE_ERROR is what a real capture attempt reports against
    // that state.
}

void MLX90640_Direct_TriggerCapture(void) {
    mlx90640_capture_status = MLX_CAPTURE_BUSY;

    if (MLX90640_TriggerMeasurement(MLX90640_I2C_ADDR) < 0) {
        mlx90640_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    if (MLX90640_GetFrameData(MLX90640_I2C_ADDR, mlx90640_frame_data) < 0) {
        mlx90640_capture_status = MLX_CAPTURE_ERROR;
        return;
    }

    float ta = MLX90640_GetTa(mlx90640_frame_data, &mlx90640_params);
    // Same tr/emissivity defaults as every other MLX9064x driver in
    // this project - see the expansion slave chip's own equivalent
    // note (slave_i2c_sensors.c) for the full reasoning.
    float tr = ta - 8.0f;
    float emissivity = 0.95f;
    MLX90640_CalculateTo(mlx90640_frame_data, &mlx90640_params, emissivity, tr, mlx90640_calibrated_frame);

    mlx90640_capture_status = MLX_CAPTURE_READY;
}

uint8_t MLX90640_Direct_GetCaptureStatus(void) {
    return mlx90640_capture_status;
}

void MLX90640_Direct_GetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx90640_frame_data[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx90640_frame_data[base+i] & 0xFF);
    }
}

void MLX90640_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        int16_t v = (int16_t)(mlx90640_calibrated_frame[base+i] * MLX_TEMP_SCALE);
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}
