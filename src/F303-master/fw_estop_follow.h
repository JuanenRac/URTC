// =============================================================================
// URTC Firmware - Following the emergency stop frame of the stack: the decisions (pure logic, no hardware)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The frame on the identifier URTC_ESTOP_CAN_ID is defined by the stack that carries it. This is the part a tool head needs: which frames are a stop,
// which one ends it, and when the silence of the master puts a ready head back in its safe state. It is the C twin of urtc_common.estop_follow, and
// tests/test_estop_follow.c replays the frames and the scenarios that URTC-COMMON writes from that reference (tests/vectors/estop_follow_vectors.h).
//
// One rule shapes everything: a stop is never refused. Any frame on the identifier that is not a well-formed release or heartbeat is a stop; only the
// way out is checked. The head starts latched. A trip, a malformed frame, a heartbeat that says tripped and a bus-off latch it at once. A release moves
// it to ready only when it is latched, the master was heard within URTC_ESTOP_ALIVE_TIMEOUT_MS, the epoch is the one of the stop it knows, the sequence is
// newer than the last one seen and, after a bus-off, a new heartbeat was heard. A ready head that hears no new heartbeat for the timeout latches again.
// Time is in milliseconds and may wrap at 32 bits. This is not a certified safety function and never replaces a hardware emergency stop. The glue that
// reads the switch, applies the safe state and lifts the error is in firmware_safety.c; nothing here has run on a board.
//
// Header only, so every suite that links firmware_safety.c has it without another source file.
// =============================================================================
#ifndef FW_ESTOP_FOLLOW_H
#define FW_ESTOP_FOLLOW_H

#include <stdint.h>
#include <string.h>

#include "urtc_can_ids.h"

#define ESTOPF_KIND_TRIP    0x01u
#define ESTOPF_KIND_RELEASE 0x02u
#define ESTOPF_KIND_ALIVE   0x03u

#define ESTOPF_ALIVE_OK      0x00u
#define ESTOPF_ALIVE_TRIPPED 0x01u

#define ESTOPF_SOURCE_BRAIN      0x00u
#define ESTOPF_SOURCE_SLOT_FIRST 0x01u
#define ESTOPF_SOURCE_SLOT_LAST  0x08u
#define ESTOPF_SOURCE_CM5        0x10u

#define ESTOPF_REASON_UNSPECIFIED 0xFFu

#define ESTOPF_STATE_LATCHED 0u
#define ESTOPF_STATE_READY   1u

typedef struct {
    uint8_t kind, detail, source, epoch;
    uint16_t seq;
} EstopFollowFrame_t;

// What the head must do after an event. stop_reason is a URTC_ERROR_REASON_* code, 0 when there is no stop to apply.
typedef struct {
    uint8_t stop_reason;
    uint8_t released;
} EstopFollowEffect_t;

typedef struct {
    uint8_t latched;      // 1 = latched (the safe state is held), 0 = ready
    uint8_t have_epoch;
    uint8_t epoch;
    uint8_t have_seq;
    uint16_t last_seq;
    uint8_t have_alive;
    uint32_t last_alive_ms;
    uint8_t busoff_wait;  // set by a bus-off: a release is refused until a new heartbeat arrives
} EstopFollow_t;

static inline uint8_t EstopFollow_ReasonKnown(uint8_t reason)
{
    return (uint8_t)((reason >= 0x01u && reason <= 0x08u) || reason == ESTOPF_REASON_UNSPECIFIED);
}

// 1 and the frame when the bytes are well formed, 0 when they are not (the head then treats the frame as a stop).
static inline uint8_t EstopFollow_Decode(const uint8_t *data, uint8_t dlc, EstopFollowFrame_t *out)
{
    if (dlc != 8u) return 0;
    const uint8_t kind = data[0], detail = data[1], source = data[2];
    if (data[6] != (uint8_t)(kind ^ 0xFFu) || data[7] != (uint8_t)(detail ^ 0xFFu)) return 0;
    if (kind == ESTOPF_KIND_TRIP) {
        const uint8_t source_ok = (uint8_t)(source == ESTOPF_SOURCE_BRAIN || source == ESTOPF_SOURCE_CM5 ||
                                            (source >= ESTOPF_SOURCE_SLOT_FIRST && source <= ESTOPF_SOURCE_SLOT_LAST));
        if (!EstopFollow_ReasonKnown(detail) || !source_ok) return 0;
    } else if (kind == ESTOPF_KIND_RELEASE) {
        if (detail != 0x00u || source != ESTOPF_SOURCE_BRAIN) return 0;
    } else if (kind == ESTOPF_KIND_ALIVE) {
        if ((detail != ESTOPF_ALIVE_OK && detail != ESTOPF_ALIVE_TRIPPED) || source != ESTOPF_SOURCE_BRAIN) return 0;
    } else {
        return 0;
    }
    out->kind = kind;
    out->detail = detail;
    out->source = source;
    out->epoch = data[3];
    out->seq = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
    return 1;
}

// 1 when candidate is 1..32767 steps ahead of last in the modular order of 16 bits; always 1 when there is no last yet.
static inline uint8_t EstopFollow_SeqNewer(uint16_t candidate, uint8_t have_last, uint16_t last)
{
    if (!have_last) return 1;
    const uint16_t step = (uint16_t)(candidate - last);
    return (uint8_t)(step >= 1u && step <= 0x7FFFu);
}

// A head that has just powered up: latched, nothing heard.
static inline void EstopFollow_Reset(EstopFollow_t *n)
{
    memset(n, 0, sizeof *n);
    n->latched = 1u;
}

static inline uint8_t EstopFollow_State(const EstopFollow_t *n)
{
    return n->latched ? (uint8_t)ESTOPF_STATE_LATCHED : (uint8_t)ESTOPF_STATE_READY;
}

static inline uint8_t EstopFollow_Alive_(const EstopFollow_t *n, uint32_t now_ms)
{
    return (uint8_t)(n->have_alive && (uint32_t)(now_ms - n->last_alive_ms) <= URTC_ESTOP_ALIVE_TIMEOUT_MS);
}

static inline EstopFollowEffect_t EstopFollow_Stop_(EstopFollow_t *n, uint8_t reason)
{
    EstopFollowEffect_t e = {reason, 0u};
    n->latched = 1u;
    return e;
}

// One frame received on the stop identifier (any length): a malformed one is a stop.
static inline EstopFollowEffect_t EstopFollow_OnFrame(EstopFollow_t *n, uint32_t now_ms, const uint8_t *data, uint8_t dlc)
{
    EstopFollowEffect_t none = {0u, 0u};
    EstopFollowFrame_t f;
    if (!EstopFollow_Decode(data, dlc, &f)) return EstopFollow_Stop_(n, URTC_ERROR_REASON_ESTOP);
    if (f.kind == ESTOPF_KIND_TRIP) {
        n->have_epoch = 1u;
        n->epoch = f.epoch;
        return EstopFollow_Stop_(n, URTC_ERROR_REASON_ESTOP);
    }
    const uint8_t newer = EstopFollow_SeqNewer(f.seq, n->have_seq, n->last_seq);
    if (f.kind == ESTOPF_KIND_ALIVE) {
        if (f.detail == ESTOPF_ALIVE_TRIPPED) { // recovers a stop this head never heard
            n->have_epoch = 1u;
            n->epoch = f.epoch;
            if (newer) {
                n->have_seq = 1u;
                n->last_seq = f.seq;
                n->have_alive = 1u;
                n->last_alive_ms = now_ms;
            }
            return EstopFollow_Stop_(n, URTC_ERROR_REASON_ESTOP);
        }
        if (newer) { // a frozen or replayed sender does not keep the head alive
            n->have_seq = 1u;
            n->last_seq = f.seq;
            n->have_alive = 1u;
            n->last_alive_ms = now_ms;
            n->busoff_wait = 0u;
        }
        return none;
    }
    // A release: only from a latched head that heard a live master (and, after a bus-off, a new heartbeat), for the epoch it knows, with a newer sequence.
    if (n->latched && !n->busoff_wait && newer && n->have_epoch && f.epoch == n->epoch && EstopFollow_Alive_(n, now_ms)) {
        n->have_seq = 1u;
        n->last_seq = f.seq;
        n->latched = 0u;
        none.released = 1u;
    }
    return none;
}

// The bus went off: the bus coming back never releases anything, and a release waits for a new heartbeat.
static inline EstopFollowEffect_t EstopFollow_OnBusOff(EstopFollow_t *n)
{
    n->busoff_wait = 1u;
    return EstopFollow_Stop_(n, URTC_ERROR_REASON_ESTOP);
}

// Every pass: a ready head that has not heard the master within the timeout is latched again.
static inline EstopFollowEffect_t EstopFollow_Tick(EstopFollow_t *n, uint32_t now_ms)
{
    EstopFollowEffect_t none = {0u, 0u};
    if (!n->latched && !EstopFollow_Alive_(n, now_ms)) return EstopFollow_Stop_(n, URTC_ERROR_REASON_ESTOP_SILENCE);
    return none;
}

#endif // FW_ESTOP_FOLLOW_H
