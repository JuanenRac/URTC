// =============================================================================
// URTC Firmware - Direct ADS1115 driver (Basic+ADS1115 expansion board)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #7 (Functional Testing Head)'s own "advanced reading" path
// normally goes through the expansion slave chip's own local sensor bus,
// relayed over CAN via firmware_can_slavebridge.c. This file is the
// OTHER way to reach an ADS1115: the Basic+ADS1115 expansion board
// variant (expansion_board_type==5, see EXPANSION.TXT) has no slave MCU
// at all - just the ADS1115 itself, wired directly onto the same
// bit-banged I2C bus (EXP_I2C2_SCL/SDA) this board already uses to
// reach the slave chip on advanced variants. Same physical bus,
// different device at a different address (0x48 here vs. the slave's
// own 0x42), talked to with the ADS1115's own real protocol instead of
// the slave's custom REG_* one - there's no slave chip here to relay
// through, so this chip has to speak ADS1115 itself.
//
// Protocol verified against Texas Instruments' own datasheet, same as
// the slave chip's own copy of this same driver (slave_i2c_sensors.c) -
// duplicated here rather than shared, since the two chips reach this
// same chip family over two different transport layers (bit-banged
// here, hardware I2C2 there) that don't share an underlying function
// signature to unify around.
// =============================================================================
#include "firmware_common.h"
#include "firmware_ads1115.h"
#include "firmware_expansion_i2c.h"

#define ADS1115_I2C_ADDR        0x48
#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01
#define ADS1115_OS_START_SINGLE (1 << 15)

static uint16_t ads1115_direct_last_config = 0x8583; // power-on-reset default per the datasheet

void ADS1115_Direct_Configure(uint16_t config_reg) {
    ads1115_direct_last_config = config_reg;
    ExpansionI2C_Start();
    ExpansionI2C_WriteByte((ADS1115_I2C_ADDR << 1) | 0);
    ExpansionI2C_WriteByte(ADS1115_REG_CONFIG);
    ExpansionI2C_WriteByte((uint8_t)(config_reg >> 8));
    ExpansionI2C_WriteByte((uint8_t)(config_reg & 0xFF));
    ExpansionI2C_Stop();
}

void ADS1115_Direct_TriggerConversion(void) {
    uint16_t start_cfg = ads1115_direct_last_config | ADS1115_OS_START_SINGLE;
    ExpansionI2C_Start();
    ExpansionI2C_WriteByte((ADS1115_I2C_ADDR << 1) | 0);
    ExpansionI2C_WriteByte(ADS1115_REG_CONFIG);
    ExpansionI2C_WriteByte((uint8_t)(start_cfg >> 8));
    ExpansionI2C_WriteByte((uint8_t)(start_cfg & 0xFF));
    ExpansionI2C_Stop();
    // Not polling the OS bit for conversion-complete here deliberately -
    // same non-blocking reasoning as the slave chip's own copy of this
    // driver: REG read arrives as a separate CAN command, by which time
    // the ADS1115's own conversion (a few ms at the default 128SPS) has
    // already finished.
}

int16_t ADS1115_Direct_ReadResult(void) {
    ExpansionI2C_Start();
    if (!ExpansionI2C_WriteByte((ADS1115_I2C_ADDR << 1) | 0)) { ExpansionI2C_Stop(); return 0; }
    if (!ExpansionI2C_WriteByte(ADS1115_REG_CONVERSION)) { ExpansionI2C_Stop(); return 0; }
    ExpansionI2C_Start(); // repeated START into the read
    if (!ExpansionI2C_WriteByte((ADS1115_I2C_ADDR << 1) | 1)) { ExpansionI2C_Stop(); return 0; }
    uint8_t msb = ExpansionI2C_ReadByte(1); // ACK - one more byte to follow
    uint8_t lsb = ExpansionI2C_ReadByte(0); // NACK - last byte
    ExpansionI2C_Stop();
    return (int16_t)((msb << 8) | lsb);
}
