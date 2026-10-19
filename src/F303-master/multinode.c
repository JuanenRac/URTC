// =============================================================================
// URTC Firmware - Several tool heads on one CAN bus (pure logic, no STM32 HAL): multinode.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// See multinode.h. Time is a free-running millisecond counter; every comparison is
// wrap-safe. Nothing here touches hardware: the glue (firmware_multinode.c) does.
#include <string.h>
#include "multinode.h"

static int32_t since(uint32_t now, uint32_t then) { return (int32_t)(now - then); }

// ---- the persistent switch ----------------------------------------------------------
uint8_t Mn_ConfigChecksum(const MnConfig_t *c) {
    const uint8_t *p = (const uint8_t *)c;
    uint8_t crc = 0;
    for (unsigned i = 0; i < sizeof(*c) - 1; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

void Mn_ConfigSeal(MnConfig_t *c) { c->checksum = Mn_ConfigChecksum(c); }

void Mn_ConfigBlank(MnConfig_t *c) {
    memset(c, 0, sizeof(*c));
    c->magic = MN_CONFIG_MAGIC;
    c->struct_version = MN_CONFIG_VERSION;
    Mn_ConfigSeal(c);
}

uint8_t Mn_ConfigIsValid(const MnConfig_t *c) {
    return c->magic == MN_CONFIG_MAGIC && c->struct_version == MN_CONFIG_VERSION &&
           c->checksum == Mn_ConfigChecksum(c);
}

uint8_t Mn_ConfigIsEnabled(const MnConfig_t *c) {
    return (uint8_t)(Mn_ConfigIsValid(c) && c->enabled == 1);
}

// ---- the node -----------------------------------------------------------------------
void Mn_Init(MnNode_t *n, uint8_t enabled, uint8_t address, uint8_t tool, uint8_t instance,
             uint32_t serial, uint32_t now_ms) {
    memset(n, 0, sizeof(*n));
    n->enabled = enabled ? 1 : 0;
    n->address = (uint8_t)(address & MN_ADDRESS_MASK);
    n->tool = tool;
    n->instance = instance;
    n->serial = serial;
    n->phase = MN_PHASE_LISTEN;
    n->boot_ms = now_ms;
    n->info_pending = 1;
    n->info_due_ms = now_ms + (uint32_t)n->address * MN_DISCOVER_SLOT_MS;
    n->status_due_ms = now_ms + MN_STATUS_PERIOD_MS;
}

// Time-driven transitions: the end of the listening window and the lapse of a selection.
static void advance(MnNode_t *n, uint32_t now) {
    if (n->phase == MN_PHASE_LISTEN && since(now, n->boot_ms) >= (int32_t)MN_LEGACY_LISTEN_MS) {
        n->phase = n->conflict ? MN_PHASE_UNSELECTED : MN_PHASE_LEGACY;
    }
    if (n->phase == MN_PHASE_SELECTED && since(now, n->select_deadline_ms) >= 0) {
        n->phase = MN_PHASE_UNSELECTED; // a host that stopped repeating SELECT leaves no node selected
        n->status_now = 1;
    }
}

uint8_t Mn_MaySingleHead(const MnNode_t *n) {
    return (uint8_t)(!n->conflict && (n->phase == MN_PHASE_SELECTED || n->phase == MN_PHASE_LEGACY));
}

uint8_t Mn_StateFlags(const MnNode_t *n, uint8_t fault) {
    uint8_t s = 0;
    if (Mn_MaySingleHead(n)) s |= MN_STATE_SELECTED;
    if (fault) s |= MN_STATE_FAULT;
    if (n->conflict) s |= MN_STATE_CONFLICT;
    return s;
}

static void leave_selection(MnNode_t *n) {
    n->phase = MN_PHASE_UNSELECTED;
    n->status_now = 1;
}

static void on_select(MnNode_t *n, uint8_t dlc, const uint8_t *d, uint32_t now) {
    if (dlc != 3 || d[0] > MN_SELECT_ACTION_SELECT || d[1] > MN_ADDRESS_MASK) return; // malformed: no change
    n->seq = d[2];
    if (d[0] == MN_SELECT_ACTION_SELECT && d[1] == n->address) {
        if (n->conflict) return; // a node in conflict is never selected
        n->phase = MN_PHASE_SELECTED;
        n->select_deadline_ms = now + MN_SELECT_TIMEOUT_MS;
        n->status_now = 1;
    } else {
        leave_selection(n); // deselect all, or another node was selected
    }
}

MnVerdict_t Mn_OnRx(MnNode_t *n, uint32_t id, uint8_t dlc, const uint8_t *d, uint32_t now, MnEffects_t *fx) {
    if (fx) memset(fx, 0, sizeof(*fx));
    advance(n, now);

    if (id < MN_ID_FIRST || id > MN_ID_LAST) {
        return Mn_MaySingleHead(n) ? MN_PASS : MN_CONSUME; // single-head frames: only the selected node
    }

    // Another board announcing itself: its address is ours (a conflict) or not (we are not alone).
    if ((id >= MN_ID_NODE_INFO && id < MN_ID_NODE_INFO + MN_NODE_MAX) ||
        (id >= MN_ID_NODE_STATUS && id < MN_ID_NODE_STATUS + MN_NODE_MAX)) {
        uint8_t from = (uint8_t)(id & MN_ADDRESS_MASK);
        if (from == n->address) {
            if (!n->conflict) {
                n->conflict = 1;
                n->phase = MN_PHASE_UNSELECTED;
                n->status_now = 1;
                if (fx) fx->safe_stop = 1; // a board that cannot be told apart must not keep driving its tool
            }
        } else if (n->phase == MN_PHASE_LISTEN) {
            n->phase = MN_PHASE_UNSELECTED; // someone else is here: wait to be selected
            n->status_now = 1;
        }
        return MN_CONSUME;
    }

    if (id == MN_ID_DISCOVER) {
        if (dlc == 2 && d[0] >= MN_VERSION && d[1] >= 1 && d[1] <= 50) {
            n->info_pending = 1;
            n->info_due_ms = now + (uint32_t)n->address * d[1];
        }
    } else if (id == MN_ID_SELECT) {
        on_select(n, dlc, d, now);
    } else if (id == MN_ID_SAFE_STOP) {
        if (dlc == 1) {
            n->stop_reason = d[0];
            if (fx) fx->safe_stop = 1; // only ever toward safety, whatever the phase
        }
    } else if (id == MN_ID_NODE_CMD + n->address) {
        if (!n->conflict && dlc >= 2 && dlc <= 8) {
            uint16_t func = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
            if (func == MN_CMD_FUNC_STATUS && fx) { // read-only; nothing else is accepted in this form
                fx->answer_cmd = 1;
                fx->cmd_func = func;
            }
        }
    }
    return MN_CONSUME;
}

uint8_t Mn_Service(MnNode_t *n, uint32_t now, uint8_t fault, uint8_t error, MnFrame_t *out) {
    advance(n, now);
    if (n->info_pending && since(now, n->info_due_ms) >= 0) {
        n->info_pending = 0;
        out->id = MN_ID_NODE_INFO + n->address;
        out->dlc = 8;
        out->data[0] = n->tool;
        out->data[1] = Mn_StateFlags(n, fault);
        out->data[2] = n->instance;
        out->data[3] = (uint8_t)MN_VERSION;
        out->data[4] = (uint8_t)(n->serial >> 24);
        out->data[5] = (uint8_t)(n->serial >> 16);
        out->data[6] = (uint8_t)(n->serial >> 8);
        out->data[7] = (uint8_t)n->serial;
        return 1;
    }
    if (n->status_now || since(now, n->status_due_ms) >= 0) {
        uint32_t up = (uint32_t)since(now, n->boot_ms) / 1000u;
        if (up > 0xFFFFu) up = 0xFFFFu;
        n->status_now = 0;
        n->status_due_ms = now + MN_STATUS_PERIOD_MS;
        out->id = MN_ID_NODE_STATUS + n->address;
        out->dlc = 4;
        out->data[0] = Mn_StateFlags(n, fault);
        out->data[1] = error;
        out->data[2] = (uint8_t)(up >> 8);
        out->data[3] = (uint8_t)up;
        return 1;
    }
    return 0;
}

void Mn_BuildResp(const MnNode_t *n, uint16_t func, const uint8_t *data, uint8_t len, MnFrame_t *out) {
    if (len > MN_CMD_ARGS_MAX) len = (uint8_t)MN_CMD_ARGS_MAX;
    out->id = MN_ID_NODE_RESP + n->address;
    out->dlc = (uint8_t)(2 + len);
    out->data[0] = (uint8_t)(func >> 8);
    out->data[1] = (uint8_t)func;
    if (len) memcpy(&out->data[2], data, len);
}
