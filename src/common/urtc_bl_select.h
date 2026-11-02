// =============================================================================
// URTC-COMMON - firmware_base/urtc_bl_select.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed: portable C held to the shared scenarios on the host (conformance/bootloader_select_vectors.json). Nothing
// in a bootloader calls it yet.
//
// Addressing one bootloader by its serial number when several share a bus. The serial is 32 bits folded from the 96-bit unique ID of the STM32 (the fold the
// application already uses for its multinode serial); a blank one (0 or 0xFFFFFFFF) takes no part. A bootloader that hears a command on 0x7EA is in
// *addressed mode* and obeys the single-head frames 0x7F0..0x7FF only while it is selected; with nobody addressing it, it behaves exactly as today.
//
//   0x7EA host to all   PROBE   01 nbits prefix(4)    those whose serial starts with the first nbits bits of prefix answer 0x7EB: 01 A5 (all send the SAME bytes)
//                       SELECT  02 serial(4) seq      the one with that serial is selected (and answers 0x7EB: 02 serial seq), the others are deselected
//                       RELEASE 03                    nobody stays selected
//                       UID     04 part               the selected one answers 0x7EB: 04 part word(4), part 0..2 (the 96-bit ID, to catch a duplicate serial)
//
// Selection lapses after URTC_BL_SELECT_LAPSE_MS unless repeated, except while an update runs; addressed mode lapses after URTC_BL_ADDRESSED_LAPSE_MS without a
// command. Time is a free-running 32-bit millisecond counter; differences are taken unsigned, so a wrap is harmless. The identifiers 0x7EA..0x7ED are PROPOSED
// (free in spec/can_protocol.json, not written into it yet).
#ifndef URTC_BL_SELECT_H
#define URTC_BL_SELECT_H

#include <stdint.h>

#define URTC_BL_ID_CMD            0x7EAu
#define URTC_BL_ID_RESP           0x7EBu
#define URTC_BL_ID_SIG_CHUNK      0x7ECu
#define URTC_BL_ID_KEY_ID         0x7EDu
#define URTC_BL_LEGACY_FIRST      0x7F0u
#define URTC_BL_LEGACY_LAST       0x7FFu
#define URTC_BL_OP_PROBE          1u
#define URTC_BL_OP_SELECT         2u
#define URTC_BL_OP_RELEASE        3u
#define URTC_BL_OP_UID            4u
#define URTC_BL_PRESENT_MARK      0xA5u
#define URTC_BL_SELECT_LAPSE_MS   2000u
#define URTC_BL_ADDRESSED_LAPSE_MS 10000u

typedef struct {
    uint32_t uid[3];
    uint32_t serial;
    uint8_t addressed;
    uint8_t selected;
    uint32_t last_cmd_ms;
    uint32_t select_ms;
} urtc_bl_node_t;

typedef struct {
    uint8_t send;       // 1 when there is an answer
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} urtc_bl_reply_t;

static inline uint32_t urtc_bl_rotl(uint32_t v, unsigned bits) { return (v << bits) | (v >> (32u - bits)); }
static inline uint32_t urtc_bl_serial_from_uid(uint32_t w0, uint32_t w1, uint32_t w2) { return w0 ^ urtc_bl_rotl(w1, 11u) ^ urtc_bl_rotl(w2, 22u); }
static inline int urtc_bl_serial_is_valid(uint32_t serial) { return serial != 0u && serial != 0xFFFFFFFFu; }
static inline uint32_t urtc_bl_get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline void urtc_bl_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static inline void urtc_bl_node_init(urtc_bl_node_t *n, uint32_t w0, uint32_t w1, uint32_t w2)
{
    n->uid[0] = w0;
    n->uid[1] = w1;
    n->uid[2] = w2;
    n->serial = urtc_bl_serial_from_uid(w0, w1, w2);
    n->addressed = 0;
    n->selected = 0;
    n->last_cmd_ms = 0;
    n->select_ms = 0;
}

static inline void urtc_bl_node_lapse(urtc_bl_node_t *n, uint32_t now_ms, int update_busy)
{
    if (update_busy) {
        return;   // a transfer that has started is never cut off by a timer here; the bootloader's own inactivity timeout ends it
    }
    if (n->addressed && (uint32_t)(now_ms - n->last_cmd_ms) > URTC_BL_ADDRESSED_LAPSE_MS) {
        n->addressed = 0;
        n->selected = 0;
    }
    if (n->selected && (uint32_t)(now_ms - n->select_ms) > URTC_BL_SELECT_LAPSE_MS) {
        n->selected = 0;
    }
}

// Should this bootloader obey a frame with this identifier right now?
static inline int urtc_bl_node_accepts(urtc_bl_node_t *n, uint32_t now_ms, uint16_t can_id, int update_busy)
{
    if (!urtc_bl_serial_is_valid(n->serial)) {
        return 1;
    }
    urtc_bl_node_lapse(n, now_ms, update_busy);
    if (can_id < URTC_BL_LEGACY_FIRST || can_id > URTC_BL_LEGACY_LAST) {
        return 1;
    }
    return !n->addressed || n->selected;
}

static inline int urtc_bl_node_prefix_matches(uint32_t serial, uint8_t nbits, uint32_t prefix)
{
    if (nbits == 0u) {
        return 1;
    }
    const unsigned shift = 32u - nbits;
    return (serial >> shift) == (prefix >> shift);
}

// A frame on 0x7EA. Fills `reply` (reply->send is 1 when there is something to transmit on 0x7EB).
static inline void urtc_bl_node_on_cmd(urtc_bl_node_t *n, uint32_t now_ms, const uint8_t *data, uint8_t dlc, int update_busy, urtc_bl_reply_t *reply)
{
    reply->send = 0;
    reply->id = (uint16_t)URTC_BL_ID_RESP;
    reply->dlc = 0;
    for (int i = 0; i < 8; i++) {
        reply->data[i] = 0;
    }
    if (!urtc_bl_serial_is_valid(n->serial) || data == NULL || dlc == 0u) {
        return;
    }
    urtc_bl_node_lapse(n, now_ms, update_busy);
    const uint8_t op = data[0];
    int heard = 0;
    if (op == URTC_BL_OP_PROBE && dlc == 6u && data[1] <= 32u) {
        heard = 1;
        if (urtc_bl_node_prefix_matches(n->serial, data[1], urtc_bl_get32(data + 2))) {
            reply->send = 1;
            reply->dlc = 2;
            reply->data[0] = (uint8_t)URTC_BL_OP_PROBE;
            reply->data[1] = (uint8_t)URTC_BL_PRESENT_MARK;
        }
    } else if (op == URTC_BL_OP_SELECT && dlc == 6u) {
        heard = 1;
        if (urtc_bl_get32(data + 1) == n->serial) {
            n->selected = 1;
            n->select_ms = now_ms;
            reply->send = 1;
            reply->dlc = 6;
            reply->data[0] = (uint8_t)URTC_BL_OP_SELECT;
            urtc_bl_put32(reply->data + 1, n->serial);
            reply->data[5] = data[5];
        } else {
            n->selected = 0;
        }
    } else if (op == URTC_BL_OP_RELEASE && dlc == 1u) {
        heard = 1;
        n->selected = 0;
    } else if (op == URTC_BL_OP_UID && dlc == 2u) {
        heard = 1;
        if (n->selected && data[1] < 3u) {
            reply->send = 1;
            reply->dlc = 6;
            reply->data[0] = (uint8_t)URTC_BL_OP_UID;
            reply->data[1] = data[1];
            urtc_bl_put32(reply->data + 2, n->uid[data[1]]);
        }
    }
    if (heard) {
        n->addressed = 1;
        n->last_cmd_ms = now_ms;
    }
}

#endif // URTC_BL_SELECT_H
