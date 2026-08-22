// =============================================================================
// URTC Expansion Slave Application Firmware - Shared types, register map,
// and cross-module externs
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Runs on the STM32F303CBT6 populating the ADVANCED expansion board
// variants, once slaveboot_protocol.c's JumpToApplication() hands control
// here. Owns 2 real hardware I2C buses on this chip:
//   I2C1 - the LINK bus back to the main board's own STM32F303CC (this
//          chip is SLAVE here, same physical bus/pins the bootloader
//          already used for its own update protocol - see
//          slave_i2c_link.c)
//   I2C2 - the LOCAL bus to this expansion board's own 2 sensor chips,
//          ADS1115 and MLX90640 (this chip is MASTER here - see
//          slave_i2c_sensors.c)
// Plus local PWM generation (see slave_pwm.c) for the handful of tools
// whose timing is precise enough to want generating right at the tool
// head rather than routed from the main board over a connector/cable.
// =============================================================================
#ifndef SLAVE_COMMON_H
#define SLAVE_COMMON_H

#include "stm32f3xx_hal.h"

// -----------------------------------------------------------------------
// Firmware identity - same values as slaveboot_common.h's own
// THIS_HARDWARE_ID/FIRMWARE_VERSION_MAJOR/FIRMWARE_VERSION_MINOR, but
// duplicated here rather than shared via a common include: the
// application and the bootloader never run at the same time and don't
// share a build (same relationship the main board's own application
// firmware already has with ITS bootloader - STM32F303CC_main.c doesn't
// include bootloader_common.h either), so keeping them independently-
// defined-but-value-matching is the existing project convention, not a
// gap specific to this chip.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID       0x0303CB01UL
// Incremental like the bootloaders (see bump_version.py at the repo root):
// PATCH+1 on every real build, odometer-carry into MINOR/MAJOR.
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0
#define FIRMWARE_VERSION_PATCH 7

// -----------------------------------------------------------------------
// Shared peripheral handles - defined once in slave_main.c, declared
// extern here so every module that needs one can see it without each
// module guessing at ownership.
// -----------------------------------------------------------------------
extern I2C_HandleTypeDef hi2c1; // LINK bus (slave mode)
extern I2C_HandleTypeDef hi2c2; // LOCAL sensor bus (master mode)
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef htim1; // local PWM generation - see slave_pwm.c
extern uint8_t mlx_sensor_variant; // MLX_VARIANT_* - which MLX9064x family member is actually populated, set by the main board over REG_MLX_SENSOR_VARIANT, defaults to MLX_VARIANT_NONE (no sensor configured) until told otherwise

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

#define REG_MLX_TRIGGER_CAPTURE   0x10 // write, 0 bytes (register-address-only write, same as a read-pointer-select) - starts a new capture on whichever MLX9064x variant REG_MLX_SENSOR_VARIANT is currently set to
#define REG_MLX_CAPTURE_STATUS    0x11 // read, 1 byte: see MLX_CAPTURE_* below
#define REG_MLX_RAW_CHUNK         0x12 // write 1 byte (chunk index), then read 32 bytes: raw pixel data, uint16_t x16 per chunk - chunk count and total pixel count depend on the configured variant (see MLX_VARIANT_* below): 48 chunks/768px for MLX90640, 12 chunks/192px for MLX90641
#define REG_MLX_CALIBRATED_CHUNK  0x13 // write 1 byte (chunk index), then read 32 bytes: calibrated temperature, int16_t x16 per chunk (centi-degrees C, see MLX_TEMP_SCALE below), same variant-dependent chunk count as the raw variant above
#define REG_MLX_SENSOR_VARIANT    0x14 // write 1 byte (MLX_VARIANT_*) to configure which of the 3 MLX9064x family members is actually populated on this board, or read 1 byte to query the current setting. Relayed here by the main board at boot (and on any later change) from its own persisted F-RAM setting - this chip has no persistent storage of its own, see EEPROM.TXT section 6 on the main board's own side of this. Writing this does NOT itself trigger re-initialization (no re-read of calibration EEPROM) - a board's own populated sensor doesn't change at runtime, so this is expected to be set once, early, and left alone; see slave_i2c_sensors.c's own note if that assumption ever needs revisiting

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

// Which MLX9064x family member is actually populated - see
// REG_MLX_SENSOR_VARIANT above. 0 means no sensor configured at all
// (the safe default) - deliberately NOT "assume MLX90640", since a
// real MLX90640 needs the main board to explicitly say so via its own
// 0x1A6 CAN command once, the same one-time hardware-configuration
// step expansion_board_type itself already requires.
#define MLX_VARIANT_NONE   0x00 // no sensor populated - see MLX_TriggerCapture/friends in slave_i2c_sensors.c for how each one handles this explicitly rather than falling through to a real sensor's own code path
#define MLX_VARIANT_90640 0x01 // 32x24, 768px, 48 chunks - Melexis's own official mlx90640-library
#define MLX_VARIANT_90641 0x02 // 16x12, 192px, 12 chunks - Melexis's own official mlx90641-library (melexis_mlx90641/)
#define MLX_VARIANT_90642 0x03 // 32x24, 768px, 48 chunks, onboard temperature calculation - melexis_mlx90642/

#define MLX_TEMP_SCALE 100 // REG_MLX_CALIBRATED_CHUNK values are degrees C * 100 (centi-degrees) as int16_t - e.g. 2350 means 23.50C; chosen over sending raw float32 to halve the byte count for the same pixel count (int16_t vs float32), which matters both for I2C2's own real transfer time and for whatever this eventually costs to relay across the link bus and onward over CAN

// -----------------------------------------------------------------------
// PWM channel assignment - see docs/PINOUT_SLAVE.txt for the underlying
// pin numbers once written; documented here at the firmware level as the
// stable interface REG_PWM_* actually addresses, independent of exactly
// which physical pin backs a given channel.
// -----------------------------------------------------------------------
#define PWM_CHANNEL_COUNT 4

#endif // SLAVE_COMMON_H
