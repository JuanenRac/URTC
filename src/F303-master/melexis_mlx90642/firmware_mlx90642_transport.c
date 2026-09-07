// =============================================================================
// URTC Firmware - MLX90642 platform transport layer (Basic+MLX9064x
// expansion board, direct connection)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The 3 functions Melexis's own vendored library (melexis_mlx90642/)
// expects a platform to provide - MLX90642_I2CRead/I2CWrite/Wait_ms, per
// MLX90642_depends.h's own interface. Genuinely simpler than the
// MLX90640/MLX90641 transports (5 functions each): this sensor's own
// Config/I2CCmd/WakeUp are implemented directly inside the vendored
// MLX90642.c itself (each one builds its own exact byte buffer using
// this sensor's own documented opcodes, then calls this file's own
// generic I2CWrite) - not left for a platform driver to interpret, so
// there's no risk of this project guessing the wrong byte layout for
// any of those 3 commands. Same bit-banged EXP_I2C2_SCL/SDA primitives
// (firmware_expansion_i2c.c) already used by this board's own ADS1115
// and MLX90641 direct drivers.
// =============================================================================
#include "firmware_common.h"
#include "firmware_expansion_i2c.h"
#include "MLX90642_depends.h"

int MLX90642_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *rData) {
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
        rData[i] = ((uint16_t)msb << 8) | lsb;
    }
    ExpansionI2C_Stop();
    return 0;
}

int MLX90642_I2CWrite(uint8_t slaveAddr, const uint8_t *buffer, uint8_t bytesNum) {
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte((slaveAddr << 1) | 0);
    for (uint8_t i = 0; i < bytesNum && ack; i++) {
        ack = ExpansionI2C_WriteByte(buffer[i]);
    }
    ExpansionI2C_Stop();
    return ack ? 0 : -1;
}

void MLX90642_Wait_ms(uint16_t time_ms) {
    HAL_Delay(time_ms);
}
