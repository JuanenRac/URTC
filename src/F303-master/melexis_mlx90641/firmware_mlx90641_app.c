// =============================================================================
// URTC Firmware - MLX90641 application-level capture (Basic+MLX9064x
// expansion board, direct connection, expansion_board_type==6)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Same capture/chunk-serving design as the expansion slave chip's own
// MLX90640 support (slave_i2c_sensors.c) - trigger a measurement, pull
// the raw frame, run Melexis's own calibration math once per capture,
// serve both raw and calibrated pixel data in 32-byte chunks. The real
// difference from that file: this sensor is 16x12 (192 pixels, 12
// chunks of 16px each) rather than 32x24 (768 pixels, 48 chunks) - see
// MLX90641_API.h's own paramsMLX90641 struct, sized for 192 throughout.
// =============================================================================
#include "firmware_common.h"
#include "MLX90641_API.h"
#include "MLX90641_I2C_Driver.h"

#define MLX90641_I2C_ADDR 0x33
#define MLX90641_EEPROM_DUMP_NUM 832 // per MLX90641_DumpEE's own fixed read size (0x2400, 832 words) - confirmed against the vendored MLX90641_API.cpp itself, not assumed from the 90640's own different dump size
// MLX_TEMP_SCALE is defined in firmware_common.h, shared with the MLX90642 direct driver's own copy

static uint8_t mlx641_capture_status = MLX_CAPTURE_IDLE;
static paramsMLX90641 mlx641_params;
static uint16_t mlx641_frame_data[242]; // 192 pixels + 48 aux words + frameData[240]/[241] control/subpage metadata, confirmed against MLX90641_GetFrameData's own actual indexing rather than assumed by analogy with the 90640's own different layout
static float mlx641_calibrated_frame[192];

void MLX90641_Direct_Init(void) {
    static uint16_t ee_data[MLX90641_EEPROM_DUMP_NUM]; // static, not a stack local - same reasoning as the slave chip's own equivalent
    MLX90641_I2CInit();
    if (MLX90641_DumpEE(MLX90641_I2C_ADDR, ee_data) == 0) {
        MLX90641_ExtractParameters(ee_data, &mlx641_params);
    }
    // A DumpEE failure here (sensor not present/not populated on this
    // board variant) leaves mlx641_params zero-initialized -
    // MLX_CAPTURE_ERROR is what MLX90641_Direct_TriggerCapture reports
    // if a real capture is attempted against that state.
}

void MLX90641_Direct_TriggerCapture(void) {
    mlx641_capture_status = MLX_CAPTURE_BUSY;

    if (MLX90641_TriggerMeasurement(MLX90641_I2C_ADDR) < 0) {
        mlx641_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    if (MLX90641_GetFrameData(MLX90641_I2C_ADDR, mlx641_frame_data) < 0) {
        mlx641_capture_status = MLX_CAPTURE_ERROR;
        return;
    }

    float ta = MLX90641_GetTa(mlx641_frame_data, &mlx641_params);
    // Same tr/emissivity defaults as the slave chip's own MLX90640
    // support - see that file's own note on why (Melexis's own
    // reference-example default for tr, a typical non-reflective
    // PCB/solder-mask emissivity, neither measured for this specific
    // board - worth revisiting once real hardware exists).
    float tr = ta - 8.0f;
    float emissivity = 0.95f;
    MLX90641_CalculateTo(mlx641_frame_data, &mlx641_params, emissivity, tr, mlx641_calibrated_frame);

    mlx641_capture_status = MLX_CAPTURE_READY;
}

uint8_t MLX90641_Direct_GetCaptureStatus(void) {
    return mlx641_capture_status;
}

// 12 chunks (192 pixels / 16 per chunk), not 48 - this sensor's own
// lower resolution. A chunk_index the caller expects to mean "48
// chunks of a 90640" simply reads back zeros past 11 here - the CAN
// handler dispatching to this file already only does so when
// expansion_board_type==6, so a host driving this sensor already knows
// its own real chunk count from EXPANSION.TXT rather than guessing 48.
void MLX90641_Direct_GetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 12) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx641_frame_data[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx641_frame_data[base+i] & 0xFF);
    }
}

void MLX90641_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 12) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        // Saturate rather than narrow-cast directly - see
        // firmware_mlx90640_app.c's own equivalent comment for the full
        // reasoning (same fix, same failure mode, same convention).
        float scaled = mlx641_calibrated_frame[base+i] * MLX_TEMP_SCALE;
        int16_t v = (scaled > 32767.0f) ? 32767 : (scaled < -32768.0f) ? -32768 : (int16_t)scaled;
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}
