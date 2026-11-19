// =============================================================================
// URTC Firmware - Optional firmware features: the persistent switches (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <string.h>
#include "fw_features.h"

static uint8_t crc8(const uint8_t *p, unsigned n) {
    uint8_t crc = 0;
    for (unsigned i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

uint8_t Feat_ConfigChecksum(const FeatConfig_t *c) { return crc8((const uint8_t *)c, sizeof(*c) - 1); }

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

// A bit this firmware does not have in either byte: the whole record is not trusted (both bytes read as zero).
static uint8_t has_unknown_bits(const FeatConfig_t *c) {
    return (uint8_t)((c->mask & (uint8_t)~FEAT_SUPPORTED) || (c->mask_ext & (uint8_t)~FEAT_EXT_SUPPORTED));
}

uint8_t Feat_ConfigMask(const FeatConfig_t *c) {
    if (!Feat_ConfigIsValid(c) || has_unknown_bits(c)) return 0;
    return c->mask;
}

uint8_t Feat_ConfigMaskExt(const FeatConfig_t *c) {
    if (!Feat_ConfigIsValid(c) || has_unknown_bits(c)) return 0;
    return c->mask_ext;
}

uint16_t Feat_ConfigEstopTimeoutMs(const FeatConfig_t *c) {
    if (!Feat_ConfigIsValid(c) || has_unknown_bits(c)) return 0;
    return (uint16_t)(c->estop_alive_timeout_lo | ((uint16_t)c->estop_alive_timeout_hi << 8));
}

void Feat_ConfigSetEstopTimeoutMs(FeatConfig_t *c, uint16_t ms) {
    c->estop_alive_timeout_lo = (uint8_t)(ms & 0xFFu);
    c->estop_alive_timeout_hi = (uint8_t)(ms >> 8);
}

uint8_t Feat_ConfigDecode(const uint8_t raw[FEAT_CONFIG_RAW_SIZE], FeatConfig_t *c) {
    FeatConfig_t next;
    uint32_t magic;
    memcpy(&magic, raw, sizeof magic);
    if (magic == FEAT_CONFIG_MAGIC && raw[4] == FEAT_CONFIG_VERSION_1) {
        FeatConfigV1_t old;
        memcpy(&old, raw, sizeof old);
        if (old.checksum == crc8((const uint8_t *)&old, sizeof(old) - 1)) {
            Feat_ConfigBlank(&next);
            next.mask = old.mask;      // the second byte stays zero: no switch of it was ever on
            Feat_ConfigSeal(&next);
            if (!has_unknown_bits(&next)) { *c = next; return 1; }
        }
    } else if (magic == FEAT_CONFIG_MAGIC && raw[4] == FEAT_CONFIG_VERSION) {
        memcpy(&next, raw, sizeof next);
        if (Feat_ConfigIsValid(&next) && !has_unknown_bits(&next)) { *c = next; return 1; }
    }
    Feat_ConfigBlank(c);  // erased, zeroed, damaged, a version from the future: every feature off
    return 0;
}

uint8_t Feat_ParseCommand(uint8_t dlc, const uint8_t *data, uint8_t *mask) {
    if (dlc != 5) return 0;
    if (data[0] != 0x46 || data[1] != 0x45 || data[2] != 0x41 || data[3] != 0x54) return 0; // 'FEAT'
    if (data[4] & (uint8_t)~FEAT_SUPPORTED) return 0;
    *mask = data[4];
    return 1;
}

uint8_t Feat_ParseCommandEx(uint8_t dlc, const uint8_t *data, uint8_t *mask, uint8_t *mask_ext, uint8_t *extended) {
    if (dlc != 5 && dlc != 6) return 0;
    if (data[0] != 0x46 || data[1] != 0x45 || data[2] != 0x41 || data[3] != 0x54) return 0; // 'FEAT'
    uint8_t ext = (dlc == 6) ? data[5] : 0;
    if ((data[4] & (uint8_t)~FEAT_SUPPORTED) || (ext & (uint8_t)~FEAT_EXT_SUPPORTED)) return 0;
    *mask = data[4];
    *mask_ext = ext;
    *extended = (uint8_t)(dlc == 6);
    return 1;
}

uint8_t Feat_QueryWantsExtended(uint8_t dlc, const uint8_t *data) { return (uint8_t)(dlc == 1 && data[0] == 0x02); }

void Feat_EncodeResponseExt(uint8_t stored, uint8_t running, uint8_t stored_ext, uint8_t running_ext, uint8_t out[6]) {
    Feat_EncodeResponse(stored, running, out);
    out[3] = stored_ext;
    out[4] = running_ext;
    out[5] = FEAT_EXT_SUPPORTED;
}

void Feat_EncodeResponse(uint8_t stored, uint8_t running, uint8_t out[3]) {
    out[0] = stored;
    out[1] = running;
    out[2] = FEAT_SUPPORTED;
}
