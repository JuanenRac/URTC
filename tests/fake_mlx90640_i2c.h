// =============================================================================
// URTC Firmware - Host test double for the MLX9064x I2C driver: header
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
// Backs the five MLX90640_I2C* symbols MLX90640_API.c expects (declared in
// MLX90640_I2C_Driver.h) with an in-memory 16-bit word map plus a few knobs
// a test can use to reproduce a marginal/stuck bus without any hardware.
#ifndef FAKE_MLX90640_I2C_H
#define FAKE_MLX90640_I2C_H

#include <stdint.h>

struct fake_i2c_state {
    unsigned read_count;      // MLX90640_I2CRead calls since the last reset
    unsigned write_count;     // MLX90640_I2CWrite calls since the last reset
    unsigned status_poll_count; // status-register reads since the last reset
    int fail_read_at;         // -1 = never; else every read after this count returns -MLX90640_I2C_NACK_ERROR
    int status_ready_after;   // -1 = the data-ready bit is NEVER set (stuck bus);
                              //  N = it becomes set on and after the Nth status read
};

extern struct fake_i2c_state fake_i2c;

void fake_i2c_reset(void);
void fake_i2c_set_word(uint16_t addr, uint16_t value);
void fake_i2c_fill(uint16_t addr, uint16_t count, uint16_t value);

#endif // FAKE_MLX90640_I2C_H
