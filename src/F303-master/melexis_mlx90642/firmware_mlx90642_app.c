// =============================================================================
// URTC Firmware - MLX90642 application-level capture (Basic+MLX9064x
// expansion board, direct connection, expansion_board_type==6)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Genuinely simpler than this project's own MLX90640/MLX90641 support:
// this sensor calculates temperature onboard itself rather than
// expecting the host to run a calibration library against a raw
// EEPROM dump - no DumpEE/ExtractParameters/CalculateTo sequence here
// at all. Step measurement mode is used (not this sensor's own default
// continuous mode) specifically so TriggerCapture below maps onto an
// explicit, host-controlled capture the same way the other 2 sensors'
// own TriggerCapture does, rather than this sensor free-running on its
// own refresh rate independent of what a host actually asked for.
//
// MLX90642_GetFrameData() (not the simpler MLX90642_GetImage()) is used
// deliberately - it reads aux data, raw IR data, AND the calculated
// image in one call, regardless of this sensor's own configured output
// format, which is what actually lets both REG_MLX_RAW_CHUNK and
// REG_MLX_CALIBRATED_CHUNK be served from a single capture the same way
// the other 2 sensors' own chunk-serving already works, rather than
// needing 2 separate captures with the output format reconfigured
// between them.
// =============================================================================
#include "firmware_common.h"
#include "MLX90642.h"
#include "MLX90642_depends.h"

#define MLX90642_I2C_ADDR SA_90642_DEFAULT

static uint8_t mlx90642_capture_status = MLX_CAPTURE_IDLE;
static uint16_t mlx90642_aux[MLX90642_TOTAL_NUMBER_OF_AUX];
static uint16_t mlx90642_rawpix[MLX90642_TOTAL_NUMBER_OF_PIXELS];
static int16_t mlx90642_pixval[MLX90642_TOTAL_NUMBER_OF_PIXELS + 1]; // +1 per MLX90642_GetFrameData's own documented Ta slot

void MLX90642_Direct_Init(void) {
    if (MLX90642_SetMeasMode(MLX90642_I2C_ADDR, MLX90642_STEP_MEAS_MODE) < 0) {
        // Sensor not present/not responding - mlx90642_capture_status
        // stays MLX_CAPTURE_IDLE; a real capture attempt against a
        // silent bus reports MLX_CAPTURE_ERROR on its own, same as the
        // other 2 sensors' own DumpEE-failure handling.
        return;
    }
    MLX90642_SetOutputFormat(MLX90642_I2C_ADDR, MLX90642_TEMPERATURE_OUTPUT);
}

void MLX90642_Direct_TriggerCapture(void) {
    mlx90642_capture_status = MLX_CAPTURE_BUSY;

    if (MLX90642_ClearDataReady(MLX90642_I2C_ADDR) < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    if (MLX90642_StartSync(MLX90642_I2C_ADDR) < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    // Deliberately not polling IsDataReady() to completion here - same
    // non-blocking reasoning as this project's own other 2 sensors'
    // TriggerCapture: the actual frame read happens in
    // MLX90642_Direct_GetCaptureStatus() below, on whichever later poll
    // first finds the sensor's own data-ready flag set.
}

uint8_t MLX90642_Direct_GetCaptureStatus(void) {
    if (mlx90642_capture_status != MLX_CAPTURE_BUSY) {
        return mlx90642_capture_status;
    }

    int ready = MLX90642_IsDataReady(MLX90642_I2C_ADDR);
    if (ready < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return mlx90642_capture_status;
    }
    if (ready != MLX90642_YES) {
        return MLX_CAPTURE_BUSY; // still waiting, unchanged
    }

    if (MLX90642_GetFrameData(MLX90642_I2C_ADDR, mlx90642_aux, mlx90642_rawpix, mlx90642_pixval) < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return mlx90642_capture_status;
    }

    mlx90642_capture_status = MLX_CAPTURE_READY;
    return mlx90642_capture_status;
}

// 48 chunks (768px / 16 per chunk), same as MLX90640's own layout -
// this sensor shares the same 32x24 resolution.
void MLX90642_Direct_GetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx90642_rawpix[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx90642_rawpix[base+i] & 0xFF);
    }
}

void MLX90642_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        // This sensor's own MLX90642_TEMPERATURE_OUTPUT is already
        // degC*50 (per MLX90642_depends.h's own comment on the example
        // buffer), not degC*100 like this project's own MLX_TEMP_SCALE
        // convention for the other 2 sensors - rescaled here so a host
        // reading REG_MLX_CALIBRATED_CHUNK never needs to know which
        // sensor variant answered it.
        int32_t rescaled = ((int32_t)mlx90642_pixval[base+i] * MLX_TEMP_SCALE) / 50;
        int16_t v = (int16_t)rescaled;
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}
