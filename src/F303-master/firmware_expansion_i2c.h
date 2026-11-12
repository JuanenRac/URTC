// =============================================================================
// URTC Firmware - CONN_EXPANSION bit-banged I2C bus declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
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
// The same three with the check byte of the application link (urtc_link_crc.h); only for an application built with URTC_SLAVE_LINK_CRC=1.
uint8_t ExpansionI2C_SlaveWriteRegisterCrc(uint8_t reg, const uint8_t *data, uint8_t len);
uint8_t ExpansionI2C_SlaveReadRegisterCrc(uint8_t reg, uint8_t *data, uint8_t len);
uint8_t ExpansionI2C_SlaveReadRegisterWithParamCrc(uint8_t reg, uint8_t param, uint8_t *data, uint8_t len);

// The calls the application registers of the slave go through. With URTC_SLAVE_LINK_CRC=1 (default 0: the link is byte for byte what it always was) they
// are the checked calls above; the slave application must be built with the same value. The registers of the slave BOOTLOADER (REG_BOOT_*) have no check
// byte and always use the plain calls.
#ifndef URTC_SLAVE_LINK_CRC
#define URTC_SLAVE_LINK_CRC 0
#endif
#if URTC_SLAVE_LINK_CRC
#define ExpansionI2C_AppWriteRegister ExpansionI2C_SlaveWriteRegisterCrc
#define ExpansionI2C_AppReadRegister ExpansionI2C_SlaveReadRegisterCrc
#define ExpansionI2C_AppReadRegisterWithParam ExpansionI2C_SlaveReadRegisterWithParamCrc
#else
#define ExpansionI2C_AppWriteRegister ExpansionI2C_SlaveWriteRegister
#define ExpansionI2C_AppReadRegister ExpansionI2C_SlaveReadRegister
#define ExpansionI2C_AppReadRegisterWithParam ExpansionI2C_SlaveReadRegisterWithParam
#endif

#endif // FIRMWARE_EXPANSION_I2C_H
