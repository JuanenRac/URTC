// =============================================================================
// URTC Firmware - Optional firmware features: the persistent switches (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Optional functions (stepper ramps, health frame, oscillator trim, heater PID, clear-error command, communication watchdog, following the emergency stop frame) are each
// behind a switch that is OFF unless a valid record in F-RAM says otherwise. This file is the
// part without hardware: the record, its checksum and the check of the 0x1AE command. The
// glue that reads and writes the F-RAM and answers on the bus is firmware_features.c.
#ifndef FW_FEATURES_H
#define FW_FEATURES_H

#include <stdint.h>

#define FEAT_MOTION_RAMPS   0x01u
#define FEAT_HEALTH_PROFILE 0x02u
#define FEAT_HSI_AUTOTRIM   0x04u
#define FEAT_THERMAL_PID    0x08u
#define FEAT_CLEAR_ERROR    0x10u  // the clear-error command 0x1B2 works
#define FEAT_COMMS_WATCHDOG 0x20u  // silence of the host cuts a stepper move and, with the next switch, lets the electromagnet go
#define FEAT_MAGNET_RELEASE 0x40u  // with the communication watchdog: the electromagnet lets go; without it, it holds
#define FEAT_ESTOP_FOLLOW   0x80u  // the head follows the emergency stop frame of the stack (see fw_estop_follow.h)
#define FEAT_SUPPORTED      0xFFu

// The second byte of the switch map (the first one has all eight bits taken). Every switch of it is OFF unless the record says otherwise.
#define FEAT_EXT_WATCHDOG_HOST_ONLY 0x01u  // with the communication watchdog: only a frame of the host side counts as a sign of life (see fw_safety_policy.h)
#define FEAT_EXT_ALLOW_UPDATE_ACTIVE_ACTUATOR 0x02u // the end of an update of a slave board is accepted while a tool is active (off: refused, as always)
#define FEAT_EXT_SUPPORTED          0x03u

// States reported in the 0x1BA answer (the enumeration pid_state of the protocol specification).
#define PID_STATE_OFF_VALUE        0u
#define PID_STATE_RUNNING_VALUE    1u
#define PID_STATE_AUTOTUNING_VALUE 2u
#define PID_STATE_DONE_VALUE       3u
#define PID_STATE_FAILED_VALUE     4u

#define FEAT_CONFIG_FRAM_ADDR 976u
#define FEAT_CONFIG_MAGIC     0x46454154UL // 'FEAT'
#define FEAT_CONFIG_VERSION_1 1u  // the first format: 8 bytes, one mask byte (still read, never written)
#define FEAT_CONFIG_VERSION   2u  // the format that is written: 16 bytes, two mask bytes
#define FEAT_CONFIG_RAW_SIZE  16u // the bytes of F-RAM that hold a record of any version (addresses 976 to 991)

// Version 1: still found on every board that was configured before the second mask byte existed.
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  struct_version; // 1
    uint8_t  mask;           // the features that are on
    uint8_t  reserved;
    uint8_t  checksum;       // CRC-8/SMBUS over every byte above
} FeatConfigV1_t; // 8 bytes

// Version 2: the record in memory is always this one; a version 1 record read from F-RAM is converted to it (second byte zero, every switch of it off).
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  struct_version; // 2
    uint8_t  mask;           // the features of the first byte that are on
    uint8_t  mask_ext;       // the features of the second byte that are on
    uint8_t  estop_alive_timeout_lo; // ESTOP_FOLLOW: the silence of the master after which a ready head latches, in ms, little endian (0 = the default of 100 ms);
    uint8_t  estop_alive_timeout_hi; // two bytes and not a uint16_t so that the packed layout is the same for every tool that reads the header
    uint8_t  reserved[6];    // 0; room for a third byte without a new version
    uint8_t  checksum;       // CRC-8/SMBUS over every byte above
} FeatConfig_t; // 16 bytes

void    Feat_ConfigBlank(FeatConfig_t *c);                 // every feature off, sealed
uint8_t Feat_ConfigChecksum(const FeatConfig_t *c);
void    Feat_ConfigSeal(FeatConfig_t *c);
uint8_t Feat_ConfigIsValid(const FeatConfig_t *c);
// The mask a record switches on: 0 for an invalid record (0xFF-filled F-RAM included) and for any bit this firmware does not have.
uint8_t Feat_ConfigMask(const FeatConfig_t *c);
uint8_t Feat_ConfigMaskExt(const FeatConfig_t *c); // the same for the second byte
// The configured silence of the master for ESTOP_FOLLOW: 0 for an invalid record (the default is then used); the limits are applied by EstopFollow_SetTimeout.
uint16_t Feat_ConfigEstopTimeoutMs(const FeatConfig_t *c);
void Feat_ConfigSetEstopTimeoutMs(FeatConfig_t *c, uint16_t ms); // before Feat_ConfigSeal
// Turns the 16 bytes read from F-RAM into the record in memory. 1 when they hold a sound record of version 1 (converted) or 2; 0 for anything else (erased,
// zeroed, damaged, a version this firmware does not know, a bad checksum), and then *c is the blank record: every feature off.
uint8_t Feat_ConfigDecode(const uint8_t raw[FEAT_CONFIG_RAW_SIZE], FeatConfig_t *c);

// 0x1AE: DLC 5, the four magic bytes 'FEAT', then the mask. 1 and *mask set when the command is valid; anything else is ignored.
uint8_t Feat_ParseCommand(uint8_t dlc, const uint8_t *data, uint8_t *mask);

// 0x1AE with the second mask byte: DLC 5 (the first form; the second byte is zero, so every switch of it goes off) or DLC 6. *extended is 1 for DLC 6.
uint8_t Feat_ParseCommandEx(uint8_t dlc, const uint8_t *data, uint8_t *mask, uint8_t *mask_ext, uint8_t *extended);

// 0x1AE of any of its three lengths: DLC 5 (the first byte of the mask), 6 (both bytes) or 8 (both bytes and the silence of the master of ESTOP_FOLLOW).
// 1 when the command is valid; anything else is ignored. *timeout_ms is the silence to store: FEAT_TIMEOUT_KEEP for the forms that do not carry one (the stored
// one stays), otherwise 0 (clear it, the default applies) or a value inside URTC_ESTOP_ALIVE_TIMEOUT_MIN_MS and _MAX_MS; a value outside them makes the whole
// command invalid, so nothing is changed. The silence is big endian on the bus (the byte 6 is the high one).
#define FEAT_TIMEOUT_KEEP 0xFFFFFFFFUL
uint8_t Feat_ParseWrite(uint8_t dlc, const uint8_t *data, uint8_t *mask, uint8_t *mask_ext, uint32_t *timeout_ms);

// 0x1AF query forms.
#define FEAT_QUERY_BASIC            0u  // three-byte answer
#define FEAT_QUERY_EXTENDED         1u  // six-byte answer (the byte 02)
#define FEAT_QUERY_TIMEOUT_IN_FORCE 2u  // eight-byte answer with the silence in force (the byte 03)
#define FEAT_QUERY_TIMEOUT_STORED   3u  // eight-byte answer with the silence stored in the record (the byte 04)
// Which answer a query gets: only a query of exactly one byte asks for more than the three-byte form.
uint8_t Feat_QueryForm(uint8_t dlc, const uint8_t *data);
// The eight-byte answer: the six bytes of the extended one, then the silence in milliseconds, high byte first.
void Feat_EncodeResponseTimeout(uint8_t stored, uint8_t running, uint8_t stored_ext, uint8_t running_ext, uint16_t timeout_ms, uint8_t out[8]);

// 0x1AF query: 1 when it asks for the six-byte answer (DLC 1, the byte 0x02); any other query gets the three-byte one.
uint8_t Feat_QueryWantsExtended(uint8_t dlc, const uint8_t *data);

// 0x1AF answer: stored, running, supported.
void Feat_EncodeResponse(uint8_t stored, uint8_t running, uint8_t out[3]);
// The six-byte answer: stored, running, supported, then the same three for the second byte.
void Feat_EncodeResponseExt(uint8_t stored, uint8_t running, uint8_t stored_ext, uint8_t running_ext, uint8_t out[6]);

#endif // FW_FEATURES_H
