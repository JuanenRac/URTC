// =============================================================================
// URTC Firmware - Lifetime thermal counters, F-RAM and CAN side
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// thermal_stats.c holds the logic; this file loads/saves the record at
// THERMAL_STATS_FRAM_ADDR and answers CAN 0x1AA with 0x1AB (docs/CANBUS.TXT).
#include "firmware_common.h"
#include "firmware_thermal_stats.h"
#include "thermal_stats.h"
#include "urtc_can_ids.h"

#define THERMAL_STATS_SAVE_INTERVAL_MS 60000u // heater time is saved at most once a minute

static ThermalStats_t stats;
static ThermalStatsClock_t stats_clock;
static uint8_t stats_persisted = 0; // 1 once the F-RAM answered (loaded or written)
static uint8_t stats_dirty = 0;
static uint32_t stats_last_save_ms = 0;

static void ThermalStats_Save(void) {
    if (FRAM_WriteBytes(THERMAL_STATS_FRAM_ADDR, (const uint8_t *)&stats, sizeof(stats))) {
        stats_persisted = 1;
        stats_dirty = 0;
    }
    stats_last_save_ms = HAL_GetTick();
}

void ThermalStats_LoadFromFram(void) {
    ThermalStats_t loaded;
    stats_last_save_ms = HAL_GetTick();
    if (!FRAM_ReadBytes(THERMAL_STATS_FRAM_ADDR, (uint8_t *)&loaded, sizeof(loaded))) {
        ThermalStats_Blank(&stats); // no F-RAM: count in RAM only, reported as not persisted
        return;
    }
    if (ThermalStats_IsValid(&loaded)) {
        stats = loaded;
        stats_persisted = 1;
    } else {
        ThermalStats_Blank(&stats); // blank chip (first boot with this firmware) - start from zero
        stats_dirty = 1;
    }
}

void ThermalStats_Service(uint8_t may_write) {
    uint8_t heater_on = (HAL_GPIO_ReadPin(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_SET);
    if (ThermalStats_AccumulateHeater(&stats, &stats_clock, HAL_GetTick(), heater_on)) {
        stats_dirty = 1;
    }
    if (may_write && stats_dirty && (HAL_GetTick() - stats_last_save_ms) >= THERMAL_STATS_SAVE_INTERVAL_MS) {
        ThermalStats_Save();
    }
}

void ThermalStats_NoteFault(uint8_t fault_code) {
    ThermalStats_RecordFault(&stats, fault_code);
    ThermalStats_Save(); // a fault is rare and worth keeping even if power is cut right after
}

void ThermalStats_HandleQuery(void) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return;
    CAN_TxHeaderTypeDef txH;
    uint8_t txD[8];
    uint32_t mb;
    ThermalStats_Encode(&stats, stats_persisted, txD);
    txH.StdId = URTC_CAN_ID_THERMAL_STATS_RESP;
    txH.IDE = CAN_ID_STD;
    txH.RTR = CAN_RTR_DATA;
    txH.DLC = 8;
    txH.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
}
