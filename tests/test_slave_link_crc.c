// =============================================================================
// URTC Firmware - Host logic tests for the check byte of the application link (urtc_link_crc.h and slave_i2c_link.c built with URTC_SLAVE_LINK_CRC=1)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The slave link is compiled here with the check byte on. The HAL "interrupts" are played by the test exactly as tests/test_slave.c does: the address match,
// the end of a receive. What is asserted is what a master on the wire would see: a write with a good check byte is executed, a write with a wrong or a
// missing one changes nothing (not even which register a following read returns) and is counted, and every answer ends with a check byte that covers the
// register and the data. The sensors are the device models of tests/host_hal/fake_i2c_bus.c; the MLX90641 library (C++) is replaced by recorders.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slave_common.h"
#include "slave_i2c_link.h"
#include "slave_i2c_sensors.h"
#include "slave_pwm.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90641_I2C_Driver.h"
#include "MLX90642_depends.h"
#include "fake_i2c_bus.h"
#include "urtc_link_crc.h"
#include "test_runner.h"

int slave_main_entry(void);
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode);
void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c);

// ---- the MLX90641 library, replaced by recorders (nothing here uses the camera) ------------------------------------------------------------------
int MLX90641_DumpEE(uint8_t slaveAddr, uint16_t *eeData) { (void)slaveAddr; (void)eeData; return 0; }
int MLX90641_ExtractParameters(uint16_t *eeData, paramsMLX90641 *p) { (void)eeData; (void)p; return 0; }
int MLX90641_TriggerMeasurement(uint8_t slaveAddr) { (void)slaveAddr; return 0; }
int MLX90641_GetFrameData(uint8_t slaveAddr, uint16_t *frame) { (void)slaveAddr; (void)frame; return 0; }
float MLX90641_GetTa(uint16_t *frame, const paramsMLX90641 *p) { (void)frame; (void)p; return 30.0f; }
void MLX90641_CalculateTo(uint16_t *frame, const paramsMLX90641 *p, float e, float tr, float *result)
{
    (void)frame; (void)p; (void)e; (void)tr; (void)result;
}

static void world(void)
{
    fake_hal_reset_recording();
    fake_i2c_bus_reset();
    fake_i2c_bus_hal_install();
    fake_tick = 10000;
    fake_tick_step = 0;
    mlx_sensor_variant = MLX_VARIANT_NONE;
    memset(&hi2c1, 0, sizeof hi2c1);
    memset(&htim1, 0, sizeof htim1);
}

// the master writes `count` bytes (the caller adds the check byte, or not)
static void master_writes(const uint8_t *bytes, uint32_t count)
{
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, I2C_SLAVE_ADDRESS << 1);
    for (uint32_t i = 0; i < count && i < fake_i2c_slave_rx_len; i++) fake_i2c_slave_rx_buffer[i] = bytes[i];
    hi2c1.XferCount = fake_i2c_slave_rx_len - count;
    HAL_I2C_SlaveRxCpltCallback(&hi2c1);
}

// the master writes register + payload with the right check byte
static void master_writes_checked(const uint8_t *bytes, uint8_t count)
{
    uint8_t frame[URTC_LINK_CRC_MAX_FRAME];
    memcpy(frame, bytes, count);
    master_writes(frame, urtc_link_crc_seal(frame, count));
}

static void test_the_check_function(int *failures)
{
    const uint8_t text[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT(urtc_link_crc8(0, text, 9) == 0xF4, "the CRC-8 (polynomial 0x07, initial value 0) of the text 123456789 is its published check value 0xF4");
    TEST_ASSERT(urtc_link_crc8(0, text, 0) == 0, "nothing to check leaves the initial value");
    uint8_t frame[URTC_LINK_CRC_MAX_FRAME] = {REG_PWM_PULSE, 1, 25, 7};
    uint8_t total = urtc_link_crc_seal(frame, 4);
    TEST_ASSERT(total == 5 && frame[4] == urtc_link_crc8(0, frame, 4), "sealing a frame appends the check byte and the new length is one more");
    TEST_ASSERT(urtc_link_crc_open(frame, total) == 4, "a sealed frame opens and gives back the length without the check byte");
    for (uint8_t i = 0; i < total; i++) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            frame[i] ^= (uint8_t)(1u << bit);
            if (urtc_link_crc_open(frame, total) != 0) { TEST_ASSERT(0, "a single flipped bit anywhere in the frame is caught"); }
            frame[i] ^= (uint8_t)(1u << bit);
        }
    }
    TEST_ASSERT(urtc_link_crc_open(frame, 1) == 0 && urtc_link_crc_open(frame, 0) == 0, "a frame too short to hold a check byte does not open");
    const uint8_t data[2] = {0xAB, 0xCD};
    TEST_ASSERT(urtc_link_crc_answer(REG_ADS_READ, data, 2) != urtc_link_crc_answer(REG_APP_STATUS, data, 2), "the check of an answer depends on the register that was selected");
}

static void test_checked_writes_and_reads(int *failures)
{
    world();
    SystemCoreClock = 72000000u;
    PWM_Init();
    I2CLink_Init();
    TEST_ASSERT(i2c_link_crc_errors == 0, "no check has failed yet");
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_rx_len == 9, "a write by the master may be eight bytes and the check byte");

    // a right write is executed
    const uint8_t pulse[4] = {REG_PWM_PULSE, 1, 25, 7};
    master_writes_checked(pulse, 4);
    TEST_ASSERT(fake_tim_compare[1] == 250 && i2c_link_crc_errors == 0, "a pulse with its check byte starts the pulse and counts no error");

    // a wrong or missing check byte changes nothing (a compare register that was never written still holds the all-ones the fake HAL starts it with)
    int starts = fake_tim_pwm_starts;
    uint8_t bad[5] = {REG_PWM_PULSE, 2, 50, 7, 0};
    bad[4] = (uint8_t)(urtc_link_crc8(0, bad, 4) ^ 0x01u);
    master_writes(bad, 5);
    TEST_ASSERT(fake_tim_pwm_starts == starts && fake_tim_compare[2] == 0xFFFFFFFFu && i2c_link_crc_errors == 1, "a pulse whose check byte is wrong is not executed and is counted");
    master_writes(pulse, 4);
    TEST_ASSERT(i2c_link_crc_errors == 2, "the same four bytes without a check byte are not a frame either");
    uint8_t corrupted[5] = {REG_PWM_PULSE, 2, 50, 7, 0};
    corrupted[4] = urtc_link_crc8(0, corrupted, 4);
    corrupted[1] = 3;                                   // a bit of the payload changed after the check byte was made
    master_writes(corrupted, 5);
    TEST_ASSERT(fake_tim_compare[3] == 0xFFFFFFFFu && i2c_link_crc_errors == 3, "a payload altered on the wire is caught");

    // the dangerous command is never executed from a frame that fails its check
    int resets = fake_reset_count;
    uint8_t magic[6] = {REG_ENTER_BOOTLOADER, 0xB0, 0x07, 0x1D, 0x5A, 0};
    magic[5] = (uint8_t)(urtc_link_crc8(0, magic, 5) ^ 0x80u);
    master_writes(magic, 6);
    TEST_ASSERT(fake_reset_count == resets && i2c_link_crc_errors == 4, "the bootloader entry with a wrong check byte does not restart the slave");
    const uint8_t good_magic[5] = {REG_ENTER_BOOTLOADER, 0xB0, 0x07, 0x1D, 0x5A};
    master_writes_checked(good_magic, 5);
    TEST_ASSERT(fake_reset_count == resets + 1, "and with the right one it does");

    // the selection of a register is itself checked, and the answer ends with a check byte over the register and the data
    world();
    I2CLink_Init();
    const uint8_t select_version[1] = {REG_APP_VERSION};
    master_writes_checked(select_version, 1);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 11, "the version answer is ten bytes and the check byte");
    TEST_ASSERT(fake_i2c_slave_tx[10] == urtc_link_crc_answer(REG_APP_VERSION, fake_i2c_slave_tx, 10), "the check byte covers the register that was selected and the ten bytes");
    TEST_ASSERT(fake_i2c_slave_tx[0] == 0 && fake_i2c_slave_tx[9] == FIRMWARE_VERSION_PATCH, "the data are the same as without the check byte");

    // a selection with a wrong check byte leaves the previous selection alone
    uint8_t bad_select[2] = {REG_ADS_READ, 0};
    bad_select[1] = (uint8_t)(urtc_link_crc8(0, bad_select, 1) ^ 0x10u);
    uint32_t errors = i2c_link_crc_errors;
    master_writes(bad_select, 2);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, 0x84);
    TEST_ASSERT(i2c_link_crc_errors == errors + 1 && fake_i2c_slave_tx_len == 11, "a selection that fails its check does not change what a following read returns");

    // the status register: one byte and its check byte
    world();
    I2CLink_Init();
    const uint8_t select_status[1] = {REG_APP_STATUS};
    master_writes_checked(select_status, 1);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 2 && fake_i2c_slave_tx[0] == APP_STATUS_IDLE && fake_i2c_slave_tx[1] == urtc_link_crc_answer(REG_APP_STATUS, fake_i2c_slave_tx, 1), "the status answer is the idle status and its check byte");

    // a thermal chunk is 32 bytes and the check byte makes 33: the longest frame
    mlx_sensor_variant = MLX_VARIANT_NONE;
    const uint8_t chunk_select[2] = {REG_MLX_RAW_CHUNK, 5};
    master_writes_checked(chunk_select, 2);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 33 && fake_i2c_slave_tx[32] == urtc_link_crc_answer(REG_MLX_RAW_CHUNK, fake_i2c_slave_tx, 32), "a chunk of the thermal image is 32 bytes and a check byte");
    TEST_ASSERT(33u == URTC_LINK_CRC_MAX_FRAME, "and 33 is the longest frame of the protocol");
}

static void run_slave_link_crc_tests(int *failures)
{
    test_the_check_function(failures);
    test_checked_writes_and_reads(failures);
}

#undef main      // the suite renames the main() of slave_main.c (-Dmain=slave_main_entry); the main of this test is the real one
int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the check byte of the application link\n");
    run_slave_link_crc_tests(&failures);
    if (failures == 0) {
        printf("  OK   all link check byte host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
