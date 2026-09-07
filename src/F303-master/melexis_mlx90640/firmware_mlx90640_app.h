// =============================================================================
// URTC Firmware - MLX90640 application-level capture (Basic+MLX9064x
// expansion board, direct connection)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_MLX90640_APP_H
#define FIRMWARE_MLX90640_APP_H

#include <stdint.h>

void MLX90640_Direct_Init(void);
void MLX90640_Direct_TriggerCapture(void);
uint8_t MLX90640_Direct_GetCaptureStatus(void);
void MLX90640_Direct_GetRawChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX90640_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);

#endif // FIRMWARE_MLX90640_APP_H
