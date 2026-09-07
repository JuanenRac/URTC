// =============================================================================
// URTC Firmware - MLX90641 platform transport layer (Basic+MLX9064x
// expansion board, direct connection)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The 5 functions Melexis's own vendored library (melexis_mlx90641/)
// expects a platform to provide - MLX90641_I2CInit/GeneralReset/Read/
// Write/FreqSet, per melexis_mlx90641/MLX90641_I2C_Driver.h's own
// interface. Same reasoning and same 5-function shape as this board's
// own MLX90640 support on the expansion slave chip (slave_i2c_sensors.c)
// and its own melexis/ library - the difference here is the transport
// underneath: this board has no hardware I2C2 peripheral free for local
// sensors (its own I2C2 is the OLED's bus), so these 5 functions are
// built on the same bit-banged EXP_I2C2_SCL/SDA primitives
// (firmware_expansion_i2c.c) already used to reach the expansion slave
// chip on advanced boards - here reaching the MLX90641 itself directly,
// no slave chip involved, same as firmware_ads1115.c's own reasoning
// for the ADS1115.
//
// Reference behavior confirmed against Melexis's own MLX90641_I2C_Driver.cpp
// (hardware I2C, mbed) and MLX90641_SWI2C_Driver.cpp (bit-banged, mbed)
// example implementations - same read-pointer-then-read-data sequence,
// same write-then-read-back verification on MLX90641_I2CWrite.
// =============================================================================
#include "firmware_common.h"
#include "firmware_expansion_i2c.h"
#include "MLX90641_I2C_Driver.h"

#define MLX90641_DEFAULT_I2C_ADDR 0x33

void MLX90641_I2CInit(void) {
    // The bit-banged bus itself is already brought up by
    // MX_ExpansionI2C_Init() at board init, before any tool-specific
    // code (including this) ever runs - nothing sensor-specific needed
    // here beyond that, same as this board's own ADS1115 driver assumes.
}

int MLX90641_I2CGeneralReset(void) {
    // Standard I2C General Call Reset: address 0x00, data byte 0x06 -
    // generic I2C bus protocol, not MLX90641-specific.
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte(0x00);
    if (ack) ack = ExpansionI2C_WriteByte(0x06);
    ExpansionI2C_Stop();
    return ack ? 0 : -1;
}

int MLX90641_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
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

int MLX90641_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte((slaveAddr << 1) | 0);
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(writeAddress >> 8));
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(writeAddress & 0xFF));
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(data >> 8));
    if (ack) ack = ExpansionI2C_WriteByte((uint8_t)(data & 0xFF));
    ExpansionI2C_Stop();
    if (!ack) return -1;

    // Read back and verify, same as Melexis's own reference drivers do -
    // more valuable here than it would be on a hardware I2C peripheral,
    // since a bit-banged bus has more ways for a single bit to go wrong.
    uint16_t readback;
    if (MLX90641_I2CRead(slaveAddr, writeAddress, 1, &readback) != 0) return -1;
    if (readback != data) return -2;
    return 0;
}

void MLX90641_I2CFreqSet(int freq) {
    (void)freq; // no-op - this board's own bit-banged bus speed is fixed by ExpansionI2C_Delay()'s own timing, not runtime-adjustable through this driver layer, same reasoning as this board's own ADS1115 driver and the slave chip's own MLX90640 driver.
}
