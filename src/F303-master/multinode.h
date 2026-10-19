// =============================================================================
// URTC Firmware - Several tool heads on one CAN bus (pure logic, no STM32 HAL): multinode.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The node side of the multinode extension designed in URTC-COMMON/docs/MULTINODE.md,
// kept free of the HAL so tests/test_multinode.c drives it on the host.
//
// The extension is OFF by default. While it is off, none of this code runs and the
// receive path is the single-head path of every earlier release (see
// firmware_multinode.c). When it is on, the node
//   * uses its five identity jumpers as the node address (0 to 31),
//   * answers DISCOVER with NODE_INFO and sends NODE_STATUS every second,
//   * obeys the single-head identifiers only while it is selected (or in legacy mode,
//     see below), so a node that is not selected never actuates,
//   * refuses commands when another board uses its address (CONFLICT),
//   * enters its safe state on SAFE_STOP.
//
// Implemented in firmware and tested on the host only; it has not been tried on boards.
#ifndef MULTINODE_H
#define MULTINODE_H

#include <stdint.h>

// ---- values shared with URTC-COMMON/spec/can_protocol.json (a test compares them) ----
#define MN_VERSION               1u
#define MN_ADDRESS_BITS          5u
#define MN_ADDRESS_MASK          31u
#define MN_NODE_MAX              32u
#define MN_STATUS_PERIOD_MS      1000u
#define MN_DISCOVER_SLOT_MS      2u
#define MN_NODE_TIMEOUT_MS       3500u
#define MN_SELECT_TIMEOUT_MS     2000u
#define MN_LEGACY_LISTEN_MS      1500u
#define MN_CMD_ARGS_MAX          6u

#define MN_ID_DISCOVER           0x400u
#define MN_ID_SELECT             0x401u
#define MN_ID_SAFE_STOP          0x402u
#define MN_ID_NODE_INFO          0x420u // + node address
#define MN_ID_NODE_STATUS        0x440u // + node address
#define MN_ID_NODE_CMD           0x460u // + node address
#define MN_ID_NODE_RESP          0x480u // + node address
#define MN_ID_FIRST              0x400u
#define MN_ID_LAST               0x49Fu

#define MN_STATE_SELECTED        0x01u
#define MN_STATE_FAULT           0x02u
#define MN_STATE_CONFLICT        0x04u
#define MN_STATE_BOOTLOADER      0x08u

#define MN_SELECT_ACTION_DESELECT_ALL 0u
#define MN_SELECT_ACTION_SELECT       1u

// The single-head command a node answers with NODE_RESP (read-only status query).
#define MN_CMD_FUNC_STATUS       0x110u

// ---- the persistent switch ----------------------------------------------------------
// Own F-RAM record (not part of SavedState_t, whose layout stays as it was). A blank,
// erased, short or corrupted record reads as "off".
#define MN_CONFIG_FRAM_ADDR      960u
#define MN_CONFIG_MAGIC          0x4D4E4F44UL // 'MNOD'
#define MN_CONFIG_VERSION        1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  struct_version;
    uint8_t  enabled;        // 1 = on; any other value reads as off
    uint8_t  instance;       // announced in NODE_INFO, for a second head of one tool type
    uint8_t  checksum;       // CRC-8/SMBUS over every byte above
} MnConfig_t; // 8 bytes

void    Mn_ConfigBlank(MnConfig_t *c);                       // off, instance 0, sealed
uint8_t Mn_ConfigChecksum(const MnConfig_t *c);
void    Mn_ConfigSeal(MnConfig_t *c);
uint8_t Mn_ConfigIsValid(const MnConfig_t *c);
// 1 only for a valid record with enabled == 1; everything else, including 0xFF-filled F-RAM, is 0.
uint8_t Mn_ConfigIsEnabled(const MnConfig_t *c);

// ---- the node -----------------------------------------------------------------------
typedef enum {
    MN_PHASE_LISTEN = 0,   // first MN_LEGACY_LISTEN_MS after start: listening for other nodes
    MN_PHASE_UNSELECTED,   // ignores every single-head identifier
    MN_PHASE_SELECTED,     // selected by SELECT; lapses after MN_SELECT_TIMEOUT_MS
    MN_PHASE_LEGACY        // heard nobody: behaves as the one head on the bus (implicitly selected)
} MnPhase_t;

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} MnFrame_t;

typedef struct {
    uint8_t  enabled;
    uint8_t  address;
    uint8_t  tool;
    uint8_t  instance;
    uint32_t serial;
    MnPhase_t phase;
    uint8_t  conflict;       // latched until the next start
    uint8_t  seq;            // sequence number of the last SELECT that was accepted
    uint8_t  stop_reason;    // reason of the last SAFE_STOP
    uint8_t  info_pending;
    uint8_t  status_now;     // send a status frame as soon as possible (state just changed)
    uint32_t boot_ms;
    uint32_t select_deadline_ms;
    uint32_t info_due_ms;
    uint32_t status_due_ms;
} MnNode_t;

// What the glue has to do after a received frame.
typedef struct {
    uint8_t  safe_stop;      // put the tool in its safe state
    uint8_t  answer_cmd;     // answer a NODE_CMD (cmd_func) with NODE_RESP
    uint16_t cmd_func;
} MnEffects_t;

typedef enum {
    MN_PASS = 0,             // hand the frame to the single-head dispatcher
    MN_CONSUME               // the extension took it, or the node ignores it: nothing else may act on it
} MnVerdict_t;

void Mn_Init(MnNode_t *n, uint8_t enabled, uint8_t address, uint8_t tool, uint8_t instance,
             uint32_t serial, uint32_t now_ms);

// Every received frame goes through here first (only while the extension is enabled).
MnVerdict_t Mn_OnRx(MnNode_t *n, uint32_t id, uint8_t dlc, const uint8_t *data, uint32_t now_ms, MnEffects_t *fx);

// Next frame due, if any (call until it returns 0). fault/error come from the board.
uint8_t Mn_Service(MnNode_t *n, uint32_t now_ms, uint8_t fault, uint8_t error, MnFrame_t *out);

// 1 when single-head frames may be obeyed and single-head telemetry sent.
uint8_t Mn_MaySingleHead(const MnNode_t *n);
uint8_t Mn_StateFlags(const MnNode_t *n, uint8_t fault);

// Builds a NODE_RESP frame: func (big-endian) followed by up to MN_CMD_ARGS_MAX data bytes.
void Mn_BuildResp(const MnNode_t *n, uint16_t func, const uint8_t *data, uint8_t len, MnFrame_t *out);

#endif // MULTINODE_H
