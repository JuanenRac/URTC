// =============================================================================
// URTC Expansion Slave Application Firmware - Local sensor bus (I2C2,
// master mode): ADS1115 and the MLX9064x family declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
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

// MLX9064x (doc tool #13, PCB Advanced Inspection) - generic dispatch,
// branches internally on mlx_sensor_variant (slave_common.h,
// MLX_VARIANT_*) so slave_i2c_link.c's own register handling never
// needs to know which of the 3 family members is actually populated.
//
// Both Get*Chunk functions serve 16 pixels (32 bytes) per call - chunk
// index range depends on the configured variant (0-47 for MLX90640,
// 0-11 for MLX90641) - matches REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK's
// own documented layout in slave_common.h exactly, since these are what
// that register's read handler in slave_i2c_link.c calls directly.
//
// MLX_TriggerCapture itself is safe to call from I2C1's own ISR context
// (slave_i2c_link.c does exactly that) - for the 90640/90641 variants it
// only ever flags the real, slow capture for MLX_ServiceCapture to run;
// see both functions' own comments in slave_i2c_sensors.c. MLX_ServiceCapture
// must be called once per main-loop iteration (see slave_main.c) for that
// deferred capture to ever actually happen.
void MLX_TriggerCapture(void);
void MLX_ServiceCapture(void);
uint8_t MLX_GetCaptureStatus(void);
void MLX_GetRawChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);

#endif // SLAVE_I2C_SENSORS_H
