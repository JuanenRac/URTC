// =============================================================================
// URTC Firmware - MLX90640 platform transport layer (Basic+MLX9064x
// expansion board, direct connection)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The 5 functions Melexis's own vendored library (melexis_mlx90640/)
// expects a platform to provide - MLX90640_I2CInit/GeneralReset/Read/
// Write/FreqSet, per MLX90640_I2C_Driver.h's own interface. Same shape
// and same reasoning as this board's own MLX90641 direct transport
// (melexis_mlx90641/firmware_mlx90641_transport.c) - built on the same
// bit-banged EXP_I2C2_SCL/SDA primitives (firmware_expansion_i2c.c)
// already used to reach the expansion slave chip on advanced boards -
// here reaching the MLX90640 itself directly, no slave chip involved.
// =============================================================================
#include "firmware_common.h"
#include "firmware_expansion_i2c.h"
#include "MLX90640_I2C_Driver.h"

void MLX90640_I2CInit(void) {
    // The bit-banged bus itself is already brought up by
    // MX_ExpansionI2C_Init() at board init, before any tool-specific
    // code (including this) ever runs - nothing sensor-specific needed
    // here beyond that, same as this board's own other direct sensor
    // drivers assume.
}

int MLX90640_I2CGeneralReset(void) {
    // Standard I2C General Call Reset: address 0x00, data byte 0x06 -
    // generic I2C bus protocol, not MLX90640-specific.
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte(0x00);
    if (ack) ack = ExpansionI2C_WriteByte(0x06);
    ExpansionI2C_Stop();
    return ack ? 0 : -1;
}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte((slaveAddr << 1) | 0);
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(startAddress >> 8));
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(startAddress & 0xFF));
    if (!ack) {
        ExpansionI2C_Stop();
        return -1;
    }

    ExpansionI2C_Start(); // repeated START into the read
    ack = ExpansionI2C_WriteByte((slaveAddr << 1) | 1);
    if (!ack) {
        ExpansionI2C_Stop();
        return -1;
    }
    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        uint8_t is_last_word = (i == nMemAddressRead - 1);
        uint8_t msb = ExpansionI2C_ReadByte(1); // ACK - one more byte in this word
        uint8_t lsb = ExpansionI2C_ReadByte(is_last_word ? 0 : 1); // NACK only on the very last byte of the whole read
        data[i] = ((uint16_t)msb << 8) | lsb;
    }
    ExpansionI2C_Stop();
    return 0;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte((slaveAddr << 1) | 0);
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(writeAddress >> 8));
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(writeAddress & 0xFF));
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(data >> 8));
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(data & 0xFF));
    ExpansionI2C_Stop();
    return ack ? 0 : -1;
}

void MLX90640_I2CFreqSet(int freq) {
    (void)freq; // no-op - this board's own bit-banged bus speed is fixed by ExpansionI2C_Delay()'s own timing, not runtime-adjustable through this driver layer, same reasoning as this board's own other direct sensor drivers.
}
