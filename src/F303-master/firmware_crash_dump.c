// =============================================================================
// URTC Firmware - crash capture: start-up check and the CAN readback (0x1BC asks, 0x1BD answers)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_crash_dump.h"
#include "urtc_can_ids.h"

#if URTC_CRASH_DUMP

void CrashDump_AppBoot(uint8_t reset_cause) {
    CrashDump_Boot(&crash_dump_store, reset_cause);
}

void CrashDump_HandleQuery(uint8_t operation) {
    if (operation != 0) return;
    uint8_t bytes[CRASH_DUMP_BYTES];
    // The record is believed only when it agrees with its checksum; if it does not, what goes out is an empty record, never half-written registers.
    if (CrashDump_IsValid(&crash_dump_store)) {
        CrashDump_Serialize(&crash_dump_store, bytes);
    } else {
        CrashDumpRecord_t empty;
        CrashDump_Clear(&empty);
        CrashDump_Serialize(&empty, bytes);
    }
    for (uint8_t frame = 0; frame < CRASH_DUMP_FRAMES; frame++) {
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return; // stop rather than send the rest out of order: a host that counts fewer than six frames knows the answer is incomplete
        CAN_TxHeaderTypeDef txH;
        txH.StdId = URTC_CAN_ID_CRASH_DUMP_RESP;
        txH.IDE = CAN_ID_STD;
        txH.RTR = CAN_RTR_DATA;
        txH.DLC = 8;
        txH.TransmitGlobalTime = DISABLE;
        uint32_t mb;
        HAL_CAN_AddTxMessage(&hcan, &txH, &bytes[frame * 8u], &mb);
    }
}

#endif // URTC_CRASH_DUMP
