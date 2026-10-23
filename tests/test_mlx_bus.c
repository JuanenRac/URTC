// =============================================================================
// URTC Firmware - Host logic tests for the thermal-camera I2C transports and the MLX90642 driver and application
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The three transports (firmware_mlx90640_transport.c, firmware_mlx90641_transport.c, firmware_mlx90642_transport.c) put the Melexis drivers on the
// bit-banged I2C of the expansion connector. Here they run against the device model of tests/host_hal/fake_i2c_bus.c: a 16-bit word-addressed memory for
// the MLX90640 and MLX90641 and the opcode protocol of the MLX90642, which also lets the real MLX90642.c and the application of the head
// (firmware_mlx90642_app.c) run end to end: configuration, a measurement started and polled, the frame read back, the chunks the CAN side sends.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_expansion_i2c.h"
#include "fake_i2c_bus.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90641_I2C_Driver.h"
#include "MLX90642.h"
#include "test_runner.h"

int MLX90642_GetRefreshTime(uint8_t slaveAddr);
void MLX90642_Direct_Init(void);
void MLX90642_Direct_TriggerCapture(void);
uint8_t MLX90642_Direct_GetCaptureStatus(void);
void MLX90642_Direct_GetRawChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX90642_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);

static FakeI2cDevice *device;

static void world(int kind, uint8_t addr7)
{
    fake_hal_reset_recording();
    fake_i2c_bus_reset();
    fake_tick = 1000;
    fake_tick_step = 0;
    MX_ExpansionI2C_Init();
    device = kind >= 0 ? fake_i2c_bus_add(addr7, kind) : 0;
    fake_i2c_bus_clear_counters();
}

static void test_mlx90640_transport(int *failures)
{
    world(FAKE_I2C_MLX, 0x33);
    for (int i = 0; i < 5; i++) device->words16[0x2400 + i] = (uint16_t)(0xA000 + i * 0x111);
    uint16_t data[5] = {0};
    TEST_ASSERT(MLX90640_I2CRead(0x33, 0x2400, 3, data) == 0, "a read of three words is acknowledged");
    TEST_ASSERT(data[0] == 0xA000 && data[1] == 0xA111 && data[2] == 0xA222 && data[3] == 0, "the words arrive high byte first, in order, and no more than asked");
    TEST_ASSERT(fake_i2c_bus.seen[0] == 0x66 && fake_i2c_bus.seen[1] == 0x24 && fake_i2c_bus.seen[2] == 0x00 && fake_i2c_bus.seen[3] == 0x67, "on the wire: the address with the write bit, the memory address, then the address with the read bit");
    TEST_ASSERT(fake_i2c_bus.starts == 2 && fake_i2c_bus.stops == 1, "a repeated START, and one STOP at the end");
    TEST_ASSERT(MLX90640_I2CRead(0x33, 0x2403, 1, data) == 0 && data[0] == 0xA333, "a single word, answered with a NACK, still arrives");
    TEST_ASSERT(MLX90640_I2CRead(0x33, 0x2400, 0, data) == 0, "reading no words is not an error");
    TEST_ASSERT(MLX90640_I2CWrite(0x33, 0x8000, 0x1234) == 0 && device->words16[0x8000] == 0x1234, "a write puts the word at the address");
    TEST_ASSERT(MLX90640_I2CGeneralReset() == -1, "the general call is not answered when nothing listens to it: -1");
    fake_i2c_bus_add(0x00, FAKE_I2C_MEM8);
    fake_i2c_bus_clear_counters();
    TEST_ASSERT(MLX90640_I2CGeneralReset() == 0 && fake_i2c_bus.seen[0] == 0x00 && fake_i2c_bus.seen[1] == 0x06, "the general reset is the byte 0x06 sent to address 0 and it reports success when acknowledged");
    MLX90640_I2CInit();
    MLX90640_I2CFreqSet(400);

    world(-1, 0);
    TEST_ASSERT(MLX90640_I2CRead(0x33, 0x2400, 2, data) == -1 && MLX90640_I2CWrite(0x33, 0x8000, 1) == -1, "with no sensor a read and a write both fail with -1");
    TEST_ASSERT(fake_i2c_bus.stops >= 2, "and each is closed with a STOP");
    world(FAKE_I2C_MLX, 0x30);
    TEST_ASSERT(MLX90640_I2CRead(0x33, 0x2400, 1, data) == -1, "a sensor at another address is not the one asked for");
}

static void test_mlx90641_transport(int *failures)
{
    world(FAKE_I2C_MLX, 0x33);
    device->words16[0x0400] = 0xBEEF; device->words16[0x0401] = 0xCAFE;
    uint16_t data[2] = {0};
    TEST_ASSERT(MLX90641_I2CRead(0x33, 0x0400, 2, data) == 0 && data[0] == 0xBEEF && data[1] == 0xCAFE, "the MLX90641 transport reads words the same way");
    TEST_ASSERT(MLX90641_I2CWrite(0x33, 0x8000, 0x0101) == 0, "a write is checked by reading it back");
    device->read_only_addr = 0x8010;
    TEST_ASSERT(MLX90641_I2CWrite(0x33, 0x8010, 0x4321) == -2, "a word that does not keep its value is reported as -2");
    TEST_ASSERT(MLX90641_I2CGeneralReset() == -1, "the general reset is -1 when nobody answers it");
    fake_i2c_bus_add(0x00, FAKE_I2C_MEM8);
    TEST_ASSERT(MLX90641_I2CGeneralReset() == 0, "and 0 when it is answered");
    MLX90641_I2CInit();
    MLX90641_I2CFreqSet(400);
    world(-1, 0);
    TEST_ASSERT(MLX90641_I2CWrite(0x33, 0x8000, 1) == -1 && MLX90641_I2CRead(0x33, 0x0400, 1, data) == -1, "with no sensor both fail with -1");
}

static void test_mlx90642_transport(int *failures)
{
    world(FAKE_I2C_MLX642, 0x66);
    device->words16[0x1230] = 0x1111; device->words16[0x1232] = 0x2222;
    uint16_t data[2] = {0};
    TEST_ASSERT(MLX90642_I2CRead(0x66, 0x1230, 2, data) == 0 && data[0] == 0x1111 && data[1] == 0x2222, "the MLX90642 transport reads words from a 16-bit address");
    const uint8_t command[4] = {0x01, 0x80, 0x00, 0x01};
    TEST_ASSERT(MLX90642_I2CWrite(0x66, command, 4) == 0 && device->cmd_writes == 1 && device->last_cmd == 0x0001, "a command is written as raw bytes, with no address of its own");
    uint32_t before = fake_tick;
    MLX90642_Wait_ms(15);
    TEST_ASSERT(fake_tick - before == 15, "the wait is the HAL delay");
    world(-1, 0);
    TEST_ASSERT(MLX90642_I2CRead(0x66, 0x1230, 1, data) == -1 && MLX90642_I2CWrite(0x66, command, 4) == -1, "with no sensor both fail with -1");
    TEST_ASSERT(MLX90642_I2CWrite(0x66, command, 0) == -1, "a write of no bytes is still an address phase that nobody answers");
}

static void test_mlx90642_driver(int *failures)
{
    world(FAKE_I2C_MLX642, SA_90642_DEFAULT);
    uint16_t *w = device->words16;
    uint16_t id[4];
    w[0x1230] = 1; w[0x1232] = 2; w[0x1234] = 3; w[0x1236] = 4;
    TEST_ASSERT(MLX90642_GetID(SA_90642_DEFAULT, id) == 0 && id[0] == 1 && id[3] == 4, "the identification is four words");
    uint8_t major, minor, patch;
    TEST_ASSERT(MLX90642_GetFWver(SA_90642_DEFAULT, &major, &minor, &patch) == 0, "the firmware version is readable");

    w[MLX90642_APPLICATION_CONFIG_ADDRESS] = MLX90642_NORMALIZED_DATA_OUTPUT;
    TEST_ASSERT(MLX90642_GetMeasMode(SA_90642_DEFAULT) == MLX90642_CONT_MEAS_MODE, "the measurement mode is read from its bit");
    uint32_t before = fake_tick;
    TEST_ASSERT(MLX90642_SetMeasMode(SA_90642_DEFAULT, MLX90642_STEP_MEAS_MODE) == 0, "setting the step mode succeeds");
    TEST_ASSERT(w[MLX90642_APPLICATION_CONFIG_ADDRESS] == (MLX90642_NORMALIZED_DATA_OUTPUT | MLX90642_STEP_MEAS_MODE), "only the mode bit changed");
    TEST_ASSERT(fake_tick - before == MLX90642_EE_WRITE_TIME, "after the EEPROM write time (15 ms) the call returns");
    TEST_ASSERT(MLX90642_SetMeasMode(SA_90642_DEFAULT, 0x1234) == -MLX90642_INVAL_VAL_ERR, "a mode that is neither is refused");
    TEST_ASSERT(MLX90642_SetOutputFormat(SA_90642_DEFAULT, MLX90642_TEMPERATURE_OUTPUT) == 0 && MLX90642_GetOutputFormat(SA_90642_DEFAULT) == MLX90642_TEMPERATURE_OUTPUT, "the output format goes back to temperatures");
    TEST_ASSERT(MLX90642_SetOutputFormat(SA_90642_DEFAULT, 7) == -MLX90642_INVAL_VAL_ERR, "an unknown output format is refused");

    TEST_ASSERT(MLX90642_GetRefreshRate(SA_90642_DEFAULT) == MLX90642_REF_RATE_2HZ, "a refresh rate of 0 in the register is reported as the slowest (2 Hz)");
    TEST_ASSERT(MLX90642_SetRefreshRate(SA_90642_DEFAULT, MLX90642_REF_RATE_8HZ) == 0 && MLX90642_GetRefreshRate(SA_90642_DEFAULT) == MLX90642_REF_RATE_8HZ, "a refresh rate is set and read back");
    TEST_ASSERT(MLX90642_GetRefreshTime(SA_90642_DEFAULT) == (MLX90642_REF_TIME >> MLX90642_REF_RATE_8HZ), "the refresh time is 2000 ms shifted by the rate");
    TEST_ASSERT(MLX90642_SetRefreshRate(SA_90642_DEFAULT, 1) == -MLX90642_INVAL_VAL_ERR && MLX90642_SetRefreshRate(SA_90642_DEFAULT, 7) == -MLX90642_INVAL_VAL_ERR, "rates below 2 Hz and above 32 Hz are refused");

    int16_t emissivity = 0;
    TEST_ASSERT(MLX90642_SetEmissivity(SA_90642_DEFAULT, 0x3000) == 0 && w[MLX90642_EMISSIVITY_ADDRESS] == 0x3000, "the emissivity is written");
    TEST_ASSERT(MLX90642_GetEmissivity(SA_90642_DEFAULT, &emissivity) == 0, "and read");
    TEST_ASSERT(MLX90642_SetTreflected(SA_90642_DEFAULT, 2500) == 0, "the reflected temperature is written");

    TEST_ASSERT(MLX90642_SetI2CMode(SA_90642_DEFAULT, MLX90642_I2C_MODE_FM_PLUS) == 0 && MLX90642_GetI2CMode(SA_90642_DEFAULT) == MLX90642_I2C_MODE_FM_PLUS, "the I2C mode");
    TEST_ASSERT(MLX90642_SetI2CMode(SA_90642_DEFAULT, 5) == -MLX90642_INVAL_VAL_ERR, "an unknown I2C mode is refused");
    TEST_ASSERT(MLX90642_SetSDALimitState(SA_90642_DEFAULT, MLX90642_I2C_SDA_CUR_LIMIT_ON) == 0 && MLX90642_GetSDALimitState(SA_90642_DEFAULT) == MLX90642_I2C_SDA_CUR_LIMIT_ON, "the SDA current limit");
    TEST_ASSERT(MLX90642_SetSDALimitState(SA_90642_DEFAULT, 9) == -MLX90642_INVAL_VAL_ERR, "an unknown limit state is refused");
    TEST_ASSERT(MLX90642_SetI2CLevel(SA_90642_DEFAULT, MLX90642_I2C_LEVEL_1P8) == 0 && MLX90642_GetI2CLevel(SA_90642_DEFAULT) == MLX90642_I2C_LEVEL_1P8, "the I2C level");
    TEST_ASSERT(MLX90642_SetI2CLevel(SA_90642_DEFAULT, 9) == -MLX90642_INVAL_VAL_ERR, "an unknown level is refused");
    TEST_ASSERT(MLX90642_SetI2CSlaveAddress(SA_90642_DEFAULT, 0x70) == 0 && w[MLX90642_I2C_SA_ADDRESS] == 0x70, "a new slave address is written");
    TEST_ASSERT(MLX90642_SetI2CSlaveAddress(SA_90642_DEFAULT, 0) == -MLX90642_INVAL_VAL_ERR && MLX90642_SetI2CSlaveAddress(SA_90642_DEFAULT, 0x80) == -MLX90642_INVAL_VAL_ERR, "an address outside 1 to 127 is refused");

    w[MLX90642_PROGRESS_DATA_ADDRESS] = 77;
    TEST_ASSERT(MLX90642_GetProgress(SA_90642_DEFAULT) == 77, "the progress word");
    w[MLX90642_FLAGS_ADDRESS] = MLX90642_FLAGS_BUSY_MASK;
    TEST_ASSERT(MLX90642_IsDeviceBusy(SA_90642_DEFAULT) == 1 && MLX90642_IsDataReady(SA_90642_DEFAULT) == 0 && MLX90642_IsReadWindowOpen(SA_90642_DEFAULT) == 0, "busy and not ready: the read window is closed");
    w[MLX90642_FLAGS_ADDRESS] = MLX90642_FLAGS_READY_MASK | MLX90642_FLAGS_BUSY_MASK;
    TEST_ASSERT(MLX90642_IsReadWindowOpen(SA_90642_DEFAULT) == MLX90642_NO, "ready but still busy: closed");
    w[MLX90642_FLAGS_ADDRESS] = MLX90642_FLAGS_READY_MASK;
    TEST_ASSERT(MLX90642_IsReadWindowOpen(SA_90642_DEFAULT) == MLX90642_YES, "ready and not busy: open");
    TEST_ASSERT(MLX90642_ClearDataReady(SA_90642_DEFAULT) == 0 && (w[MLX90642_FLAGS_ADDRESS] & MLX90642_FLAGS_READY_MASK) == 0, "reading the temperature block clears the ready flag");

    TEST_ASSERT(MLX90642_GotoSleep(SA_90642_DEFAULT) == 0 && device->last_cmd == MLX90642_SLEEP_CMD, "the sleep command");
    TEST_ASSERT(MLX90642_WakeUp(SA_90642_DEFAULT) == 0, "the wake-up byte is acknowledged");

    // a whole measurement: Init starts one, polls until the ready flag, and MeasureNow returns the image
    for (int i = 0; i < 768; i++) w[MLX90642_TO_DATA_ADDRESS + 2 * i] = (uint16_t)(i * 3);
    int16_t image[768];
    TEST_ASSERT(MLX90642_Init(SA_90642_DEFAULT) == 0, "Init starts a measurement and waits for it");
    TEST_ASSERT(MLX90642_MeasureNow(SA_90642_DEFAULT, image) == 0 && image[0] == 0 && image[100] == 300 && image[767] == 2301, "MeasureNow returns the 768 temperature words");
    TEST_ASSERT(MLX90642_GetImage(SA_90642_DEFAULT, image) == 0 && image[5] == 15, "GetImage reads the block without a trigger");
    device->ignore_sync = 1;
    before = fake_tick;
    TEST_ASSERT(MLX90642_Init(SA_90642_DEFAULT) == -MLX90642_TIMEOUT_ERR, "a sensor that never says ready ends in a time-out, not a hang");
    TEST_ASSERT(fake_tick - before >= (uint32_t)(MLX90642_MAX_POLL_TRIES * MLX90642_POLL_TIME_MS), "after at least 100 polls of 2 ms");
    TEST_ASSERT(MLX90642_MeasureNow(SA_90642_DEFAULT, image) == -MLX90642_TIMEOUT_ERR, "MeasureNow gives up the same way");

    world(-1, 0);
    TEST_ASSERT(MLX90642_GetMeasMode(SA_90642_DEFAULT) < 0 && MLX90642_SetMeasMode(SA_90642_DEFAULT, MLX90642_STEP_MEAS_MODE) < 0, "with no sensor the calls fail instead of reporting a mode");
    TEST_ASSERT(MLX90642_GetRefreshRate(SA_90642_DEFAULT) < 0 && MLX90642_GetRefreshTime(SA_90642_DEFAULT) < 0 && MLX90642_Init(SA_90642_DEFAULT) < 0, "refresh rate, time and Init fail too");
    TEST_ASSERT(MLX90642_IsDataReady(SA_90642_DEFAULT) < 0 && MLX90642_IsDeviceBusy(SA_90642_DEFAULT) < 0 && MLX90642_GetProgress(SA_90642_DEFAULT) < 0, "so do the status reads");
    TEST_ASSERT(MLX90642_GetI2CMode(SA_90642_DEFAULT) < 0 && MLX90642_GetSDALimitState(SA_90642_DEFAULT) < 0 && MLX90642_GetI2CLevel(SA_90642_DEFAULT) < 0, "and the I2C configuration reads");
    TEST_ASSERT(MLX90642_SetI2CMode(SA_90642_DEFAULT, MLX90642_I2C_MODE_FM) < 0 && MLX90642_SetSDALimitState(SA_90642_DEFAULT, MLX90642_I2C_SDA_CUR_LIMIT_OFF) < 0 && MLX90642_SetI2CLevel(SA_90642_DEFAULT, MLX90642_I2C_LEVEL_VDD) < 0, "and the writes");
    TEST_ASSERT(MLX90642_SetRefreshRate(SA_90642_DEFAULT, MLX90642_REF_RATE_4HZ) < 0 && MLX90642_SetOutputFormat(SA_90642_DEFAULT, MLX90642_TEMPERATURE_OUTPUT) < 0, "the rate and the format");
    TEST_ASSERT(MLX90642_ClearDataReady(SA_90642_DEFAULT) < 0 && MLX90642_IsReadWindowOpen(SA_90642_DEFAULT) < 0 && MLX90642_GetOutputFormat(SA_90642_DEFAULT) < 0, "the ready flag, the read window and the format");
}

static void test_mlx90642_application(int *failures)
{
    char label[128];
    world(FAKE_I2C_MLX642, SA_90642_DEFAULT);
    uint16_t *w = device->words16;
    w[MLX90642_APPLICATION_CONFIG_ADDRESS] = MLX90642_NORMALIZED_DATA_OUTPUT;
    MLX90642_Direct_Init();
    TEST_ASSERT(w[MLX90642_APPLICATION_CONFIG_ADDRESS] == MLX90642_STEP_MEAS_MODE && device->config_writes == 2, "the head starts in single-measurement mode with temperature output");
    TEST_ASSERT(MLX90642_Direct_GetCaptureStatus() == MLX_CAPTURE_IDLE, "nothing is captured before it is asked for");

    for (int i = 0; i < 768; i++) {
        w[MLX90642_IR_DATA_ADDRESS + 2 * i] = (uint16_t)(0x4000 + i);
        w[MLX90642_TO_DATA_ADDRESS + 2 * i] = (uint16_t)(int16_t)(i * 50 / 2 - 100);   // 0.01-degree style units as the sensor reports them (half of 0.02)
    }
    w[MLX90642_TO_DATA_ADDRESS + 2 * 768] = 1234;
    for (int i = 0; i < 20; i++) w[MLX90642_AUX_DATA_ADDRESS + 2 * i] = (uint16_t)(0x100 + i);
    MLX90642_Direct_TriggerCapture();
    TEST_ASSERT(device->last_cmd == MLX90642_START_SYNC_MEAS_CMD, "a capture starts a synchronised measurement");
    TEST_ASSERT(MLX90642_Direct_GetCaptureStatus() == MLX_CAPTURE_READY, "when the sensor says ready the frame is read and the capture is ready");
    TEST_ASSERT(MLX90642_Direct_GetCaptureStatus() == MLX_CAPTURE_READY, "and stays ready until the next capture");

    uint8_t chunk[32];
    MLX90642_Direct_GetRawChunk(0, chunk);
    TEST_ASSERT(chunk[0] == 0x40 && chunk[1] == 0x00 && chunk[2] == 0x40 && chunk[3] == 0x01, "raw chunk 0 is the first sixteen raw pixels, high byte first");
    MLX90642_Direct_GetRawChunk(47, chunk);
    TEST_ASSERT(chunk[30] == 0x42 && chunk[31] == 0xFF, "raw chunk 47 ends with pixel 767");
    MLX90642_Direct_GetRawChunk(48, chunk);
    int zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "a chunk beyond the last one is all zeros");
    MLX90642_Direct_GetCalibratedChunk(1, chunk);
    int16_t first = (int16_t)((chunk[0] << 8) | chunk[1]);
    int16_t second = (int16_t)((chunk[2] << 8) | chunk[3]);
    snprintf(label, sizeof label, "the calibrated value is rescaled to hundredths of a degree (got %d and %d)", first, second);
    TEST_ASSERT(first == (int16_t)((int32_t)(int16_t)(16 * 50 / 2 - 100) * MLX_TEMP_SCALE / 50) && second == (int16_t)((int32_t)(int16_t)(17 * 50 / 2 - 100) * MLX_TEMP_SCALE / 50), label);
    MLX90642_Direct_GetCalibratedChunk(48, chunk);
    zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "a calibrated chunk beyond the last is all zeros");
    w[MLX90642_TO_DATA_ADDRESS] = 30000; w[MLX90642_TO_DATA_ADDRESS + 2] = (uint16_t)(int16_t)-30000;
    MLX90642_Direct_TriggerCapture();
    MLX90642_Direct_GetCaptureStatus();
    MLX90642_Direct_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == 32767 && (int16_t)((chunk[2] << 8) | chunk[3]) == -32768, "values beyond the 16-bit range are clamped, not wrapped");

    device->ignore_sync = 1;
    MLX90642_Direct_TriggerCapture();
    TEST_ASSERT(MLX90642_Direct_GetCaptureStatus() == MLX_CAPTURE_BUSY && MLX90642_Direct_GetCaptureStatus() == MLX_CAPTURE_BUSY, "a sensor that has not finished leaves the capture busy, and asking again does not disturb it");
    device->present = 0;
    TEST_ASSERT(MLX90642_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a sensor that disappears during the measurement is an error");
    MLX90642_Direct_TriggerCapture();
    TEST_ASSERT(MLX90642_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a trigger to a missing sensor is an error too");

    world(-1, 0);
    MLX90642_Direct_Init();
    TEST_ASSERT(fake_i2c_bus.stops >= 1, "Init with no sensor stops after the first failed read and writes nothing");
}

void run_mlx_bus_tests(int *failures)
{
    test_mlx90640_transport(failures);
    test_mlx90641_transport(failures);
    test_mlx90642_transport(failures);
    test_mlx90642_driver(failures);
    test_mlx90642_application(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the thermal camera transports, the MLX90642 driver and its application\n");
    run_mlx_bus_tests(&failures);
    if (failures == 0) {
        printf("  OK   all thermal camera bus host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
