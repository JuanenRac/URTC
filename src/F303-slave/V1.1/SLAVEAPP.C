// =============================================================================
// URTC Expansion Slave Application Firmware (STM32F303CBT6) - monolithic
// build
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Single-file form of this project's OWN code, kept in sync with the
// partitioned form under partitioned/ on every change. Melexis's own
// MLX90640_API.c (melexis/, Apache-2.0) is deliberately NOT folded into
// this file even in this "monolithic" build - merging third-party code
// under a different license into a single blob would obscure its own
// copyright notice and license terms, which Apache-2.0 requires stay
// intact. It compiles and links as its own separate object here exactly
// as it does in the partitioned build; "monolithic" describes this
// project's own code, not vendored third-party libraries.
// =============================================================================
#include "stm32f3xx_hal.h"
#include "MLX90640_API.h"



// -----------------------------------------------------------------------
// Firmware identity - same values as slaveboot_common.h's own
// THIS_HARDWARE_ID/FIRMWARE_VERSION_MAJOR/FIRMWARE_VERSION_MINOR, but
// duplicated here rather than shared via a common include: the
// application and the bootloader never run at the same time and don't
// share a build (same relationship the main board's own application
// firmware already has with ITS bootloader - STM32F303CC.C doesn't
// include bootloader_common.h either), so keeping them independently-
// defined-but-value-matching is the existing project convention, not a
// gap specific to this chip.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID       0x0303CB01UL
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0

// -----------------------------------------------------------------------
// Shared peripheral handles - defined once in slave_main.c, declared
// extern here so every module that needs one can see it without each
// module guessing at ownership.
// -----------------------------------------------------------------------
extern I2C_HandleTypeDef hi2c1; // LINK bus (slave mode)
extern I2C_HandleTypeDef hi2c2; // LOCAL sensor bus (master mode)
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef htim1; // local PWM generation - see slave_pwm.c

// -----------------------------------------------------------------------
// Local sensor bus (I2C2) addresses
// -----------------------------------------------------------------------
#define ADS1115_I2C_ADDR   0x48 // ADDR pin tied to GND - the datasheet-default of the 4 selectable addresses (0x48-0x4B); this board's own schematic is what actually fixes which one is wired, noted here as the assumption this firmware is built against
#define MLX90640_I2C_ADDR  0x33 // fixed factory address, not configurable - this one isn't a board-wiring assumption, it's the only address this specific sensor ever answers on

// -----------------------------------------------------------------------
// I2C1 LINK protocol (application mode) - same register-style pattern as
// the bootloader's own REG_* (slaveboot_common.h), but this is a
// completely separate register space: the two never run at the same
// time (this is application code; the bootloader's own registers stop
// existing the moment JumpToApplication() hands off control here), so
// reusing low numbers isn't a collision, it's just a fresh table for a
// fresh context. Read-only registers here are almost all "write a 1-byte
// selector first (or nothing, for the fixed ones below), then read" -
// same convention already established for REG_QUERY_VERSION et al. in
// the bootloader.
// -----------------------------------------------------------------------
#define I2C_SLAVE_ADDRESS         0x42 // same address as the bootloader used - nothing on the link bus needs to tell bootloader and application apart by address, since only one of them is ever actually running

#define REG_APP_STATUS            0x00 // read, 1 byte: 0=idle, 1=busy (see APP_STATUS_* below)
#define REG_APP_VERSION           0x01 // read, 10 bytes: same field layout as the bootloader's own REG_QUERY_VERSION response, byte0=0 here (marks "application answering", mirroring the main board's own 0/1 convention for the same byte)
#define REG_ENTER_BOOTLOADER      0x02 // write, 4 bytes: magic payload 0xB0,0x07,0x1D,0x5A (same constant the main board's own application checks for its own CAN 0x7F0) - resets into slaveboot on a match; any other 4 bytes, or any other length, is silently ignored rather than treated as a malformed command worth reporting, same reasoning as the main board's own version: requiring an exact magic rather than just the register address alone is what keeps a corrupted/malformed link-bus transaction from accidentally resetting this chip mid-tool-operation

#define REG_MLX_TRIGGER_CAPTURE   0x10 // write, 0 bytes (register-address-only write, same as a read-pointer-select) - starts a new MLX90640 frame capture
#define REG_MLX_CAPTURE_STATUS    0x11 // read, 1 byte: see MLX_CAPTURE_* below
#define REG_MLX_RAW_CHUNK         0x12 // write 1 byte (chunk index 0-47), then read 32 bytes: raw pixel data, uint16_t x16 per chunk, 48 chunks x 16 = 768 pixels total
#define REG_MLX_CALIBRATED_CHUNK  0x13 // write 1 byte (chunk index 0-47), then read 32 bytes: calibrated temperature, int16_t x16 per chunk (centi-degrees C, see MLX_TEMP_SCALE below), same 48-chunk layout as the raw variant

#define REG_ADS_CONFIGURE         0x20 // write, 2 bytes: forwarded near-verbatim into the ADS1115's own 16-bit config register (see slave_i2c_sensors.c) - this chip doesn't interpret the bitfields, just relays them, same "generic passthrough" philosophy as the main board's own SPI passthrough to the expansion driver
#define REG_ADS_TRIGGER           0x21 // write, 0 bytes - starts a single-shot conversion using whatever config REG_ADS_CONFIGURE last set (or the chip's own power-on default if never set)
#define REG_ADS_READ              0x22 // read, 2 bytes: the most recent completed conversion result, raw signed 16-bit ADC counts exactly as the ADS1115 itself reports them - scaling to volts depends on the PGA gain field in whatever config was last sent, which is why this stays raw rather than this firmware guessing at a conversion

#define REG_PWM_CONFIGURE         0x30 // write, 3 bytes: channel (1 byte, 0-3) + frequency in Hz (2 bytes, big-endian) - sets up a channel without starting it
#define REG_PWM_PULSE             0x31 // write, 3 bytes: channel (1 byte) + duty percent (1 byte, 0-100) + pulse duration in ms (1 byte, 0=continuous until REG_PWM_STOP) - starts output
#define REG_PWM_STOP              0x32 // write, 1 byte: channel to stop

#define APP_STATUS_IDLE       0x00
#define APP_STATUS_BUSY       0x01

#define MLX_CAPTURE_IDLE      0x00 // no capture requested yet, or the last one's data has been fully read
#define MLX_CAPTURE_BUSY      0x01 // I2C2 transfer from the sensor in progress
#define MLX_CAPTURE_READY     0x02 // raw frame in RAM, calibration either already run or running - REG_MLX_RAW_CHUNK is servable as soon as this shows, REG_MLX_CALIBRATED_CHUNK once calibration finishes (see slave_i2c_sensors.c's own note on why these aren't the same moment)
#define MLX_CAPTURE_ERROR     0xFF // I2C2 transfer or sensor communication failed - chunk reads return stale/zeroed data until the next successful REG_MLX_TRIGGER_CAPTURE

#define MLX_TEMP_SCALE 100 // REG_MLX_CALIBRATED_CHUNK values are degrees C * 100 (centi-degrees) as int16_t - e.g. 2350 means 23.50C; chosen over sending raw float32 to halve the byte count for the same pixel count (int16_t vs float32), which matters both for I2C2's own real transfer time and for whatever this eventually costs to relay across the link bus and onward over CAN

// -----------------------------------------------------------------------
// PWM channel assignment - see docs/PINOUT_SLAVE.txt for the underlying
// pin numbers once written; documented here at the firmware level as the
// stable interface REG_PWM_* actually addresses, independent of exactly
// which physical pin backs a given channel.
// -----------------------------------------------------------------------
#define PWM_CHANNEL_COUNT 4



// -----------------------------------------------------------------------
// Generic I2C2 master transport
// -----------------------------------------------------------------------
#define I2C2_TIMEOUT_MS 50

static uint8_t I2C2_Write8(uint8_t addr, const uint8_t *data, uint16_t len) {
    return HAL_I2C_Master_Transmit(&hi2c2, addr << 1, (uint8_t*)data, len, I2C2_TIMEOUT_MS) == HAL_OK;
}

static uint8_t I2C2_Read8(uint8_t addr, uint8_t *data, uint16_t len) {
    return HAL_I2C_Master_Receive(&hi2c2, addr << 1, data, len, I2C2_TIMEOUT_MS) == HAL_OK;
}

// -----------------------------------------------------------------------
// ADS1115 - unchanged from the earlier version of this file; verified
// against Texas Instruments' own datasheet, no dependency on the
// MLX90640 library below.
// -----------------------------------------------------------------------
#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG     0x01
#define ADS1115_OS_START_SINGLE (1 << 15)

static uint16_t ads1115_last_config = 0x8583;

void ADS1115_Configure(uint16_t config_reg) {
    ads1115_last_config = config_reg;
    uint8_t buf[3];
    buf[0] = ADS1115_REG_CONFIG;
    buf[1] = (uint8_t)(config_reg >> 8);
    buf[2] = (uint8_t)(config_reg & 0xFF);
    I2C2_Write8(ADS1115_I2C_ADDR, buf, 3);
}

void ADS1115_TriggerConversion(void) {
    uint16_t start_cfg = ads1115_last_config | ADS1115_OS_START_SINGLE;
    uint8_t buf[3];
    buf[0] = ADS1115_REG_CONFIG;
    buf[1] = (uint8_t)(start_cfg >> 8);
    buf[2] = (uint8_t)(start_cfg & 0xFF);
    I2C2_Write8(ADS1115_I2C_ADDR, buf, 3);
}

int16_t ADS1115_ReadResult(void) {
    uint8_t ptr = ADS1115_REG_CONVERSION;
    if (!I2C2_Write8(ADS1115_I2C_ADDR, &ptr, 1)) return 0;
    uint8_t raw[2];
    if (!I2C2_Read8(ADS1115_I2C_ADDR, raw, 2)) return 0;
    return (int16_t)((raw[0] << 8) | raw[1]);
}

// -----------------------------------------------------------------------
// MLX90640_I2C_Driver.h implementation - the platform-specific transport
// Melexis's own MLX90640_API.c calls into. Built directly on this file's
// own I2C2_Write8/Read8 above, same pattern as ADS1115's own driver
// pair, just with the 16-bit register addressing this sensor uses
// instead of ADS1115's 8-bit pointer register.
// -----------------------------------------------------------------------
void MLX90640_I2CInit(void) {
    // I2C2 itself is already brought up in slave_main.c
    // (MX_I2C2_Init_Master) before Sensors_Init calls this - nothing
    // sensor-specific needed here beyond that.
}

int MLX90640_I2CGeneralReset(void) {
    // Standard I2C General Call Reset: address 0x00, data byte 0x06 -
    // not MLX90640-specific, this is generic I2C bus protocol every
    // device supporting general call reset responds to the same way.
    uint8_t reset_cmd = 0x06;
    return I2C2_Write8(0x00, &reset_cmd, 1) ? MLX90640_NO_ERROR : -MLX90640_I2C_NACK_ERROR;
}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
    uint8_t addr_buf[2] = {(uint8_t)(startAddress >> 8), (uint8_t)(startAddress & 0xFF)};
    if (!I2C2_Write8(slaveAddr, addr_buf, 2)) return -MLX90640_I2C_NACK_ERROR;
    uint8_t raw[2];
    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        if (!I2C2_Read8(slaveAddr, raw, 2)) return -MLX90640_I2C_NACK_ERROR;
        data[i] = ((uint16_t)raw[0] << 8) | raw[1];
    }
    return MLX90640_NO_ERROR;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(writeAddress >> 8);
    buf[1] = (uint8_t)(writeAddress & 0xFF);
    buf[2] = (uint8_t)(data >> 8);
    buf[3] = (uint8_t)(data & 0xFF);
    if (!I2C2_Write8(slaveAddr, buf, 4)) return -MLX90640_I2C_WRITE_ERROR;
    return MLX90640_NO_ERROR;
}

void MLX90640_I2CFreqSet(int freq) {
    (void)freq; // no-op - this chip's I2C2 bus speed is fixed at init (MX_I2C2_Init_Master, 100kHz), not runtime-adjustable through this driver layer; the library calling this expecting a live frequency change is the one case its own generic driver interface doesn't map cleanly onto this project's own I2C2 setup
}

// -----------------------------------------------------------------------
// MLX90640 - application-level capture and chunk-serving
// -----------------------------------------------------------------------
static uint8_t mlx_capture_status = MLX_CAPTURE_IDLE;
static paramsMLX90640 mlx_params; // extracted calibration constants - populated once by Sensors_Init, reused by every capture after
static uint16_t mlx_frame_data[834]; // MLX90640_GetFrameData's own documented buffer size - 768 pixels + 64 aux words + frameData[832]/[833] control/subpage metadata, confirmed against its actual implementation rather than assumed
static float mlx_calibrated_frame[768];

void Sensors_Init(void) {
    static uint16_t ee_data[MLX90640_EEPROM_DUMP_NUM]; // static, not a stack local - 1664 bytes comfortably fits this chip's 40KB RAM but not the linker script's own 1KB minimum stack reservation
    MLX90640_I2CInit();
    if (MLX90640_DumpEE(MLX90640_I2C_ADDR, ee_data) == MLX90640_NO_ERROR) {
        MLX90640_ExtractParameters(ee_data, &mlx_params);
    }
    // A DumpEE failure here (sensor not present/not responding) leaves
    // mlx_params zero-initialized - MLX_CAPTURE_ERROR is what
    // MLX90640_TriggerCapture reports if a real capture is attempted
    // against that state, rather than this function itself blocking
    // startup on a sensor that might genuinely not be populated on a
    // given board variant.
}

void MLX90640_TriggerCapture(void) {
    mlx_capture_status = MLX_CAPTURE_BUSY;

    if (MLX90640_TriggerMeasurement(MLX90640_I2C_ADDR) < 0) {
        mlx_capture_status = MLX_CAPTURE_ERROR;
        return;
    }
    if (MLX90640_GetFrameData(MLX90640_I2C_ADDR, mlx_frame_data) < 0) {
        mlx_capture_status = MLX_CAPTURE_ERROR;
        return;
    }

    float vdd = MLX90640_GetVdd(mlx_frame_data, &mlx_params);
    float ta = MLX90640_GetTa(mlx_frame_data, &mlx_params);
    (void)vdd; // read for completeness/future use (e.g. exposing supply voltage as its own diagnostic register) - not otherwise consumed here, since MLX90640_CalculateTo below already applies its own internal Vdd compensation without needing this value passed in separately
    // Reflected/ambient temperature (tr) and target emissivity both feed
    // directly into the calibration math - tr defaults to ta-8 per
    // Melexis's own reference examples (a reasonable assumption absent
    // a separate way to measure true reflected temperature), emissivity
    // 0.95 is a common default for non-reflective PCB surfaces/solder
    // mask, not a measured value for this specific board - both worth
    // revisiting once real hardware is available to tune against actual
    // target surfaces this tool inspects.
    float tr = ta - 8.0f;
    float emissivity = 0.95f;
    MLX90640_CalculateTo(mlx_frame_data, &mlx_params, emissivity, tr, mlx_calibrated_frame);

    mlx_capture_status = MLX_CAPTURE_READY;
}

uint8_t MLX90640_GetCaptureStatus(void) {
    return mlx_capture_status;
}

void MLX90640_GetRawChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        out[i*2]   = (uint8_t)(mlx_frame_data[base+i] >> 8);
        out[i*2+1] = (uint8_t)(mlx_frame_data[base+i] & 0xFF);
    }
}

void MLX90640_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]) {
    if (chunk_index >= 48) { for (int i = 0; i < 32; i++) out[i] = 0; return; }
    uint16_t base = chunk_index * 16;
    for (int i = 0; i < 16; i++) {
        int16_t v = (int16_t)(mlx_calibrated_frame[base+i] * MLX_TEMP_SCALE);
        out[i*2]   = (uint8_t)(((uint16_t)v) >> 8);
        out[i*2+1] = (uint8_t)(((uint16_t)v) & 0xFF);
    }
}



// TIM1 counts at 1MHz regardless of frequency requested (PSC fixed,
// derived from SystemClock_Config's own 64MHz - see slave_main.c) - ARR
// is what actually varies per-channel to hit a given frequency. 1MHz
// resolution (1us per tick) comfortably covers every tool this firmware
// currently targets (solder paste jetting explicitly wants sub-
// millisecond precision - 1us resolution is 1000x finer than that).
#define TIM1_COUNTER_CLOCK_HZ 1000000UL

static uint8_t pulse_remaining_ms[PWM_CHANNEL_COUNT] = {0};

void PWM_Init(void) {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // TIM1 CH1-CH4 on PA8-PA11 (this chip's default AF6 mapping, same
    // alternate function this project already uses for TIM1 on the main
    // board's own STM32F303CC - not a coincidence, both chips are the
    // same F3 family and share this peripheral's pin mapping convention).
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = (SystemCoreClock / TIM1_COUNTER_CLOCK_HZ) - 1;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 999; // placeholder 1kHz - PWM_Configure overwrites this per-channel-request before any channel actually starts
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0; // 0% duty until PWM_Pulse sets a real value
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4);

    // TIM1 is an advanced-control timer - its outputs stay disabled at
    // the break/dead-time-generator level until this is set, even with a
    // channel individually started, unlike this project's other, simpler
    // timers.
    __HAL_TIM_MOE_ENABLE(&htim1);
}

static uint32_t ChannelToHalChannel(uint8_t channel) {
    switch (channel) {
        case 0: return TIM_CHANNEL_1;
        case 1: return TIM_CHANNEL_2;
        case 2: return TIM_CHANNEL_3;
        default: return TIM_CHANNEL_4;
    }
}

void PWM_Configure(uint8_t channel, uint16_t frequency_hz) {
    if (channel >= PWM_CHANNEL_COUNT || frequency_hz == 0) return;
    uint32_t period = (TIM1_COUNTER_CLOCK_HZ / frequency_hz);
    if (period > 0) period -= 1;
    __HAL_TIM_SET_AUTORELOAD(&htim1, period);
}

void PWM_Pulse(uint8_t channel, uint8_t duty_percent, uint8_t duration_ms) {
    if (channel >= PWM_CHANNEL_COUNT) return;
    if (duty_percent > 100) duty_percent = 100;
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    uint32_t ccr = ((uint32_t)duty_percent * (arr + 1)) / 100;
    __HAL_TIM_SET_COMPARE(&htim1, ChannelToHalChannel(channel), ccr);
    HAL_TIM_PWM_Start(&htim1, ChannelToHalChannel(channel));
    pulse_remaining_ms[channel] = duration_ms; // 0 stays 0 - PWM_Tick only ever counts down a nonzero value, so a continuous request (duration_ms=0) is correctly never auto-stopped
}

void PWM_Stop(uint8_t channel) {
    if (channel >= PWM_CHANNEL_COUNT) return;
    HAL_TIM_PWM_Stop(&htim1, ChannelToHalChannel(channel));
    pulse_remaining_ms[channel] = 0;
}

void PWM_Tick(void) {
    // Called once per main loop iteration, which slave_main.c paces at
    // 1ms via HAL_Delay - each call here represents 1ms elapsed, so
    // decrementing by 1 per call is a millisecond countdown without a
    // dedicated hardware timer interrupt for it.
    for (uint8_t ch = 0; ch < PWM_CHANNEL_COUNT; ch++) {
        if (pulse_remaining_ms[ch] > 0) {
            pulse_remaining_ms[ch]--;
            if (pulse_remaining_ms[ch] == 0) {
                PWM_Stop(ch);
            }
        }
    }
}



static uint8_t rx_buffer[8]; // largest real write is 4 bytes (1 register + 3 payload, REG_PWM_CONFIGURE/REG_PWM_PULSE) - sized with margin
static uint8_t tx_buffer[32]; // largest real read is 32 bytes (REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK)
static uint8_t pending_read_register = 0xFF;
static uint8_t pending_mlx_chunk_index = 0;

void I2CLink_Init(void) {
    HAL_I2C_EnableListen_IT(&hi2c1);
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode) {
    (void)AddrMatchCode;
    if (TransferDirection == I2C_DIRECTION_RECEIVE) {
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, rx_buffer, sizeof(rx_buffer), I2C_LAST_FRAME);
    } else {
        uint8_t len = 1;
        switch (pending_read_register) {
            case REG_APP_STATUS:
                tx_buffer[0] = APP_STATUS_IDLE; // this chip has no long-running blocking operation on the application side the way STATUS_COPYING is on the bootloader side - every REG_* handler below returns well within one I2C transaction, so there's currently never a moment this would read APP_STATUS_BUSY; kept as its own register anyway for forward compatibility rather than hardcoding APP_STATUS_IDLE assumptions into callers
                len = 1;
                break;
            case REG_APP_VERSION:
                tx_buffer[0] = 0x00; // 0=application answering, mirrors the bootloader's own 1=bootloader convention on this same byte
                tx_buffer[1] = (uint8_t)(THIS_HARDWARE_ID >> 24);
                tx_buffer[2] = (uint8_t)(THIS_HARDWARE_ID >> 16);
                tx_buffer[3] = (uint8_t)(THIS_HARDWARE_ID >> 8);
                tx_buffer[4] = (uint8_t)(THIS_HARDWARE_ID);
                tx_buffer[5] = (uint8_t)(FIRMWARE_VERSION_MAJOR >> 8);
                tx_buffer[6] = (uint8_t)(FIRMWARE_VERSION_MAJOR);
                tx_buffer[7] = (uint8_t)(FIRMWARE_VERSION_MINOR >> 8);
                tx_buffer[8] = (uint8_t)(FIRMWARE_VERSION_MINOR);
                tx_buffer[9] = 0; // reserved, matches the bootloader's own 10-byte response length for a consistent read size regardless of who's answering
                len = 10;
                break;
            case REG_MLX_CAPTURE_STATUS:
                tx_buffer[0] = MLX90640_GetCaptureStatus();
                len = 1;
                break;
            case REG_MLX_RAW_CHUNK:
                MLX90640_GetRawChunk(pending_mlx_chunk_index, tx_buffer);
                len = 32;
                break;
            case REG_MLX_CALIBRATED_CHUNK:
                MLX90640_GetCalibratedChunk(pending_mlx_chunk_index, tx_buffer);
                len = 32;
                break;
            case REG_ADS_READ: {
                int16_t result = ADS1115_ReadResult();
                tx_buffer[0] = (uint8_t)(((uint16_t)result) >> 8);
                tx_buffer[1] = (uint8_t)(((uint16_t)result) & 0xFF);
                len = 2;
                break;
            }
            default:
                tx_buffer[0] = APP_STATUS_IDLE;
                len = 1;
                break;
        }
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c, tx_buffer, len, I2C_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    uint32_t bytes_received = sizeof(rx_buffer) - hi2c->XferCount;
    if (bytes_received == 0) return;

    uint8_t reg = rx_buffer[0];

    // REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK are the one case in this
    // protocol where a "read setup" write carries a payload byte (the
    // chunk index) alongside the register selector, rather than being a
    // bare 1-byte register-only write the way every other read-pointer
    // selection in this protocol works - handled first and explicitly so
    // the generic 1-byte-vs-more-bytes branch below doesn't need to know
    // about this one exception.
    if (bytes_received == 2 && (reg == REG_MLX_RAW_CHUNK || reg == REG_MLX_CALIBRATED_CHUNK)) {
        pending_read_register = reg;
        pending_mlx_chunk_index = rx_buffer[1];
        return;
    }

    if (bytes_received == 1) {
        pending_read_register = reg;
        if (reg == REG_MLX_TRIGGER_CAPTURE) {
            MLX90640_TriggerCapture();
        } else if (reg == REG_ADS_TRIGGER) {
            ADS1115_TriggerConversion();
        }
        return;
    }

    switch (reg) {
        case REG_ENTER_BOOTLOADER:
            if (bytes_received == 5 && rx_buffer[1] == 0xB0 && rx_buffer[2] == 0x07 &&
                rx_buffer[3] == 0x1D && rx_buffer[4] == 0x5A) {
                // Same shutdown-then-reset pattern as the main board's own
                // 0x7F0 handler - stop any active local PWM output before
                // resetting, since a continuous-mode pulse (duration_ms=0)
                // would otherwise keep driving its GPIO right up until the
                // reset actually takes effect.
                for (uint8_t ch = 0; ch < PWM_CHANNEL_COUNT; ch++) {
                    PWM_Stop(ch);
                }
                HAL_Delay(5); // let the stop above settle before resetting, same order-of-operations reasoning as the main board's own ~5ms settle wait
                NVIC_SystemReset();
                // never reached
            }
            break;
        case REG_ADS_CONFIGURE:
            if (bytes_received == 3) {
                uint16_t cfg = ((uint16_t)rx_buffer[1] << 8) | rx_buffer[2];
                ADS1115_Configure(cfg);
            }
            break;
        case REG_PWM_CONFIGURE:
            if (bytes_received == 4) {
                uint8_t channel = rx_buffer[1];
                uint16_t freq = ((uint16_t)rx_buffer[2] << 8) | rx_buffer[3];
                PWM_Configure(channel, freq);
            }
            break;
        case REG_PWM_PULSE:
            if (bytes_received == 4) {
                uint8_t channel = rx_buffer[1];
                uint8_t duty = rx_buffer[2];
                uint8_t duration = rx_buffer[3];
                PWM_Pulse(channel, duty, duration);
            }
            break;
        case REG_PWM_STOP:
            if (bytes_received == 2) {
                PWM_Stop(rx_buffer[1]);
            }
            break;
        default:
            break;
    }
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    (void)hi2c;
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
    HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    HAL_I2C_EnableListen_IT(hi2c);
}



I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
IWDG_HandleTypeDef hiwdg;
TIM_HandleTypeDef htim1;

static void SystemClock_Config(void) {
    // Identical to slaveboot_main.c's own SystemClock_Config - same HSI-
    // only, no-external-crystal reasoning applies to the application as
    // much as the bootloader; see that file's own top-of-file note.
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2; // I2C1/I2C2 both live on APB1, max 36MHz - 64/2=32MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1; // TIM1 lives on APB2, no such ceiling at 64MHz
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    // I2C1 (link, slave) on PB6/PB7 - same pins the bootloader already
    // used for this same bus.
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // I2C2 (local sensor bus, master) on PB10/PB11 - this chip's default
    // I2C2 remap, open-drain with pull-ups required on this board (same
    // convention as every other I2C bus in this project).
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void MX_I2C1_Init_Slave(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x2000090E; // same computed value as the bootloader's own I2C1 init - identical PCLK1 (32MHz), identical target (100kHz standard mode)
    hi2c1.Init.OwnAddress1 = (I2C_SLAVE_ADDRESS << 1);
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; // same reasoning as the bootloader - register handlers here (particularly anything touching the local sensor bus) aren't necessarily instant
    HAL_I2C_Init(&hi2c1);

    __HAL_RCC_I2C1_CLK_ENABLE();
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}

static void MX_I2C2_Init_Master(void) {
    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x2000090E; // same 100kHz-at-32MHz timing as I2C1 - both ADS1115 and MLX90640 are comfortably within standard mode's own 100kHz ceiling (MLX90640 supports up to 1MHz Fast Mode+, but nothing here currently needs that speed)
    hi2c2.Init.OwnAddress1 = 0; // master-only role on this bus - no slave address of its own needed
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
    __HAL_RCC_I2C2_CLK_ENABLE();
    // No interrupt/NVIC setup here deliberately - slave_i2c_sensors.c
    // talks to both local chips with HAL's own blocking
    // Master_Transmit/Receive calls (50ms timeout each), not interrupt-
    // driven, so nothing here needs an ISR the way I2C1's slave role does.
}

static void MX_IWDG_Init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 999;
    HAL_IWDG_Init(&hiwdg);
}

void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c1); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c1); }

int main(void) {
    // Relocates its own vector table right at the start, before
    // HAL_Init() - same defensive convention already established for the
    // main board's own application firmware (see this project's own
    // note on that: the bootloader already does this before jumping
    // here, so this isn't covering a live gap, it's cheap insurance
    // against a future refactor reordering either side's own VTOR write).
    SCB->VTOR = 0x08005000UL; // MAIN_APP_ADDR from slaveboot_common.h - duplicated as a literal here since the application doesn't share that bootloader-only header, same as how the main board's own application firmware doesn't include bootloader_common.h either
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_IWDG_Init();
    MX_I2C1_Init_Slave();
    MX_I2C2_Init_Master();
    PWM_Init();
    Sensors_Init();
    I2CLink_Init();

    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        PWM_Tick();
        HAL_Delay(1);
    }
}
