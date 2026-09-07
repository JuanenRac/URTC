// =============================================================================
// URTC Firmware - Direct ADS1115 driver (Basic+ADS1115 expansion board)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_ADS1115_H
#define FIRMWARE_ADS1115_H

#include <stdint.h>

void ADS1115_Direct_Configure(uint16_t config_reg);
void ADS1115_Direct_TriggerConversion(void);
int16_t ADS1115_Direct_ReadResult(void);

#endif // FIRMWARE_ADS1115_H
