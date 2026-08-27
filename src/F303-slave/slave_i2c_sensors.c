// =============================================================================
// URTC Expansion Slave Application Firmware - Local sensor bus (I2C2,
// master mode): ADS1115 and the MLX9064x family
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// MLX90640 support built on Melexis's own official library
// (melexis_mlx90640/MLX90640_API.c/.h, Apache-2.0, copied unmodified into this
// project's melexis_mlx90640/ subfolder - its own copyright/license header is
// left exactly as Melexis wrote it, not replaced with this project's
// own GPL-3.0 header, since that code isn't this project's to relicense).
// MLX90641 support built the same way on melexis_mlx90641/MLX90641_API.h/.cpp
// (also Apache-2.0, also unmodified except an extern "C" linkage wrapper
// documented in that file itself) - this is a genuinely separate
// Melexis library, not a variant of the 90640's own one, hence the
// separate vendored folder and the "90641" name throughout this file's
// own second half.
//
// Both libraries expect a platform to provide the same 5-function shape
// (I2CInit/GeneralReset/Read/Write/FreqSet, per each library's own
// I2C_Driver.h), built here on this chip's existing I2C2 transport, and
// call their own public API in the sequence each one's own header
// documents: DumpEE + ExtractParameters once at init (EEPROM calibration
// constants don't change), then TriggerMeasurement + GetFrameData +
// CalculateTo per capture.
//
// Only ONE of the 3 MLX9064x family members is ever actually populated
// on a real board - mlx_sensor_variant (slave_common.h) says which.
// MLX_TriggerCapture/GetCaptureStatus/GetRawChunk/GetCalibratedChunk
// below are the single generic entry points slave_i2c_link.c's own
// register dispatch calls - they branch on that variant internally, so
// neither that dispatch code nor the main board's own CAN handlers on
// the other end of the link bus need to know or care which sensor is
// actually behind them.
// =============================================================================
#include "stm32f3xx_hal.h"
#include "slave_common.h"
#include "slave_i2c_sensors.h"

// -----------------------------------------------------------------------
// Generic I2C2 master transport
// -----------------------------------------------------------------------
#define I2C2_TIMEOUT_MS 50

static uint8_t I2C2_Write8(uint8_t addr, const uint8_t *data, uint16_t len) {
    return HAL_I2C_Master_Transmit(&hi2c2, addr << 1, (uint8_t*)data, len, I2C2_TIMEOUT_MS) == HAL_OK;
}

static uint8_t I2C2_Read8(uint8_t addr, uint8_t *data, uint16_t len) {
    return HAL_I2C_Master_Receive(&hi2c2, addr << 1, data, len, I2C2_TIMEOUT_MS) == HAL_OK;
}

// -----------------------------------------------------------------------
// ADS1115 - unchanged; verified against Texas Instruments' own
// datasheet, no dependency on either MLX9064x library below.
// -----------------------------------------------------------------------
#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG     0x01
#define ADS1115_OS_START_SINGLE (1 << 15)

static uint16_t ads1115_last_config = 0x8583;

void ADS1115_Configure(uint16_t config_reg) {
    ads1115_last_config = config_reg;
    uint8_t buf[3];
    buf[0] = ADS1115_REG_CONFIG;
    buf[1] = (uint8_t)(config_reg >> 8);
    buf[2] = (uint8_t)(config_reg & 0xFF);
    I2C2_Write8(ADS1115_I2C_ADDR, buf, 3);
}

void ADS1115_TriggerConversion(void) {
    uint16_t start_cfg = ads1115_last_config | ADS1115_OS_START_SINGLE;
    uint8_t buf[3];
    buf[0] = ADS1115_REG_CONFIG;
    buf[1] = (uint8_t)(start_cfg >> 8);
    buf[2] = (uint8_t)(start_cfg & 0xFF);
    I2C2_Write8(ADS1115_I2C_ADDR, buf, 3);
}

int16_t ADS1115_ReadResult(void) {
    uint8_t ptr = ADS1115_REG_CONVERSION;
    if (!I2C2_Write8(ADS1115_I2C_ADDR, &ptr, 1)) return 0;
    uint8_t raw[2];
    if (!I2C2_Read8(ADS1115_I2C_ADDR, raw, 2)) return 0;
    return (int16_t)((raw[0] << 8) | raw[1]);
}

// -----------------------------------------------------------------------
// MLX90640_I2C_Driver.h implementation - built on this file's own
// I2C2_Write8/Read8 above, 16-bit register addressing.
// -----------------------------------------------------------------------
void MLX90640_I2CInit(void) {
    // I2C2 itself is already brought up in slave_main.c
    // (MX_I2C2_Init_Master) before Sensors_Init calls this - nothing
    // sensor-specific needed here beyond that.
}

int MLX90640_I2CGeneralReset(void) {
    uint8_t reset_cmd = 0x06;
    return I2C2_Write8(0x00, &reset_cmd, 1) ? MLX90640_NO_ERROR : -MLX90640_I2C_NACK_ERROR;
}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
    uint8_t addr_buf[2] = {(uint8_t)(startAddress >> 8), (uint8_t)(startAddress & 0xFF)};
    if (!I2C2_Write8(slaveAddr, addr_buf, 2)) return -MLX90640_I2C_NACK_ERROR;
    uint8_t raw[2];
    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        if (!I2C2_Read8(slaveAddr, raw, 2)) return -MLX90640_I2C_NACK_ERROR;
        data[i] = ((uint16_t)raw[0] << 8) | raw[1];
    }
    return MLX90640_NO_ERROR;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(writeAddress >> 8);
    buf[1] = (uint8_t)(writeAddress & 0xFF);
    buf[2] = (uint8_t)(data >> 8);
    buf[3] = (uint8_t)(data & 0xFF);
    if (!I2C2_Write8(slaveAddr, buf, 4)) return -MLX90640_I2C_WRITE_ERROR;
    return MLX90640_NO_ERROR;
}

void MLX90640_I2CFreqSet(int freq) {
    (void)freq; // no-op - see this file's own header comment
}

// -----------------------------------------------------------------------
// MLX90640 - application-level capture and chunk-serving state
// -----------------------------------------------------------------------
static uint8_t mlx90640_capture_status = MLX_CAPTURE_IDLE;
static paramsMLX90640 mlx90640_params;
static uint16_t mlx90640_frame_data[834]; // 768 pixels + 64 aux words + frameData[832]/[833] control/subpage metadata, confirmed against MLX90640_GetFrameData's own actual implementation
static float mlx90640_calibrated_frame[768];

static void MLX90640_DoInit(void) {
    static uint16_t ee_data[MLX90640_EEPROM_DUMP_NUM]; // static, not a stack local - fits this chip's 40KB RAM but not the linker script's own 1KB minimum stack reservation
    MLX90640_I2CInit();
    if (MLX90640_DumpEE(MLX90640_I2C_ADDR, ee_data) == MLX90640_NO_ERROR) {
        MLX90640_ExtractParameters(ee_data, &mlx90640_params);
    }
    // A DumpEE failure here (sensor not present/not responding) leaves
    // mlx90640_params zero-initialized - MLX_CAPTURE_ERROR is what a
    // real capture attempt reports against that state.
}

static void MLX90640_DoTriggerCapture(void) {
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
    // tr defaults to ta-8 per Melexis's own reference examples,
    // emissivity 0.95 is a common non-reflective PCB/solder-mask
    // default - neither measured for this specific board, worth
    // revisiting once real hardware exists.
    float tr = ta - 8.0f;
    float emissivity = 0.95f;
    MLX90640_CalculateTo(mlx90640_frame_data, &mlx90640_params, emissivity, tr, mlx90640_calibrated_frame);

    mlx90640_capture_status = MLX_CAPTURE_READY;
}

static void MLX90640_DoGetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx90640_frame_data[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx90640_frame_data[base+i] & 0xFF);
    }
}

static void MLX90640_DoGetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        // Saturate rather than narrow-cast directly - a pixel above
        // ~327.67C (or below -327.68C) would otherwise silently wrap
        // instead of reporting a real, if clamped, reading - see the main
        // board's own direct driver (firmware_mlx90640_app.c) for the
        // same fix.
        float scaled = mlx90640_calibrated_frame[base+i] * MLX_TEMP_SCALE;
        int16_t v = (scaled > 32767.0f) ? 32767 : (scaled < -32768.0f) ? -32768 : (int16_t)scaled;
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}

// -----------------------------------------------------------------------
// MLX90641_I2C_Driver.h implementation - same transport pattern as the
// MLX90640 block above, reusing this file's own I2C2_Write8/Read8.
// -----------------------------------------------------------------------
void MLX90641_I2CInit(void) {
}

int MLX90641_I2CGeneralReset(void) {
    uint8_t reset_cmd = 0x06;
    return I2C2_Write8(0x00, &reset_cmd, 1) ? 0 : -1;
}

int MLX90641_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
    uint8_t addr_buf[2] = {(uint8_t)(startAddress >> 8), (uint8_t)(startAddress & 0xFF)};
    if (!I2C2_Write8(slaveAddr, addr_buf, 2)) return -1;
    uint8_t raw[2];
    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        if (!I2C2_Read8(slaveAddr, raw, 2)) return -1;
        data[i] = ((uint16_t)raw[0] << 8) | raw[1];
    }
    return 0;
}

int MLX90641_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(writeAddress >> 8);
    buf[1] = (uint8_t)(writeAddress & 0xFF);
    buf[2] = (uint8_t)(data >> 8);
    buf[3] = (uint8_t)(data & 0xFF);
    if (!I2C2_Write8(slaveAddr, buf, 4)) return -1;
    // Read back and verify, same as Melexis's own reference drivers -
    // matches this project's own Basic+MLX9064x direct-connection
    // transport's reasoning (firmware_mlx90641_transport.c on the main
    // board) for the same library on a different bus.
    uint16_t readback;
    if (MLX90641_I2CRead(slaveAddr, writeAddress, 1, &readback) != 0) return -1;
    if (readback != data) return -2;
    return 0;
}

void MLX90641_I2CFreqSet(int freq) {
    (void)freq;
}

// -----------------------------------------------------------------------
// MLX90641 - application-level capture and chunk-serving state
// -----------------------------------------------------------------------
#define MLX90641_I2C_ADDR 0x33
#define MLX90641_EEPROM_DUMP_NUM 832

static uint8_t mlx90641_capture_status = MLX_CAPTURE_IDLE;
static paramsMLX90641 mlx90641_params;
static uint16_t mlx90641_frame_data[242]; // 192 pixels + 48 aux words + frameData[240]/[241] control/subpage metadata, confirmed against the vendored MLX90641_GetFrameData's own actual indexing
static float mlx90641_calibrated_frame[192];

static void MLX90641_DoInit(void) {
    static uint16_t ee_data[MLX90641_EEPROM_DUMP_NUM];
    MLX90641_I2CInit();
    if (MLX90641_DumpEE(MLX90641_I2C_ADDR, ee_data) == 0) {
        MLX90641_ExtractParameters(ee_data, &mlx90641_params);
    }
}

static void MLX90641_DoTriggerCapture(void) {
    mlx90641_capture_status = MLX_CAPTURE_BUSY;

    if (MLX90641_TriggerMeasurement(MLX90641_I2C_ADDR) < 0) {
        mlx90641_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    if (MLX90641_GetFrameData(MLX90641_I2C_ADDR, mlx90641_frame_data) < 0) {
        mlx90641_capture_status = MLX_CAPTURE_ERROR;
        return;
    }

    float ta = MLX90641_GetTa(mlx90641_frame_data, &mlx90641_params);
    float tr = ta - 8.0f;
    float emissivity = 0.95f;
    MLX90641_CalculateTo(mlx90641_frame_data, &mlx90641_params, emissivity, tr, mlx90641_calibrated_frame);

    mlx90641_capture_status = MLX_CAPTURE_READY;
}

static void MLX90641_DoGetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 12) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx90641_frame_data[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx90641_frame_data[base+i] & 0xFF);
    }
}

static void MLX90641_DoGetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 12) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        // Saturate rather than narrow-cast directly - see
        // MLX90640_DoGetCalibratedChunk above for the full reasoning.
        float scaled = mlx90641_calibrated_frame[base+i] * MLX_TEMP_SCALE;
        int16_t v = (scaled > 32767.0f) ? 32767 : (scaled < -32768.0f) ? -32768 : (int16_t)scaled;
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}

// -----------------------------------------------------------------------
// MLX90642_depends.h implementation - genuinely simpler transport than
// the other 2 sensors' own 5-function shape: this sensor's own Config/
// I2CCmd/WakeUp are already fully implemented inside the vendored
// MLX90642.c itself (each builds its own exact byte buffer using this
// sensor's own documented opcodes, then calls the generic I2CWrite
// below) - only 3 truly platform-specific functions needed here.
// -----------------------------------------------------------------------
int MLX90642_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *rData) {
    uint8_t addr_buf[2] = {(uint8_t)(startAddress >> 8), (uint8_t)(startAddress & 0xFF)};
    if (!I2C2_Write8(slaveAddr, addr_buf, 2)) return -1;
    uint8_t raw[2];
    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        if (!I2C2_Read8(slaveAddr, raw, 2)) return -1;
        rData[i] = ((uint16_t)raw[0] << 8) | raw[1];
    }
    return 0;
}

int MLX90642_I2CWrite(uint8_t slaveAddr, const uint8_t *buffer, uint8_t bytesNum) {
    return I2C2_Write8(slaveAddr, buffer, bytesNum) ? 0 : -1;
}

void MLX90642_Wait_ms(uint16_t time_ms) {
    HAL_Delay(time_ms);
}

// -----------------------------------------------------------------------
// MLX90642 - application-level capture and chunk-serving state. Same
// MLX90642_GetFrameData()-based design as this project's own direct-
// connection MLX90642 driver on the main board
// (firmware_mlx90642_app.c) - see that file's own header comment for
// why GetFrameData rather than the simpler GetImage.
// -----------------------------------------------------------------------
#define MLX90642_SLAVE_I2C_ADDR SA_90642_DEFAULT

static uint8_t mlx90642_capture_status = MLX_CAPTURE_IDLE;
static uint16_t mlx90642_aux[MLX90642_TOTAL_NUMBER_OF_AUX];
static uint16_t mlx90642_rawpix[MLX90642_TOTAL_NUMBER_OF_PIXELS];
static int16_t mlx90642_pixval[MLX90642_TOTAL_NUMBER_OF_PIXELS + 1];

static void MLX90642_DoInit(void) {
    if (MLX90642_SetMeasMode(MLX90642_SLAVE_I2C_ADDR, MLX90642_STEP_MEAS_MODE) < 0) {
        return;
    }
    MLX90642_SetOutputFormat(MLX90642_SLAVE_I2C_ADDR, MLX90642_TEMPERATURE_OUTPUT);
}

static void MLX90642_DoTriggerCapture(void) {
    mlx90642_capture_status = MLX_CAPTURE_BUSY;

    if (MLX90642_ClearDataReady(MLX90642_SLAVE_I2C_ADDR) < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    if (MLX90642_StartSync(MLX90642_SLAVE_I2C_ADDR) < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
}

static uint8_t MLX90642_DoGetCaptureStatus(void) {
    if (mlx90642_capture_status != MLX_CAPTURE_BUSY) {
        return mlx90642_capture_status;
    }

    int ready = MLX90642_IsDataReady(MLX90642_SLAVE_I2C_ADDR);
    if (ready < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return mlx90642_capture_status;
    }
    if (ready != MLX90642_YES) {
        return MLX_CAPTURE_BUSY;
    }

    if (MLX90642_GetFrameData(MLX90642_SLAVE_I2C_ADDR, mlx90642_aux, mlx90642_rawpix, mlx90642_pixval) < 0) {
        mlx90642_capture_status = MLX_CAPTURE_ERROR;
        return mlx90642_capture_status;
    }

    mlx90642_capture_status = MLX_CAPTURE_READY;
    return mlx90642_capture_status;
}

static void MLX90642_DoGetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx90642_rawpix[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx90642_rawpix[base+i] & 0xFF);
    }
}

static void MLX90642_DoGetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        // This sensor's own MLX90642_TEMPERATURE_OUTPUT is degC*50, not
        // this project's own MLX_TEMP_SCALE (degC*100) - rescaled here,
        // same as the main board's own direct driver does.
        int32_t rescaled = ((int32_t)mlx90642_pixval[base+i] * MLX_TEMP_SCALE) / 50;
        // Saturate rather than narrow-cast directly - see
        // MLX90640_DoGetCalibratedChunk above for the full reasoning.
        int16_t v = (rescaled > 32767) ? 32767 : (rescaled < -32768) ? -32768 : (int16_t)rescaled;
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}

// -----------------------------------------------------------------------
// Generic dispatch - the only entry points slave_i2c_link.c's own
// register handling calls. Branches on mlx_sensor_variant
// (slave_common.h, MLX_VARIANT_*) so the link-bus protocol and the main
// board's own CAN handlers never need to know which sensor is actually
// behind them. MLX_VARIANT_NONE (the safe default, no sensor
// configured) is handled explicitly in every function below rather than
// falling through to MLX90640's own code path - guessing which real
// sensor might be there and reading garbage from it would be worse than
// simply reporting nothing configured.
// -----------------------------------------------------------------------
void Sensors_Init(void) {
    if (mlx_sensor_variant == MLX_VARIANT_90640) {
        MLX90640_DoInit();
    } else if (mlx_sensor_variant == MLX_VARIANT_90641) {
        MLX90641_DoInit();
    } else if (mlx_sensor_variant == MLX_VARIANT_90642) {
        MLX90642_DoInit();
    }
    // MLX_VARIANT_NONE: nothing to initialize.
}

void MLX_TriggerCapture(void) {
    if (mlx_sensor_variant == MLX_VARIANT_90640) {
        MLX90640_DoTriggerCapture();
    } else if (mlx_sensor_variant == MLX_VARIANT_90641) {
        MLX90641_DoTriggerCapture();
    } else if (mlx_sensor_variant == MLX_VARIANT_90642) {
        MLX90642_DoTriggerCapture();
    }
    // MLX_VARIANT_NONE: no sensor configured - deliberately does
    // nothing, status stays MLX_CAPTURE_IDLE rather than silently
    // succeeding against nothing.
}

uint8_t MLX_GetCaptureStatus(void) {
    if (mlx_sensor_variant == MLX_VARIANT_90640) return mlx90640_capture_status;
    if (mlx_sensor_variant == MLX_VARIANT_90641) return mlx90641_capture_status;
    if (mlx_sensor_variant == MLX_VARIANT_90642) return MLX90642_DoGetCaptureStatus();
    return MLX_CAPTURE_ERROR; // MLX_VARIANT_NONE - answered explicitly
                              // rather than silently, so a host polling
                              // this can tell "no sensor configured"
                              // apart from "chip not responding at all".
}

void MLX_GetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (mlx_sensor_variant == MLX_VARIANT_90640) {
        MLX90640_DoGetRawChunk(chunk_index, out);
    } else if (mlx_sensor_variant == MLX_VARIANT_90641) {
        MLX90641_DoGetRawChunk(chunk_index, out);
    } else if (mlx_sensor_variant == MLX_VARIANT_90642) {
        MLX90642_DoGetRawChunk(chunk_index, out);
    } else {
        for (int i = 0; i < 32; i++) out[i] = 0; // MLX_VARIANT_NONE
    }
}

void MLX_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (mlx_sensor_variant == MLX_VARIANT_90640) {
        MLX90640_DoGetCalibratedChunk(chunk_index, out);
    } else if (mlx_sensor_variant == MLX_VARIANT_90641) {
        MLX90641_DoGetCalibratedChunk(chunk_index, out);
    } else if (mlx_sensor_variant == MLX_VARIANT_90642) {
        MLX90642_DoGetCalibratedChunk(chunk_index, out);
    } else {
        for (int i = 0; i < 32; i++) out[i] = 0; // MLX_VARIANT_NONE
    }
}

