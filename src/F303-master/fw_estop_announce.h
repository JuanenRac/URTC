// =============================================================================
// URTC Firmware - The silence of the master, announced by the master itself (pure logic, no hardware)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// OFF in the build (URTC_ESTOP_MASTER_ANNOUNCE 0): the identifier is then ignored like any identifier nobody uses. docs/ESTOP_MASTER_ANNOUNCE.md has the
// frame and the rules. The value only ever makes a head less patient than its own configuration, never more. The source byte says who claims to send the
// frame; it does not authenticate it. Nothing here has run on a board.
#ifndef FW_ESTOP_ANNOUNCE_H
#define FW_ESTOP_ANNOUNCE_H

#include "fw_estop_follow.h"

#ifndef URTC_ESTOP_MASTER_ANNOUNCE
#define URTC_ESTOP_MASTER_ANNOUNCE 0
#endif
// Right after the stop identifier, and below every identifier of a tool head and every window of a slot of the stack.
#ifndef URTC_ESTOP_ANNOUNCE_CAN_ID
#define URTC_ESTOP_ANNOUNCE_CAN_ID 0x002u
#endif

#define ESTOPA_KIND_TIMEOUT 0x04u

typedef struct {
    uint8_t have_seq;
    uint16_t last_seq;
} EstopAnnounce_t;

// 1 and the values when the bytes are an announcement of the master, 0 for anything else.
static inline uint8_t EstopAnnounce_Decode(const uint8_t *data, uint8_t dlc, uint16_t *timeout_ms, uint16_t *seq)
{
    if (dlc != 8u || data[0] != ESTOPA_KIND_TIMEOUT || data[1] != ESTOPF_SOURCE_BRAIN) return 0;
    if (data[6] != (uint8_t)(data[2] ^ 0xFFu) || data[7] != (uint8_t)(data[3] ^ 0xFFu)) return 0;
    *timeout_ms = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
    *seq = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
    return 1;
}

// One frame of the announcement identifier. 1 when it was taken (the silence in use is then node->timeout_ms), 0 when it was ignored. A frame that is
// ignored never stops the head and never changes anything: the rule "a stop is never refused" belongs to the stop identifier, not to this one.
static inline uint8_t EstopAnnounce_OnFrame(EstopAnnounce_t *a, EstopFollow_t *node, uint32_t now_ms, const uint8_t *data, uint8_t dlc)
{
    uint16_t timeout_ms, seq;
    if (node->latched) a->have_seq = 0u;   // after a stop the master may have restarted and counts again
    if (!EstopAnnounce_Decode(data, dlc, &timeout_ms, &seq)) return 0;
    if (timeout_ms != 0u && (timeout_ms < URTC_ESTOP_ALIVE_TIMEOUT_MIN_MS || timeout_ms > URTC_ESTOP_ALIVE_TIMEOUT_MAX_MS)) return 0;
    if (!EstopFollow_Alive_(node, now_ms)) return 0;   // only while the heartbeat of that master is heard
    if (!EstopFollow_SeqNewer(seq, a->have_seq, a->last_seq)) return 0;
    a->have_seq = 1u;
    a->last_seq = seq;
    (void)EstopFollow_SetMasterTimeout(node, timeout_ms);   // 0 gives the head its own value back
    return 1;
}

#endif // FW_ESTOP_ANNOUNCE_H
