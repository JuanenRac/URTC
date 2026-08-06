// =============================================================================
// URTC Expansion Slave Application Firmware - Local sensor bus (I2C2,
// master mode): ADS1115 and MLX90640
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// MLX90640 support now built on Melexis's own official library
// (melexis/MLX90640_API.c/.h, Apache-2.0, copied unmodified into this
// project's melexis/ subfolder - its own copyright/license header is
// left exactly as Melexis wrote it, not replaced with this project's
// own GPL-3.0 header, since that code isn't this project's to relicense).
// This file provides the platform-specific pieces that library expects
// (MLX90640_I2CInit/GeneralReset/Read/Write/FreqSet, per its own
// MLX90640_I2C_Driver.h interface) built on this chip's existing I2C2
// transport, and calls its public API in the sequence its own header
// documents: MLX90640_DumpEE + MLX90640_ExtractParameters once at init
// (EEPROM calibration constants don't change), then
// MLX90640_TriggerMeasurement + MLX90640_GetFrameData + MLX90640_CalculateTo
// per capture. The sub-page interleaving and the full calibration math
// this project couldn't responsibly hand-roll from partial datasheet
// fragments earlier now live entirely inside Melexis's own verified code,
// not reconstructed here.
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
// ADS1115 - unchanged from the earlier version of this file; verified
// against Texas Instruments' own datasheet, no dependency on the
// MLX90640 library below.
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
// MLX90640_I2C_Driver.h implementation - the platform-specific transport
// Melexis's own MLX90640_API.c calls into. Built directly on this file's
// own I2C2_Write8/Read8 above, same pattern as ADS1115's own driver
// pair, just with the 16-bit register addressing this sensor uses
// instead of ADS1115's 8-bit pointer register.
// -----------------------------------------------------------------------
void MLX90640_I2CInit(void) {
    // I2C2 itself is already brought up in slave_main.c
    // (MX_I2C2_Init_Master) before Sensors_Init calls this - nothing
    // sensor-specific needed here beyond that.
}

int MLX90640_I2CGeneralReset(void) {
    // Standard I2C General Call Reset: address 0x00, data byte 0x06 -
    // not MLX90640-specific, this is generic I2C bus protocol every
    // device supporting general call reset responds to the same way.
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
    (void)freq; // no-op - this chip's I2C2 bus speed is fixed at init (MX_I2C2_Init_Master, 100kHz), not runtime-adjustable through this driver layer; the library calling this expecting a live frequency change is the one case its own generic driver interface doesn't map cleanly onto this project's own I2C2 setup
}

// -----------------------------------------------------------------------
// MLX90640 - application-level capture and chunk-serving
// -----------------------------------------------------------------------
static uint8_t mlx_capture_status = MLX_CAPTURE_IDLE;
static paramsMLX90640 mlx_params; // extracted calibration constants - populated once by Sensors_Init, reused by every capture after
static uint16_t mlx_frame_data[834]; // MLX90640_GetFrameData's own documented buffer size - 768 pixels + 64 aux words + frameData[832]/[833] control/subpage metadata, confirmed against its actual implementation rather than assumed
static float mlx_calibrated_frame[768];

void Sensors_Init(void) {
    static uint16_t ee_data[MLX90640_EEPROM_DUMP_NUM]; // static, not a stack local - 1664 bytes comfortably fits this chip's 40KB RAM but not the linker script's own 1KB minimum stack reservation
    MLX90640_I2CInit();
    if (MLX90640_DumpEE(MLX90640_I2C_ADDR, ee_data) == MLX90640_NO_ERROR) {
        MLX90640_ExtractParameters(ee_data, &mlx_params);
    }
    // A DumpEE failure here (sensor not present/not responding) leaves
    // mlx_params zero-initialized - MLX_CAPTURE_ERROR is what
    // MLX90640_TriggerCapture reports if a real capture is attempted
    // against that state, rather than this function itself blocking
    // startup on a sensor that might genuinely not be populated on a
    // given board variant.
}

void MLX90640_TriggerCapture(void) {
    mlx_capture_status = MLX_CAPTURE_BUSY;

    if (MLX90640_TriggerMeasurement(MLX90640_I2C_ADDR) < 0) {
        mlx_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    if (MLX90640_GetFrameData(MLX90640_I2C_ADDR, mlx_frame_data) < 0) {
        mlx_capture_status = MLX_CAPTURE_ERROR;
        return;
    }

    float vdd = MLX90640_GetVdd(mlx_frame_data, &mlx_params);
    float ta = MLX90640_GetTa(mlx_frame_data, &mlx_params);
    (void)vdd; // read for completeness/future use (e.g. exposing supply voltage as its own diagnostic register) - not otherwise consumed here, since MLX90640_CalculateTo below already applies its own internal Vdd compensation without needing this value passed in separately
    // Reflected/ambient temperature (tr) and target emissivity both feed
    // directly into the calibration math - tr defaults to ta-8 per
    // Melexis's own reference examples (a reasonable assumption absent
    // a separate way to measure true reflected temperature), emissivity
    // 0.95 is a common default for non-reflective PCB surfaces/solder
    // mask, not a measured value for this specific board - both worth
    // revisiting once real hardware is available to tune against actual
    // target surfaces this tool inspects.
    float tr = ta - 8.0f;
    float emissivity = 0.95f;
    MLX90640_CalculateTo(mlx_frame_data, &mlx_params, emissivity, tr, mlx_calibrated_frame);

    mlx_capture_status = MLX_CAPTURE_READY;
}

uint8_t MLX90640_GetCaptureStatus(void) {
    return mlx_capture_status;
}

void MLX90640_GetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx_frame_data[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx_frame_data[base+i] & 0xFF);
    }
}

void MLX90640_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        int16_t v = (int16_t)(mlx_calibrated_frame[base+i] * MLX_TEMP_SCALE);
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}
