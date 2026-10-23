// =============================================================================
// URTC Firmware - a device-level model of the bit-banged I2C bus of the expansion connector, for host-side tests: fake_i2c_bus.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// firmware_expansion_i2c.c moves SCL and SDA one pin at a time through HAL_GPIO_WritePin / HAL_GPIO_ReadPin. This file is the other end of those two
// lines: it follows the START and STOP conditions, the clocks and the acknowledge like a device on the bus, and offers three kinds of device:
//   FAKE_I2C_MEM8   a byte-addressed register file (the expansion slave): the first byte after the address is the pointer, then bytes advance it;
//   FAKE_I2C_WORD16 a 16-bit register file (an ADS1115): pointer byte, then two bytes per register, high byte first;
//   FAKE_I2C_MLX    a 16-bit word-addressed memory (the MLX90640 and MLX90641): a two-byte address, then words, high byte first, the address advancing;
//   FAKE_I2C_MLX642 the MLX90642: the same memory but addressed in bytes (a word is two addresses), but it is written with a configuration opcode (0x3A2E, address, data) or a command opcode (0x0180,
//                   command); the start-synchronised-measurement command raises the data-ready flag (unless ignore_sync), reading the temperature
//                   block clears it.
// A test installs the model after fake_hal_reset_recording() (which removes the hooks), adds the devices it wants and then calls the firmware.
#ifndef HOST_FAKE_I2C_BUS_H
#define HOST_FAKE_I2C_BUS_H

#include <stdint.h>

enum { FAKE_I2C_MEM8 = 0, FAKE_I2C_WORD16 = 1, FAKE_I2C_MLX = 2, FAKE_I2C_MLX642 = 3 };

typedef struct {
    uint8_t addr;
    int kind, present;
    uint8_t mem[256];
    uint16_t words[4];
    uint16_t *words16;            // FAKE_I2C_MLX: the 65536-word memory
    // read-only for the tests
    uint8_t ptr, wr_hi, half, expect_ptr;
    uint32_t mptr;
    int wcount;
    int ignore_sync;              // FAKE_I2C_MLX642: the start command does not raise the data-ready flag
    int config_writes, cmd_writes;
    uint16_t last_cmd;
    int32_t read_only_addr;       // FAKE_I2C_MLX: a write to this word address is ignored (-1: none)
    int clear_trigger_on_reset;   // FAKE_I2C_MLX: the general call (a write to address 0 through the hardware I2C) clears the trigger bit of the control register
} FakeI2cDevice;

typedef struct {
    int stretch;                  // 1 = a device holds SCL low for ever
    int starts, stops, byte_count, ack_nacks;
    uint8_t seen[128];            // every byte the master sent, addresses included
} FakeI2cBusStats;

extern FakeI2cBusStats fake_i2c_bus;

void fake_i2c_bus_reset(void);                          // an idle bus with no device; installs the GPIO hooks
FakeI2cDevice *fake_i2c_bus_add(uint8_t addr7, int kind);
void fake_i2c_bus_hal_install(void);                    // the same devices answer HAL_I2C_Master_Transmit / Receive too (the hardware I2C of the expansion slave)
void fake_i2c_bus_clear_counters(void);                 // starts, stops, byte_count and seen (after the init of the pins, for instance)

#endif // HOST_FAKE_I2C_BUS_H
