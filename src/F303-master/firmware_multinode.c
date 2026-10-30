// =============================================================================
// URTC Firmware - Several tool heads on one CAN bus, board side
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Glue between the pure logic (multinode.c) and the board: the persistent switch in
// F-RAM, the frames on the bus, the safe state. The switch is OFF unless a valid
// record in F-RAM says otherwise; while it is off nothing in this file does anything,
// so the receive path and the transmit path are those of a single head.
//
// Written by the 0x1AC command (four-byte magic, like the other persistent settings; read back
// with 0x1AD) and read once at boot: a change acts after the next power-up, never in the middle
// of a job.
#include <string.h>
#include "firmware_common.h"
#include "firmware_fram.h"
#include "firmware_multinode.h"
#include "multinode.h"
#include "urtc_can_ids.h"


static MnNode_t node;               // zeroed: enabled = 0 until MultiNode_Init() reads a valid record
static MnConfig_t persisted;        // what is in F-RAM now (blank when the chip is absent, blank or damaged)

static void read_persisted(void) {
    MnConfig_t loaded;
    if (FRAM_ReadBytes(MN_CONFIG_FRAM_ADDR, (uint8_t *)&loaded, sizeof(loaded)) && Mn_ConfigIsValid(&loaded)) {
        persisted = loaded;
    } else {
        Mn_ConfigBlank(&persisted); // erased (0xFF), never written, damaged or no chip: off
    }
}

static void send_frame(const MnFrame_t *f) {
    CAN_TxHeaderTypeDef txH;
    uint8_t txD[8];
    uint32_t mb;
    memcpy(txD, f->data, sizeof(txD));
    txH.StdId = f->id;
    txH.IDE = CAN_ID_STD;
    txH.RTR = CAN_RTR_DATA;
    txH.DLC = f->dlc;
    txH.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
}

void MultiNode_Init(void) {
    read_persisted();
    Mn_Init(&node, Mn_ConfigIsEnabled(&persisted), (uint8_t)(raw_id_pin_value & MN_ADDRESS_MASK),
            (uint8_t)active_tool, persisted.instance, MultiNode_PlatformSerial(), HAL_GetTick());
}

uint8_t MultiNode_FilterRx(void) {
    if (!node.enabled) return 0; // switch off: the single-head path, untouched
    if (rxHeader.IDE != CAN_ID_STD) return 1;
    MnEffects_t fx;
    MnVerdict_t verdict = Mn_OnRx(&node, rxHeader.StdId, (uint8_t)rxHeader.DLC, rxData, HAL_GetTick(), &fx);
    if (fx.safe_stop) {
        MultiNode_PlatformSafeState();
    }
    if (fx.answer_cmd && HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
        MnFrame_t resp;
        uint8_t body[6] = {
            (uint8_t)active_tool,
            system_error_flag ? 0x01 : 0x00,
            can_bus_error_flag ? 0x01 : 0x00,
            boot_sequence_active ? 0x01 : 0x00,
            thermal_fault_code,
            last_reset_cause
        }; // the same six bytes as the single-head answer 0x111
        Mn_BuildResp(&node, fx.cmd_func, body, sizeof(body), &resp);
        send_frame(&resp);
    }
    return verdict == MN_CONSUME;
}

void MultiNode_Service(void) {
    if (!node.enabled) return;
    for (int i = 0; i < 4; i++) {
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return;
        MnFrame_t f;
        uint8_t fault = system_error_flag ? 1 : 0;
        uint8_t error = thermal_fault_code ? thermal_fault_code : (uint8_t)(fault ? 0xFF : 0);
        __disable_irq();
        uint8_t due = Mn_Service(&node, HAL_GetTick(), fault, error, &f);
        __enable_irq();
        if (!due) return;
        send_frame(&f);
    }
}

uint8_t MultiNode_SingleHeadTxAllowed(void) {
    if (!node.enabled) return 1;
    return Mn_MaySingleHead(&node);
}

void MultiNode_HandleConfigCommand(void) {
    if (rxHeader.StdId == URTC_CAN_ID_SET_MULTINODE) {
        // Four-byte magic (ASCII 'MNOD'), then enable (0 or 1) and the instance number.
        if (!(rxHeader.StdId == URTC_CAN_ID_SET_MULTINODE && rxHeader.DLC == 6 &&
              rxData[0] == 0x4D && rxData[1] == 0x4E && rxData[2] == 0x4F && rxData[3] == 0x44 &&
              rxData[4] <= 1)) {
            return; // anything else is ignored, like every magic-protected command
        }
        MnConfig_t next;
        Mn_ConfigBlank(&next);
        next.enabled = rxData[4];
        next.instance = rxData[5];
        Mn_ConfigSeal(&next);
        if (FRAM_WriteBytes(MN_CONFIG_FRAM_ADDR, (const uint8_t *)&next, sizeof(next))) {
            read_persisted(); // report what the chip really holds, not what was asked
        }
    } else if (rxHeader.StdId != URTC_CAN_ID_MULTINODE_CONFIG_RESP) {
        return;
    }
    // 0x1AC (after a change) and 0x1AD (a plain query) are both answered with 0x1AD.
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
        MnFrame_t f;
        f.id = URTC_CAN_ID_MULTINODE_CONFIG_RESP;
        f.dlc = 4;
        memset(f.data, 0, sizeof(f.data));
        f.data[0] = Mn_ConfigIsEnabled(&persisted);
        f.data[1] = node.enabled;
        f.data[2] = persisted.instance;
        f.data[3] = (uint8_t)(raw_id_pin_value & MN_ADDRESS_MASK);
        send_frame(&f);
    }
}
