// =============================================================================
// URTC Firmware - Host logic tests for the bit-banged buses of the expansion connector
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// firmware_expansion_i2c.c, firmware_ads1115.c and firmware_expansion_spi.c move the lines of the expansion connector one pin at a time. Here
// the lines are given a second end: an I2C bus with the expansion slave (0x42) and an ADS1115 (0x48) on it that follows the START and STOP
// conditions, the nine clocks of a byte and the acknowledge like a real device, and an SPI slave that answers on the falling clock. The real
// sources are compiled unchanged against the fake HAL; what is asserted is what a device on the wire would see and what the caller gets back.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_expansion_i2c.h"
#include "firmware_expansion_spi.h"
#include "firmware_ads1115.h"
#include "fake_i2c_bus.h"
#include "test_runner.h"

// ---- the I2C bus: the model of tests/host_hal/fake_i2c_bus.c, with the expansion slave and an ADS1115 on it ----------------------------------------
static FakeI2cDevice *dev_expansion, *dev_ads;

static void i2c_world(int expansion_present, int ads_present)
{
    fake_hal_reset_recording();
    fake_i2c_bus_reset();
    dev_expansion = expansion_present ? fake_i2c_bus_add(0x42, FAKE_I2C_MEM8) : 0;
    dev_ads = ads_present ? fake_i2c_bus_add(0x48, FAKE_I2C_WORD16) : 0;
    MX_ExpansionI2C_Init();
    fake_i2c_bus_clear_counters();
}

static void test_i2c_init(int *failures)
{
    fake_hal_reset_recording();
    fake_i2c_bus_reset();
    MX_ExpansionI2C_Init();
    TEST_ASSERT(fake_gpio_init_count == 1 && fake_gpio_inits[0].port == GPIOB, "the lines are configured once, on port B");
    TEST_ASSERT(fake_gpio_inits[0].init.Pin == (GPIO_PIN_10 | GPIO_PIN_11), "both pins PB10 and PB11 are configured");
    TEST_ASSERT(fake_gpio_inits[0].init.Mode == GPIO_MODE_OUTPUT_OD, "open drain: a device can pull the line down without a short");
    TEST_ASSERT(fake_gpio_inits[0].init.Pull == GPIO_NOPULL, "no internal pull: the board has its own pull-ups");
    TEST_ASSERT(ExpansionI2C_HadTimeout() == 0 && fake_i2c_bus.starts == 0, "the bus is left released (no START, no timeout)");
}

static void test_register_write_and_read(int *failures)
{
    i2c_world(1, 0);
    const uint8_t out[3] = {0xDE, 0xAD, 0xBE};
    TEST_ASSERT(ExpansionI2C_SlaveWriteRegister(0x10, out, 3) == 1, "a register write to the expansion slave is acknowledged");
    TEST_ASSERT(fake_i2c_bus.seen[0] == 0x84 && fake_i2c_bus.seen[1] == 0x10 && fake_i2c_bus.seen[2] == 0xDE && fake_i2c_bus.seen[3] == 0xAD && fake_i2c_bus.seen[4] == 0xBE, "the wire carries the address with the write bit, the register and the data in order");
    TEST_ASSERT(dev_expansion->mem[0x10] == 0xDE && dev_expansion->mem[0x11] == 0xAD && dev_expansion->mem[0x12] == 0xBE, "the device stored the bytes from the register on");
    TEST_ASSERT(fake_i2c_bus.starts == 1 && fake_i2c_bus.stops == 1, "one START and one STOP frame the write");

    dev_expansion->mem[0x20] = 0x11; dev_expansion->mem[0x21] = 0x22; dev_expansion->mem[0x22] = 0x33;
    uint8_t in[3] = {0};
    int starts_before = fake_i2c_bus.starts;
    TEST_ASSERT(ExpansionI2C_SlaveReadRegister(0x20, in, 3) == 1, "a register read is acknowledged");
    TEST_ASSERT(in[0] == 0x11 && in[1] == 0x22 && in[2] == 0x33, "the three bytes come back in order");
    TEST_ASSERT(fake_i2c_bus.starts - starts_before == 2, "a read is a write of the register, a repeated START and the read");
    TEST_ASSERT(fake_i2c_bus.seen[5] == 0x84 && fake_i2c_bus.seen[6] == 0x20 && fake_i2c_bus.seen[7] == 0x85, "the read address carries the read bit after the register");

    dev_expansion->mem[0x31] = 0xAA; dev_expansion->mem[0x32] = 0xBB;
    uint8_t in2[2] = {0};
    TEST_ASSERT(ExpansionI2C_SlaveReadRegisterWithParam(0x30, 0x5A, in2, 2) == 1, "a read with a parameter is acknowledged");
    TEST_ASSERT(dev_expansion->mem[0x30] == 0x5A, "the parameter went to the device after the register");
    TEST_ASSERT(in2[0] == 0xAA && in2[1] == 0xBB, "and the data came back");

    in[0] = 0x77;
    TEST_ASSERT(ExpansionI2C_SlaveReadRegister(0x20, in, 1) == 1 && in[0] == 0x11, "a one-byte read answers with a NACK of the last byte and still returns it");
}

static void test_no_device_and_stuck_clock(int *failures)
{
    i2c_world(0, 0);
    uint8_t in[2] = {0xEE, 0xEE};
    const uint8_t out[1] = {1};
    TEST_ASSERT(ExpansionI2C_SlaveWriteRegister(0x01, out, 1) == 0, "with no device the write is not acknowledged");
    TEST_ASSERT(ExpansionI2C_SlaveReadRegister(0x01, in, 2) == 0 && in[0] == 0xEE, "the read fails at once and leaves the caller buffer alone");
    TEST_ASSERT(ExpansionI2C_SlaveReadRegisterWithParam(0x01, 2, in, 2) == 0 && in[1] == 0xEE, "the read with a parameter fails the same way");
    TEST_ASSERT(fake_i2c_bus.stops >= 3 , "every failed transfer still ends with a STOP");

    i2c_world(1, 0);
    fake_i2c_bus.stretch = 1;                                    // a device holds the clock low for ever
    ExpansionI2C_Start();
    TEST_ASSERT(ExpansionI2C_HadTimeout() == 1, "waiting for a clock that never rises is given up and reported");
    fake_i2c_bus.stretch = 0;
    ExpansionI2C_Start();
    TEST_ASSERT(ExpansionI2C_HadTimeout() == 0, "the flag is cleared by the next START");

    fake_i2c_bus.stretch = 1;
    uint8_t one[1] = {0};
    TEST_ASSERT(ExpansionI2C_SlaveWriteRegister(0x01, one, 1) == 0, "a write on a stuck clock is not reported as done");
    TEST_ASSERT(ExpansionI2C_HadTimeout() == 1, "and says why");
}

static void test_ads1115(int *failures)
{
    i2c_world(0, 1);
    ADS1115_Direct_Configure(0x0483);
    TEST_ASSERT(dev_ads->words[1] == 0x0483, "the configuration register holds what was written");
    TEST_ASSERT(fake_i2c_bus.seen[0] == 0x90 && fake_i2c_bus.seen[1] == 0x01 && fake_i2c_bus.seen[2] == 0x04 && fake_i2c_bus.seen[3] == 0x83, "address 0x48 write, the config register, the high byte, the low byte");

    ADS1115_Direct_TriggerConversion();
    TEST_ASSERT(dev_ads->words[1] == (0x0483 | 0x8000), "the trigger sets the start bit on the last configuration");
    ADS1115_Direct_Configure(0x4000);
    ADS1115_Direct_TriggerConversion();
    TEST_ASSERT(dev_ads->words[1] == 0xC000, "the start bit is added to whatever was configured last");

    dev_ads->words[0] = 0xFFF6;
    TEST_ASSERT(ADS1115_Direct_ReadResult() == -10, "the result is a signed 16-bit value, high byte first");
    dev_ads->words[0] = 0x1234;
    TEST_ASSERT(ADS1115_Direct_ReadResult() == 0x1234, "a positive result");
    TEST_ASSERT(fake_i2c_bus.seen[fake_i2c_bus.byte_count - 1] == 0x91, "the last byte the master sent was the read address of the converter");
    dev_ads->words[0] = 0x8000;
    TEST_ASSERT(ADS1115_Direct_ReadResult() == -32768, "the most negative value is not wrapped");

    i2c_world(0, 0);
    TEST_ASSERT(ADS1115_Direct_ReadResult() == 0, "with no converter the result is 0, not a stale value");
    TEST_ASSERT(fake_i2c_bus.stops == 1, "and the transfer is closed with a STOP");
}

// ---- the SPI bus -----------------------------------------------------------------------------------------------------------------------
static struct { uint8_t reply, rx; int bit, miso, mosi, sck, edges; uint8_t sent[8]; int sent_count; } spi;

static void spi_write_hook(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    int level = state == GPIO_PIN_SET;
    if (port == EXP_SPI_MOSI_PORT && pin == EXP_SPI_MOSI_PIN) { spi.mosi = level; return; }
    if (port == EXP_SPI_SCK_PORT && pin == EXP_SPI_SCK_PIN) {
        if (spi.sck && !level) {                   // falling: the slave puts its next bit on MISO
            spi.miso = (spi.reply >> (7 - spi.bit)) & 1;
        } else if (!spi.sck && level) {            // rising: the slave takes the bit on MOSI
            spi.rx = (uint8_t)((spi.rx << 1) | spi.mosi);
            spi.bit++;
            spi.edges++;
            if (spi.bit == 8) { if (spi.sent_count < 8) spi.sent[spi.sent_count++] = spi.rx; spi.bit = 0; }
        }
        spi.sck = level;
    }
}

static int spi_read_hook(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == EXP_SPI_MISO_PORT && pin == EXP_SPI_MISO_PIN) return spi.miso;
    return -1;
}

static void test_spi(int *failures)
{
    fake_hal_reset_recording();
    memset(&spi, 0, sizeof spi);
    MX_ExpansionSPI_Init();
    TEST_ASSERT(fake_gpio_write_count >= 2 && fake_gpio_writes[0].pin == EXP_SPI_CS_PIN && fake_gpio_writes[0].state == GPIO_PIN_SET, "the chip select is raised first, so no device is selected while the pins change");
    TEST_ASSERT(fake_gpio_writes[1].pin == EXP_SPI_SCK_PIN && fake_gpio_writes[1].state == GPIO_PIN_SET, "the clock idles high");
    TEST_ASSERT(fake_gpio_init_count == 5, "five pins are configured: chip select, clock, MOSI, MISO and the driver diagnostic line");
    int outputs = 0, inputs = 0;
    for (int i = 0; i < fake_gpio_init_count; i++) {
        if (fake_gpio_inits[i].init.Mode == GPIO_MODE_OUTPUT_PP) outputs++;
        if (fake_gpio_inits[i].init.Mode == GPIO_MODE_INPUT) inputs++;
    }
    TEST_ASSERT(outputs == 3 && inputs == 2, "three push-pull outputs and two inputs");
    TEST_ASSERT(fake_gpio_inits[3].init.Pin == EXP_SPI_MISO_PIN && fake_gpio_inits[3].init.Pull == GPIO_PULLUP, "MISO has its pull-up, so an absent device reads as ones");
    TEST_ASSERT(fake_gpio_inits[4].init.Pin == TMC_DIAG0_PIN && fake_gpio_inits[4].init.Pull == GPIO_NOPULL, "the diagnostic line of the driver has no pull");

    fake_gpio_write_hook = spi_write_hook;
    fake_gpio_read_hook = spi_read_hook;
    spi.sck = 1;                                   // the clock idles high after the init
    spi.reply = 0x3C;
    uint8_t got = ExpansionSPI_TransferByte(0xA5);
    TEST_ASSERT(got == 0x3C, "the byte the slave answered comes back, most significant bit first");
    TEST_ASSERT(spi.sent_count == 1 && spi.sent[0] == 0xA5, "the slave received the byte the master sent, most significant bit first");
    TEST_ASSERT(spi.edges == 8, "exactly eight clock pulses");

    spi.reply = 0x00;
    TEST_ASSERT(ExpansionSPI_TransferByte(0xFF) == 0x00 && spi.sent[1] == 0xFF, "all ones out, all zeros in");
    spi.reply = 0xFF;
    TEST_ASSERT(ExpansionSPI_TransferByte(0x00) == 0xFF && spi.sent[2] == 0x00, "all zeros out, all ones in");
    spi.reply = 0x01;
    TEST_ASSERT(ExpansionSPI_TransferByte(0x80) == 0x01 && spi.sent[3] == 0x80, "the single-bit patterns of both ends are not mirrored");
    fake_gpio_write_hook = 0;
    fake_gpio_read_hook = 0;
}

void run_expansion_bus_tests(int *failures)
{
    test_i2c_init(failures);
    test_register_write_and_read(failures);
    test_no_device_and_stuck_clock(failures);
    test_ads1115(failures);
    test_spi(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the I2C and SPI of the expansion connector, and the ADS1115\n");
    run_expansion_bus_tests(&failures);
    if (failures == 0) {
        printf("  OK   all expansion bus host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
