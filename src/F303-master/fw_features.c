// =============================================================================
// URTC Firmware - Optional firmware features: the persistent switches (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <string.h>
#include "fw_features.h"

uint8_t Feat_ConfigChecksum(const FeatConfig_t *c) {
    const uint8_t *p = (const uint8_t *)c;
    uint8_t crc = 0;
    for (unsigned i = 0; i < sizeof(*c) - 1; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

void Feat_ConfigSeal(FeatConfig_t *c) { c->checksum = Feat_ConfigChecksum(c); }

void Feat_ConfigBlank(FeatConfig_t *c) {
    memset(c, 0, sizeof(*c));
    c->magic = FEAT_CONFIG_MAGIC;
    c->struct_version = FEAT_CONFIG_VERSION;
    Feat_ConfigSeal(c);
}

uint8_t Feat_ConfigIsValid(const FeatConfig_t *c) {
    return (uint8_t)(c->magic == FEAT_CONFIG_MAGIC && c->struct_version == FEAT_CONFIG_VERSION &&
                     c->checksum == Feat_ConfigChecksum(c));
}

uint8_t Feat_ConfigMask(const FeatConfig_t *c) {
    if (!Feat_ConfigIsValid(c)) return 0;
    if (c->mask & (uint8_t)~FEAT_SUPPORTED) return 0; // a bit this firmware does not have: the whole record is not trusted
    return c->mask;
}

uint8_t Feat_ParseCommand(uint8_t dlc, const uint8_t *data, uint8_t *mask) {
    if (dlc != 5) return 0;
    if (data[0] != 0x46 || data[1] != 0x45 || data[2] != 0x41 || data[3] != 0x54) return 0; // 'FEAT'
    if (data[4] & (uint8_t)~FEAT_SUPPORTED) return 0;
    *mask = data[4];
    return 1;
}

void Feat_EncodeResponse(uint8_t stored, uint8_t running, uint8_t out[3]) {
    out[0] = stored;
    out[1] = running;
    out[2] = FEAT_SUPPORTED;
}
