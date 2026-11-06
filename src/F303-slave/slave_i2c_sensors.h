// =============================================================================
// URTC Expansion Slave Application Firmware - Local sensor bus (I2C2,
// master mode): ADS1115 and the MLX9064x family declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef SLAVE_I2C_SENSORS_H
#define SLAVE_I2C_SENSORS_H

#include <stdint.h>
#include "MLX90640_API.h"
#include "MLX90641_API.h"
#include "MLX90642.h"

void Sensors_Init(void);

// ADS1115 (doc tool #7, Functional Testing Head)
void ADS1115_Configure(uint16_t config_reg);
void ADS1115_TriggerConversion(void);
int16_t ADS1115_ReadResult(void);

// MLX9064x (doc tool #13, PCB Advanced Inspection) - generic dispatch on
// mlx_sensor_variant (slave_common.h), so slave_i2c_link.c never needs to
// know which family member is populated.
//
// Both Get*Chunk functions serve 16 pixels (32 bytes) per call; the chunk
// index runs 0-47 for the MLX90640 and 0-11 for the MLX90641, as laid out
// for REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK in slave_common.h.
//
// MLX_TriggerCapture is safe to call from the I2C1 ISR: for the 90640/90641
// it only flags the slow capture, which MLX_ServiceCapture runs. Call
// MLX_ServiceCapture once per main-loop iteration (slave_main.c).
void MLX_TriggerCapture(void);
void MLX_ServiceCapture(void);
uint8_t MLX_GetCaptureStatus(void);
void MLX_GetRawChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);

#endif // SLAVE_I2C_SENSORS_H
