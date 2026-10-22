// =============================================================================
// URTC Firmware - Optional firmware features: the persistent switches (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Four optional functions (stepper ramps, health frame, oscillator trim, heater PID) are each
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
#define FEAT_SUPPORTED      0x0Fu

// States reported in the 0x1BA answer (the enumeration pid_state of the protocol specification).
#define PID_STATE_OFF_VALUE        0u
#define PID_STATE_RUNNING_VALUE    1u
#define PID_STATE_AUTOTUNING_VALUE 2u
#define PID_STATE_DONE_VALUE       3u
#define PID_STATE_FAILED_VALUE     4u

#define FEAT_CONFIG_FRAM_ADDR 976u
#define FEAT_CONFIG_MAGIC     0x46454154UL // 'FEAT'
#define FEAT_CONFIG_VERSION   1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  struct_version;
    uint8_t  mask;           // the features that are on
    uint8_t  reserved;
    uint8_t  checksum;       // CRC-8/SMBUS over every byte above
} FeatConfig_t; // 8 bytes

void    Feat_ConfigBlank(FeatConfig_t *c);                 // every feature off, sealed
uint8_t Feat_ConfigChecksum(const FeatConfig_t *c);
void    Feat_ConfigSeal(FeatConfig_t *c);
uint8_t Feat_ConfigIsValid(const FeatConfig_t *c);
// The mask a record switches on: 0 for an invalid record (0xFF-filled F-RAM included) and for any bit this firmware does not have.
uint8_t Feat_ConfigMask(const FeatConfig_t *c);

// 0x1AE: DLC 5, the four magic bytes 'FEAT', then the mask. 1 and *mask set when the command is valid; anything else is ignored.
uint8_t Feat_ParseCommand(uint8_t dlc, const uint8_t *data, uint8_t *mask);

// 0x1AF answer: stored, running, supported.
void Feat_EncodeResponse(uint8_t stored, uint8_t running, uint8_t out[3]);

#endif // FW_FEATURES_H
