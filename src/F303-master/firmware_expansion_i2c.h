// =============================================================================
// URTC Firmware - CONN_EXPANSION bit-banged I2C bus declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_EXPANSION_I2C_H
#define FIRMWARE_EXPANSION_I2C_H

#include <stdint.h>

void MX_ExpansionI2C_Init(void);
void ExpansionI2C_Start(void);
void ExpansionI2C_Stop(void);
uint8_t ExpansionI2C_WriteByte(uint8_t byte);
uint8_t ExpansionI2C_ReadByte(uint8_t send_ack);
uint8_t ExpansionI2C_HadTimeout(void);
uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len);
uint8_t ExpansionI2C_SlaveReadRegister(uint8_t reg, uint8_t *data, uint8_t len);
uint8_t ExpansionI2C_SlaveReadRegisterWithParam(uint8_t reg, uint8_t param, uint8_t *data, uint8_t len);

#endif // FIRMWARE_EXPANSION_I2C_H
