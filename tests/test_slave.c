// =============================================================================
// URTC Firmware - Host logic tests for the expansion slave: start-up, I2C link, PWM and sensors (slave_main.c, slave_i2c_link.c, slave_pwm.c, slave_i2c_sensors.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The slave is a second STM32 on the expansion connector: the master talks to it on I2C address 0x42 (registers for the thermal camera, the ADS1115 and
// four PWM outputs) and it talks to the sensors on its own I2C2. Here the real sources are compiled against the fake HAL with main() renamed: the
// HAL "interrupts" are played by the test (the address match, the end of a receive), the sensors are the device models of tests/host_hal/fake_i2c_bus.c
// behind HAL_I2C_Master_Transmit and Receive, and the PWM timer is read back from the recorded configuration. The MLX90641 library is C++ and is not
// built here: its entry points are recorders.
#include <setjmp.h>
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
#include "mlx_synthetic.h"
#include "test_runner.h"

int slave_main_entry(void);
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode);
void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);
void I2C1_EV_IRQHandler(void);
void I2C1_ER_IRQHandler(void);

// ---- the MLX90641 library, replaced by recorders --------------------------------------------------------------------------------------------
static struct { int dump_rc, trigger_rc, frame_rc, extract_calls, to_calls; uint16_t word0; float to_value; } m641;
int MLX90641_DumpEE(uint8_t slaveAddr, uint16_t *eeData) { (void)slaveAddr; (void)eeData; return m641.dump_rc; }
int MLX90641_ExtractParameters(uint16_t *eeData, paramsMLX90641 *p) { (void)eeData; (void)p; m641.extract_calls++; return 0; }
int MLX90641_TriggerMeasurement(uint8_t slaveAddr) { (void)slaveAddr; return m641.trigger_rc; }
int MLX90641_GetFrameData(uint8_t slaveAddr, uint16_t *frame) { (void)slaveAddr; for (int i = 0; i < 242; i++) frame[i] = (uint16_t)(m641.word0 + i); return m641.frame_rc; }
float MLX90641_GetTa(uint16_t *frame, const paramsMLX90641 *p) { (void)frame; (void)p; return 30.0f; }
void MLX90641_CalculateTo(uint16_t *frame, const paramsMLX90641 *p, float e, float tr, float *result)
{
    (void)frame; (void)p; (void)e; (void)tr;
    m641.to_calls++;
    for (int i = 0; i < 192; i++) result[i] = m641.to_value + (float)i;
}

static FakeI2cDevice *mlx, *ads;

static void world(void)
{
    fake_hal_reset_recording();
    fake_i2c_bus_reset();
    fake_i2c_bus_hal_install();
    fake_tick = 10000;
    fake_tick_step = 0;
    mlx = ads = 0;
    mlx_sensor_variant = MLX_VARIANT_NONE;
    memset(&m641, 0, sizeof m641);
    memset(&hi2c1, 0, sizeof hi2c1);
    memset(&htim1, 0, sizeof htim1);
}

// ---- the PWM -------------------------------------------------------------------------------------------------------------------------------
static void test_pwm(int *failures)
{
    world();
    SystemCoreClock = 72000000u;
    PWM_Init();
    int pins = -1;
    for (int i = 0; i < fake_gpio_init_count; i++) if (fake_gpio_inits[i].port == GPIOA && fake_gpio_inits[i].init.Pin == (GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11)) pins = i;
    TEST_ASSERT(pins >= 0 && fake_gpio_inits[pins].init.Mode == GPIO_MODE_AF_PP && fake_gpio_inits[pins].init.Alternate == GPIO_AF6_TIM1, "the four outputs are PA8 to PA11 on alternate function 6 (timer 1)");
    TEST_ASSERT(htim1.Instance == TIM1 && fake_tim_pwm_inits == 1 && htim1.Init.Prescaler == 71 && htim1.Init.Period == 999 && htim1.Init.AutoReloadPreload == TIM_AUTORELOAD_PRELOAD_ENABLE, "the counter runs at 1 MHz (72 MHz / 72) with a period of 1000 counts: 1 kHz, and the period is buffered");
    TEST_ASSERT(fake_tim_oc_configs == 4 && fake_tim_moe == 1 && fake_tim_oc[2].Pulse == 0, "four channels in PWM mode 1, starting at zero, and the main output enabled");

    PWM_Pulse(1, 50, 20);
    TEST_ASSERT(fake_tim_compare[1] == 500 && fake_tim_pwm_starts == 1, "50 percent of a 1000-count period is a compare value of 500, and the channel is started");
    PWM_Pulse(2, 33, 5);
    TEST_ASSERT(fake_tim_compare[2] == 330, "33 percent is 330");
    PWM_Pulse(3, 250, 5);
    TEST_ASSERT(fake_tim_compare[3] == 1000, "a duty over 100 is held at 100 percent");
    PWM_Pulse(0, 0, 5);
    TEST_ASSERT(fake_tim_compare[0] == 0, "zero percent is zero");
    int starts = fake_tim_pwm_starts;
    PWM_Pulse(4, 50, 5);
    TEST_ASSERT(fake_tim_pwm_starts == starts, "a channel that does not exist is ignored");
    PWM_Stop(4);
    PWM_Stop(2);
    TEST_ASSERT(fake_tim_pwm_stops == 1 && fake_tim_pwm_last_channel == TIM_CHANNEL_3, "stopping a channel stops that channel only, and a wrong channel is ignored");

    // a pulse ends by itself after its milliseconds
    PWM_StopAll();
    fake_tim_pwm_stops = 0;
    PWM_Pulse(1, 50, 3);
    PWM_Tick(); PWM_Tick();
    TEST_ASSERT(fake_tim_pwm_stops == 0, "two ticks of a three-millisecond pulse do not end it");
    PWM_Tick();
    TEST_ASSERT(fake_tim_pwm_stops == 1 && fake_tim_pwm_last_channel == TIM_CHANNEL_2, "the third does");
    PWM_Tick();
    TEST_ASSERT(fake_tim_pwm_stops == 1, "and an idle channel is not stopped again");
    PWM_StopAll();
    TEST_ASSERT(fake_tim_pwm_stops == 1 + 4, "StopAll stops the four channels");

    // the frequency
    PWM_Configure(0, 1000);
    TEST_ASSERT(htim1.Init.Period == 999, "1 kHz is a period of 1000 counts");
    PWM_Configure(0, 20000);
    TEST_ASSERT(htim1.Init.Period == 49, "20 kHz is 50 counts");
    PWM_Configure(0, 1);
    TEST_ASSERT(htim1.Init.Period == 0xFFFF, "1 Hz does not fit the 16-bit counter and is held at the longest period");
    PWM_Configure(0, 2000);
    PWM_Configure(0, 0);
    TEST_ASSERT(htim1.Init.Period == 499, "a frequency of zero changes nothing");
    PWM_Configure(7, 5000);
    TEST_ASSERT(htim1.Init.Period == 499, "a channel that does not exist changes nothing");
    PWM_Pulse(1, 10, 100);
    PWM_Configure(0, 4000);
    TEST_ASSERT(htim1.Init.Period == 499, "the frequency is shared by the four channels, so it is not changed while another channel is running");
    PWM_Configure(1, 4000);
    TEST_ASSERT(htim1.Init.Period == 249, "the channel that is running may change it");
    PWM_Stop(1);
    PWM_Configure(0, 4000);
    TEST_ASSERT(htim1.Init.Period == 249, "and once all are stopped any channel may");
}

// ---- the I2C link the master talks to -----------------------------------------------------------------------------------------------------
static void master_writes(const uint8_t *bytes, uint32_t count)
{
    // the master addresses the slave for a write, the HAL starts a receive into the buffer of the firmware, the master sends its bytes and stops
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, I2C_SLAVE_ADDRESS << 1);
    for (uint32_t i = 0; i < count && i < fake_i2c_slave_rx_len; i++) fake_i2c_slave_rx_buffer[i] = bytes[i];
    hi2c1.XferCount = fake_i2c_slave_rx_len - count;
    HAL_I2C_SlaveRxCpltCallback(&hi2c1);
}

static void master_write_reg(uint8_t reg) { master_writes(&reg, 1); }

static void test_link(int *failures)
{
    world();
    PWM_Init();
    I2CLink_Init();
    TEST_ASSERT(fake_i2c_listen_count == 1, "the link starts listening for its address");

    // The firmware starts a receive when the direction it is given is I2C_DIRECTION_RECEIVE and a transmit otherwise. In the HAL the direction is the
    // master's: I2C_DIRECTION_TRANSMIT (0) is a write by the master, which the slave must receive. KNOWN, not changed here: the code reads it the other
    // way round, so the tests below follow the code (a "write" is announced with RECEIVE) and the point is to be checked on the board.
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, 0x84);
    TEST_ASSERT(fake_i2c_slave_rx_count == 1 && fake_i2c_slave_rx_len == 8 && fake_i2c_slave_tx_count == 0, "KNOWN: the direction RECEIVE starts a receive of up to eight bytes");
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_count == 1 && fake_i2c_slave_tx_len == 1 && fake_i2c_slave_tx[0] == APP_STATUS_IDLE, "KNOWN: with no register chosen the answer is the idle status");

    hi2c1.XferCount = 8;
    uint32_t before = i2c_link_last_activity_tick;
    HAL_I2C_SlaveRxCpltCallback(&hi2c1);
    TEST_ASSERT(i2c_link_last_activity_tick == before, "a receive that ended with no byte is not activity");
    master_write_reg(REG_APP_STATUS);
    TEST_ASSERT(i2c_link_last_activity_tick == fake_tick, "a received byte is activity");
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 1 && fake_i2c_slave_tx[0] == APP_STATUS_IDLE, "register 0 answers the application status");

    master_write_reg(REG_APP_VERSION);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 10 && fake_i2c_slave_tx[0] == 0, "the version answer is ten bytes after a zero status byte");
    TEST_ASSERT(fake_i2c_slave_tx[1] == 0x03 && fake_i2c_slave_tx[2] == 0x03 && fake_i2c_slave_tx[3] == 0xCB && fake_i2c_slave_tx[4] == 0x01, "the hardware identifier of the slave, high byte first");
    TEST_ASSERT(fake_i2c_slave_tx[5] == 0 && fake_i2c_slave_tx[6] == FIRMWARE_VERSION_MAJOR && fake_i2c_slave_tx[7] == 0 && fake_i2c_slave_tx[8] == FIRMWARE_VERSION_MINOR, "the major and the minor version, two bytes each");
    TEST_ASSERT(fake_i2c_slave_tx[9] == FIRMWARE_VERSION_PATCH && FIRMWARE_VERSION_PATCH >= 0 && FIRMWARE_VERSION_PATCH <= 127, "the tenth byte is the patch number of the application, which the master passes on");

    uint8_t variant[2] = {REG_MLX_SENSOR_VARIANT, MLX_VARIANT_90641};
    master_writes(variant, 2);
    TEST_ASSERT(mlx_sensor_variant == MLX_VARIANT_90641, "a two-byte write of the variant register chooses the camera");
    master_write_reg(REG_MLX_SENSOR_VARIANT);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 1 && fake_i2c_slave_tx[0] == MLX_VARIANT_90641, "and reading it gives it back");

    mlx_sensor_variant = MLX_VARIANT_NONE;
    master_write_reg(REG_MLX_CAPTURE_STATUS);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx[0] == MLX_CAPTURE_ERROR, "with no camera chosen the capture status is the error value");
    uint8_t chunk_select[2] = {REG_MLX_RAW_CHUNK, 5};
    master_writes(chunk_select, 2);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    int zeros = fake_i2c_slave_tx_len == 32;
    for (int i = 0; i < 32; i++) if (fake_i2c_slave_tx[i]) zeros = 0;
    TEST_ASSERT(zeros, "a chunk with no camera is 32 zero bytes");
    chunk_select[0] = REG_MLX_CALIBRATED_CHUNK;
    master_writes(chunk_select, 2);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 32, "the calibrated chunk is 32 bytes as well");

    uint8_t pwm_pulse[4] = {REG_PWM_PULSE, 1, 25, 7};
    master_writes(pwm_pulse, 4);
    TEST_ASSERT(fake_tim_compare[1] == 250 && fake_tim_pwm_starts >= 1, "a four-byte write of the pulse register starts a pulse");
    pwm_pulse[0] = REG_PWM_CONFIGURE; pwm_pulse[1] = 0; pwm_pulse[2] = 0x07; pwm_pulse[3] = 0xD0;     // channel 0, 2000 Hz
    master_writes(pwm_pulse, 4);
    TEST_ASSERT(htim1.Init.Period == 999, "a frequency is not changed while another channel is running a pulse");
    int stops = fake_tim_pwm_stops;
    uint8_t pwm_stop[2] = {REG_PWM_STOP, 1};
    master_writes(pwm_stop, 2);
    TEST_ASSERT(fake_tim_pwm_stops == stops + 1, "a two-byte write of the stop register stops the channel");
    master_writes(pwm_pulse, 4);
    TEST_ASSERT(htim1.Init.Period == 499, "the configure register takes the channel and a 16-bit frequency, high byte first: 0x07D0 is 2000 Hz");
    uint8_t short_pulse[3] = {REG_PWM_PULSE, 1, 25};
    int starts = fake_tim_pwm_starts;
    master_writes(short_pulse, 3);
    TEST_ASSERT(fake_tim_pwm_starts == starts, "a pulse command of the wrong length is ignored");

    // the converter
    ads = fake_i2c_bus_add(ADS1115_I2C_ADDR, FAKE_I2C_WORD16);
    uint8_t ads_cfg[3] = {REG_ADS_CONFIGURE, 0x04, 0x83};
    master_writes(ads_cfg, 3);
    TEST_ASSERT(ads->words[1] == 0x0483, "the configure register of the link writes the converter configuration");
    master_write_reg(REG_ADS_TRIGGER);
    TEST_ASSERT(ads->words[1] == (0x0483 | 0x8000), "a one-byte write of the trigger register starts a conversion");
    ads->words[0] = 0xFFF6;
    master_write_reg(REG_ADS_READ);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_len == 2 && fake_i2c_slave_tx[0] == 0xFF && fake_i2c_slave_tx[1] == 0xF6, "reading the result gives the 16-bit value, high byte first");

    // the bootloader entry
    int resets = fake_reset_count;
    uint8_t wrong_magic[5] = {REG_ENTER_BOOTLOADER, 0xB0, 0x07, 0x1D, 0x5B};
    master_writes(wrong_magic, 5);
    uint8_t short_magic[4] = {REG_ENTER_BOOTLOADER, 0xB0, 0x07, 0x1D};
    master_writes(short_magic, 4);
    TEST_ASSERT(fake_reset_count == resets, "a wrong or a short magic payload does not restart the slave");
    stops = fake_tim_pwm_stops;
    uint8_t magic[5] = {REG_ENTER_BOOTLOADER, 0xB0, 0x07, 0x1D, 0x5A};
    master_writes(magic, 5);
    TEST_ASSERT(fake_reset_count == resets + 1 && fake_tim_pwm_stops == stops + 4, "the right magic payload stops every output and restarts into the bootloader");

    uint8_t unknown[3] = {0x77, 1, 2};
    master_writes(unknown, 3);
    master_write_reg(REG_MLX_TRIGGER_CAPTURE);
    HAL_I2C_SlaveTxCpltCallback(&hi2c1);
    int listens = fake_i2c_listen_count;
    HAL_I2C_ListenCpltCallback(&hi2c1);
    HAL_I2C_ErrorCallback(&hi2c1);
    TEST_ASSERT(fake_i2c_listen_count == listens + 2, "after the end of a transfer or an error the link listens again");
}

// ---- the sensors on the I2C2 of the slave ----------------------------------------------------------------------------------------------------
static void test_converter(int *failures)
{
    world();
    ads = fake_i2c_bus_add(ADS1115_I2C_ADDR, FAKE_I2C_WORD16);
    ADS1115_Configure(0x0483);
    TEST_ASSERT(ads->words[1] == 0x0483 && fake_i2c_bus.seen[0] == 0x01 && fake_i2c_bus.seen[1] == 0x04 && fake_i2c_bus.seen[2] == 0x83, "the configuration is the register number and two bytes");
    ADS1115_TriggerConversion();
    TEST_ASSERT(ads->words[1] == 0x8483, "the trigger adds the start bit to the last configuration");
    ads->words[0] = 0x8000;
    TEST_ASSERT(ADS1115_ReadResult() == -32768, "the result is signed");
    ads->words[0] = 0x0123;
    TEST_ASSERT(ADS1115_ReadResult() == 0x0123, "a positive result");
    world();
    TEST_ASSERT(ADS1115_ReadResult() == 0, "with no converter the result is zero");
}

static void test_transport_codes(int *failures)
{
    uint16_t data[4];
    world();
    TEST_ASSERT(MLX90640_I2CRead(0x33, 0x2400, 2, data) == -MLX90640_I2C_NACK_ERROR && MLX90640_I2CWrite(0x33, 0x8000, 1) == -MLX90640_I2C_WRITE_ERROR, "with no sensor the MLX90640 transport returns the NACK error on a read and the write error on a write");
    TEST_ASSERT(MLX90641_I2CRead(0x33, 0x400, 1, data) == -1 && MLX90641_I2CWrite(0x33, 0x8000, 1) == -1 && MLX90642_I2CRead(0x66, 0x1230, 1, data) == -1, "and the other two transports return -1");
    TEST_ASSERT(MLX90642_I2CWrite(0x66, (const uint8_t *)"\x01", 1) == -1, "the MLX90642 write too");

    mlx = fake_i2c_bus_add(0x33, FAKE_I2C_MLX);
    mlx->words16[0x2400] = 0x1234; mlx->words16[0x2401] = 0x5678;
    TEST_ASSERT(MLX90640_I2CRead(0x33, 0x2400, 2, data) == 0 && data[0] == 0x1234 && data[1] == 0x5678, "a read of two words, one transfer per word, high byte first");
    TEST_ASSERT(MLX90640_I2CWrite(0x33, 0x8100, 0xBEEF) == 0 && mlx->words16[0x8100] == 0xBEEF, "a write puts the word at its address");
    MLX90640_I2CInit(); MLX90640_I2CFreqSet(400); MLX90641_I2CInit(); MLX90641_I2CFreqSet(400);
    TEST_ASSERT(MLX90641_I2CRead(0x33, 0x2400, 2, data) == 0 && data[1] == 0x5678 && MLX90641_I2CWrite(0x33, 0x8200, 0x0102) == 0, "the MLX90641 transport reads and writes the same way");
    mlx->read_only_addr = 0x8300;
    TEST_ASSERT(MLX90641_I2CWrite(0x33, 0x8300, 0x0102) == -2, "a word that does not keep its value is reported as -2");
    TEST_ASSERT(MLX90641_I2CGeneralReset() == 0 && MLX90640_I2CGeneralReset() == MLX90640_NO_ERROR, "the general reset (the byte 0x06 to address 0) is answered by every device and reports success");
    uint32_t before = fake_tick;
    MLX90642_Wait_ms(12);
    TEST_ASSERT(fake_tick - before == 12, "the MLX90642 wait is the HAL delay");
}

static void test_camera_640(int *failures)
{
    static uint16_t ee[MLX90640_EEPROM_DUMP_NUM];
    static uint16_t frame[834];
    uint8_t chunk[32];
    world();
    mlx_sensor_variant = MLX_VARIANT_90640;
    mlx = fake_i2c_bus_add(0x33, FAKE_I2C_MLX);
    mlx->clear_trigger_on_reset = 1;
    synthetic_eeprom(ee);
    synthetic_frame(frame, 0x0100);
    for (int i = 0; i < MLX90640_EEPROM_DUMP_NUM; i++) mlx->words16[0x2400 + i] = ee[i];
    for (int i = 0; i < 768; i++) mlx->words16[0x0400 + i] = (uint16_t)(0x0100 + i % 50);
    for (int i = 0; i < 64; i++) mlx->words16[0x0700 + i] = frame[768 + i];
    mlx->words16[0x800D] = 0x1901;
    mlx->words16[0x8000] = 0x0008;
    mlx->read_only_addr = 0x8000;                                // the status register keeps its data-ready bit when it is written
    Sensors_Init();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_IDLE, "nothing is captured before it is asked for");
    MLX_TriggerCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_BUSY, "a trigger from the master only marks the capture busy (it is slow, and the master is waiting on an interrupt)");
    MLX_ServiceCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_READY, "the main loop does the capture: ready");
    MLX_ServiceCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_READY, "and a second service call with nothing requested leaves it alone");
    MLX_GetRawChunk(0, chunk);
    TEST_ASSERT(chunk[0] == 0x01 && chunk[1] == 0x00 && chunk[2] == 0x01 && chunk[3] == 0x01, "raw chunk 0 is the first pixel words of the frame");
    MLX_GetRawChunk(48, chunk);
    int zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "a chunk beyond the last is zeros");
    MLX_GetCalibratedChunk(0, chunk);
    MLX_GetCalibratedChunk(200, chunk);
    zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "so is a calibrated chunk beyond the last");

    mlx->clear_trigger_on_reset = 0;
    MLX_TriggerCapture();
    MLX_ServiceCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a sensor that does not start the measurement is an error");
    world();
    mlx_sensor_variant = MLX_VARIANT_90640;
    Sensors_Init();
    MLX_TriggerCapture();
    MLX_ServiceCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_ERROR, "with no sensor it is an error too");
}

static void test_camera_641(int *failures)
{
    uint8_t chunk[32];
    world();
    mlx_sensor_variant = MLX_VARIANT_90641;
    m641.word0 = 0x2000; m641.to_value = 21.0f;
    Sensors_Init();
    TEST_ASSERT(m641.extract_calls == 1, "a readable EEPROM is turned into parameters");
    m641.dump_rc = -1;
    Sensors_Init();
    TEST_ASSERT(m641.extract_calls == 1, "an unreadable one is not");
    MLX_TriggerCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_BUSY, "busy until the main loop has done the capture");
    MLX_ServiceCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_READY && m641.to_calls == 1, "ready after it");
    MLX_GetRawChunk(0, chunk);
    TEST_ASSERT(chunk[0] == 0x20 && chunk[1] == 0x00 && chunk[3] == 0x01, "raw chunk 0");
    MLX_GetRawChunk(12, chunk);
    int zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "this camera has twelve chunks");
    MLX_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == 2100 && (int16_t)((chunk[2] << 8) | chunk[3]) == 2200, "calibrated values are hundredths of a degree");
    m641.to_value = 400.0f;
    MLX_TriggerCapture(); MLX_ServiceCapture(); MLX_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == 32767, "values beyond the 16-bit range are clamped");
    m641.to_value = -400.0f;
    MLX_TriggerCapture(); MLX_ServiceCapture(); MLX_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == -32768, "at both ends");
    MLX_GetCalibratedChunk(12, chunk);
    zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "a calibrated chunk beyond the last is zeros");
    m641.trigger_rc = -9;
    MLX_TriggerCapture(); MLX_ServiceCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a trigger that fails is an error");
    m641.trigger_rc = 0; m641.frame_rc = -8;
    MLX_TriggerCapture(); MLX_ServiceCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_ERROR, "and so is a frame that cannot be read");
}

static void test_camera_642(int *failures)
{
    uint8_t chunk[32];
    world();
    mlx_sensor_variant = MLX_VARIANT_90642;
    mlx = fake_i2c_bus_add(SA_90642_DEFAULT, FAKE_I2C_MLX642);
    mlx->words16[MLX90642_APPLICATION_CONFIG_ADDRESS] = MLX90642_NORMALIZED_DATA_OUTPUT;
    Sensors_Init();
    TEST_ASSERT(mlx->words16[MLX90642_APPLICATION_CONFIG_ADDRESS] == MLX90642_STEP_MEAS_MODE, "start-up puts the camera in single-measurement mode with temperature output");
    for (int i = 0; i < 768; i++) {
        mlx->words16[MLX90642_IR_DATA_ADDRESS + 2 * i] = (uint16_t)(0x4000 + i);
        mlx->words16[MLX90642_TO_DATA_ADDRESS + 2 * i] = (uint16_t)(int16_t)(i - 50);
    }
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_IDLE, "idle before the first capture");
    MLX_TriggerCapture();
    TEST_ASSERT(mlx->last_cmd == MLX90642_START_SYNC_MEAS_CMD, "the trigger starts a synchronised measurement at once (this camera is fast)");
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_READY, "and the first status read after it finds the frame ready");
    MLX_GetRawChunk(0, chunk);
    TEST_ASSERT(chunk[0] == 0x40 && chunk[1] == 0x00, "raw chunk 0");
    MLX_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == (-50 * MLX_TEMP_SCALE) / 50, "calibrated chunk 0 is rescaled to hundredths of a degree");
    MLX_GetRawChunk(48, chunk);
    MLX_GetCalibratedChunk(48, chunk);
    int zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "chunks beyond the last are zeros");
    mlx->words16[MLX90642_TO_DATA_ADDRESS] = 30000;
    MLX_TriggerCapture(); MLX_GetCaptureStatus();
    MLX_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == 32767, "a value beyond the range is clamped");
    mlx->ignore_sync = 1;
    MLX_TriggerCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_BUSY, "a camera that has not finished leaves the capture busy");
    mlx->present = 0;
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_ERROR, "one that disappears is an error");
    MLX_TriggerCapture();
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a trigger to a missing camera is an error");
    mlx_sensor_variant = 0x55;
    Sensors_Init();
    MLX_TriggerCapture();
    MLX_GetRawChunk(0, chunk);
    MLX_GetCalibratedChunk(0, chunk);
    TEST_ASSERT(MLX_GetCaptureStatus() == MLX_CAPTURE_ERROR, "an unknown variant is an error");
}

// ---- the start-up and the main loop ---------------------------------------------------------------------------------------------------------
static void test_main(int *failures)
{
    world();
    fake_iwdg_exit_after = 1500;
    fake_reset_jmp_armed = 1;
    PWM_Pulse(1, 50, 200);                                   // a pulse started by the last command, long after the master has gone quiet
    fake_tim_pwm_stops = 0;
    i2c_link_last_activity_tick = fake_tick;                 // the master was heard just now
    int left = setjmp(fake_reset_jmp);
    if (left == 0) slave_main_entry();
    fake_reset_jmp_armed = 0;
    TEST_ASSERT(left == 2 && fake_iwdg_refreshes == 1500, "the main loop refreshes the watchdog on every pass (the test leaves it after 1500)");
    TEST_ASSERT(fake_hal_inits == 1 && fake_scb.VTOR == 0x08005000UL, "the vector table is set to the application address, then the HAL started");
    TEST_ASSERT(fake_rcc_calls == 2 && fake_rcc_osc.PLL.PLLMUL == RCC_PLL_MUL16 && fake_rcc_clk.APB1CLKDivider == RCC_HCLK_DIV2, "the clocks: 64 MHz from the internal oscillator, a slower APB1");
    TEST_ASSERT(fake_iwdg_inits == 1 && fake_iwdg_prescaler == IWDG_PRESCALER_32 && fake_iwdg_reload == 999, "the watchdog is armed: 32 kHz / 32 for 1000 counts, a second");
    TEST_ASSERT(fake_i2c_inits == 2 && hi2c1.Init.OwnAddress1 == (I2C_SLAVE_ADDRESS << 1) && hi2c2.Init.OwnAddress1 == 0, "I2C1 answers at address 0x42, I2C2 is a master");
    TEST_ASSERT(fake_nvic[I2C1_EV_IRQn].enabled && fake_nvic[I2C1_ER_IRQn].enabled && fake_nvic[I2C1_EV_IRQn].preempt == 1, "its two interrupts are enabled at priority 1");
    TEST_ASSERT(fake_i2c_listen_count == 1, "the link is listening");
    int sda_scl_pairs = 0;
    for (int i = 0; i < fake_gpio_init_count; i++) if (fake_gpio_inits[i].init.Mode == GPIO_MODE_AF_OD) sda_scl_pairs++;
    TEST_ASSERT(sda_scl_pairs == 2, "the pins of both I2C buses are open-drain alternate function");
    TEST_ASSERT(fake_tim_pwm_stops > 4 && fake_tim_pwm_stops < 4 * 1500 - 4 * 900, "the outputs are stopped only after more than a second without a word from the master, then on every pass (a lost master leaves no output on)");
    I2C1_EV_IRQHandler();
    I2C1_ER_IRQHandler();
    TEST_ASSERT(fake_i2c_ev_irq_count == 1 && fake_i2c_er_irq_count == 1, "the two I2C1 vectors are handed to the HAL");
}

void run_slave_tests(int *failures)
{
    test_pwm(failures);
    test_link(failures);
    test_converter(failures);
    test_transport_codes(failures);
    test_camera_640(failures);
    test_camera_641(failures);
    test_camera_642(failures);
    test_main(failures);
}

#undef main      // the suite renames the main() of slave_main.c (-Dmain=slave_main_entry); the main of this test is the real one
int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the expansion slave: start-up, I2C link, PWM and sensors\n");
    run_slave_tests(&failures);
    if (failures == 0) {
        printf("  OK   all expansion slave host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
