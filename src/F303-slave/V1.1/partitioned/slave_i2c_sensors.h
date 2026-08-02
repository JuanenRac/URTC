// =============================================================================
// URTC Expansion Slave Application Firmware - Local sensor bus (I2C2,
// master mode): ADS1115 and MLX90640 declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef SLAVE_I2C_SENSORS_H
#define SLAVE_I2C_SENSORS_H

#include <stdint.h>
#include "MLX90640_API.h"

void Sensors_Init(void);

// ADS1115 (doc tool #7, Functional Testing Head)
void ADS1115_Configure(uint16_t config_reg);
void ADS1115_TriggerConversion(void);
int16_t ADS1115_ReadResult(void);

// MLX90640 (doc tool #13, PCB Advanced Inspection) - see slave_i2c_sensors.c
// for why MLX90640_RunCalibration is currently a documented stub rather
// than a full implementation.
void MLX90640_TriggerCapture(void);
uint8_t MLX90640_GetCaptureStatus(void);
// Both Get*Chunk functions serve 16 pixels (32 bytes) per call, chunk
// indices 0-47 covering the full 768-pixel frame - matches
// REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK's own documented layout in
// slave_common.h exactly, since these are what that register's read
// handler in slave_i2c_link.c calls directly.
void MLX90640_GetRawChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX90640_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);

#endif // SLAVE_I2C_SENSORS_H
