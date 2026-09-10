// =============================================================================
// URTC Firmware - Host test double for the MLX9064x I2C driver
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include <string.h>
#include <stdint.h>

#include "MLX90640_API.h"
#include "fake_mlx90640_i2c.h"

// The MLX9064x address space is 16-bit word-addressed; a flat map is the
// simplest faithful model for a host test (128 KiB, host-only).
static uint16_t g_reg[65536];
struct fake_i2c_state fake_i2c;

void fake_i2c_reset(void)
{
    memset(g_reg, 0, sizeof(g_reg));
    memset(&fake_i2c, 0, sizeof(fake_i2c));
    fake_i2c.fail_read_at = -1;
    fake_i2c.status_ready_after = -1;
}

void fake_i2c_set_word(uint16_t addr, uint16_t value)
{
    g_reg[addr] = value;
}

void fake_i2c_fill(uint16_t addr, uint16_t count, uint16_t value)
{
    for (uint16_t i = 0; i < count; i++) {
        g_reg[(uint16_t)(addr + i)] = value;
    }
}

// ---- the five symbols MLX90640_I2C_Driver.h declares -------------------

void MLX90640_I2CInit(void) {}

void MLX90640_I2CFreqSet(int freq) { (void)freq; }

int MLX90640_I2CGeneralReset(void) { return MLX90640_NO_ERROR; }

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress,
                     uint16_t nMemAddressRead, uint16_t *data)
{
    (void)slaveAddr;
    fake_i2c.read_count++;
    if (fake_i2c.fail_read_at >= 0 &&
        fake_i2c.read_count > (unsigned)fake_i2c.fail_read_at) {
        return -MLX90640_I2C_NACK_ERROR;
    }

    // Status register: honour the data-ready knob so a test can model a
    // sensor that acks every transaction but never signals a frame.
    if (startAddress == MLX90640_STATUS_REG && nMemAddressRead == 1) {
        uint16_t s = g_reg[MLX90640_STATUS_REG];
        unsigned poll = fake_i2c.status_poll_count++;
        if (fake_i2c.status_ready_after >= 0 &&
            poll >= (unsigned)fake_i2c.status_ready_after) {
            s |= MLX90640_STAT_DATA_READY_MASK; // bit 3
        }
        data[0] = s;
        return MLX90640_NO_ERROR;
    }

    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        data[i] = g_reg[(uint16_t)(startAddress + i)];
    }
    return MLX90640_NO_ERROR;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data)
{
    (void)slaveAddr;
    fake_i2c.write_count++;
    g_reg[writeAddress] = data;
    return MLX90640_NO_ERROR;
}
