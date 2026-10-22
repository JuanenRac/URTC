// =============================================================================
// URTC Firmware - Optional firmware features: glue with the board
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <string.h>
#include "firmware_common.h"
#include "firmware_fram.h"
#include "firmware_features.h"
#include "health_profile.h"
#include "hsi_trim.h"
#include "pid_control.h"

#define HEALTH_PERIOD_MS 1000u
#define PID_FRAM_ADDR 992u
#define PID_MAGIC 0x50494447UL // 'PIDG'
#define PID_VERSION 1u
#define PID_WINDOW_MS 500u        // the heater output is time-proportional over this window
#define PID_AUTOTUNE_MAX_S 900.0f
#define PID_AUTOTUNE_HYST_C 2.0f
#define PID_AUTOTUNE_MIN_TARGET_C 100u
#define PID_GAIN_MAX_X100 60000u

static FeatConfig_t persisted;     // what is in F-RAM now (blank when the chip is absent, blank or damaged)
static uint8_t running_mask;       // read once at boot

// ---- the switches ------------------------------------------------------------------------------------
static void read_persisted(void) {
    FeatConfig_t loaded;
    if (FRAM_ReadBytes(FEAT_CONFIG_FRAM_ADDR, (uint8_t *)&loaded, sizeof(loaded)) && Feat_ConfigIsValid(&loaded)) {
        persisted = loaded;
    } else {
        Feat_ConfigBlank(&persisted); // erased, never written, damaged or no chip: every feature off
    }
}

static void send_frame(uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return;
    CAN_TxHeaderTypeDef txH;
    uint8_t txD[8] = {0};
    uint32_t mb;
    memcpy(txD, data, dlc);
    txH.StdId = id;
    txH.IDE = CAN_ID_STD;
    txH.RTR = CAN_RTR_DATA;
    txH.DLC = dlc;
    txH.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
}

// ---- heater PID (state) ------------------------------------------------------------------------------
static Pid_t pid;
static PidTune_t tune;
static uint16_t gain_kp_x100, gain_ki_x100, gain_kd_x100;
static uint8_t pid_state;          // PID_STATE_* of the 0x1BA answer
static uint8_t last_duty;
static uint32_t pid_last_ms;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  struct_version;
    uint16_t kp_x100, ki_x100, kd_x100;
    uint8_t  checksum;
} PidGains_t; // 12 bytes

static uint8_t pid_checksum(const PidGains_t *g) {
    const uint8_t *p = (const uint8_t *)g;
    uint8_t crc = 0;
    for (unsigned i = 0; i < sizeof(*g) - 1; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static void pid_store(void) {
    PidGains_t g;
    memset(&g, 0, sizeof(g));
    g.magic = PID_MAGIC;
    g.struct_version = PID_VERSION;
    g.kp_x100 = gain_kp_x100;
    g.ki_x100 = gain_ki_x100;
    g.kd_x100 = gain_kd_x100;
    g.checksum = pid_checksum(&g);
    FRAM_WriteBytes(PID_FRAM_ADDR, (const uint8_t *)&g, sizeof(g));
}

static void pid_apply_gains(void) {
    Pid_Init(&pid, gain_kp_x100 / 100.0f, gain_ki_x100 / 100.0f, gain_kd_x100 / 100.0f, 0.0f, 100.0f);
}

static void pid_load(void) {
    // Moderate defaults for a T12 cartridge when nothing valid is stored; the autotune replaces them.
    gain_kp_x100 = 800; gain_ki_x100 = 20; gain_kd_x100 = 150;
    PidGains_t g;
    if (FRAM_ReadBytes(PID_FRAM_ADDR, (uint8_t *)&g, sizeof(g)) && g.magic == PID_MAGIC &&
        g.struct_version == PID_VERSION && g.checksum == pid_checksum(&g) &&
        g.kp_x100 <= PID_GAIN_MAX_X100 && g.ki_x100 <= PID_GAIN_MAX_X100 && g.kd_x100 <= PID_GAIN_MAX_X100) {
        gain_kp_x100 = g.kp_x100; gain_ki_x100 = g.ki_x100; gain_kd_x100 = g.kd_x100;
    }
    pid_apply_gains();
}

// ---- health ---------------------------------------------------------------------------------------
static HsiTrim_t hsi;
static uint8_t bus_off_count, bus_off_seen, loop_max_ms;
static uint32_t health_last_ms, service_last_ms;
static MpPlan_t motion_plan;

__attribute__((weak)) int16_t Features_PlatformMcuTempC(uint16_t *vdda_mv) { *vdda_mv = HEALTH_VDDA_NA; return HEALTH_TEMP_NA; }
__attribute__((weak)) uint8_t Features_PlatformHsiMeasure(uint32_t *expected_ticks, uint32_t *measured_ticks) { (void)expected_ticks; (void)measured_ticks; return 0; }
__attribute__((weak)) void Features_PlatformHsiWriteTrim(uint8_t trim) { (void)trim; }

void Features_Init(void) {
    read_persisted();
    running_mask = Feat_ConfigMask(&persisted);
    pid_state = PID_STATE_OFF_VALUE;
    if (running_mask & FEAT_THERMAL_PID) { pid_load(); pid_state = PID_STATE_RUNNING_VALUE; }
    HsiTrim_Init(&hsi, HSI_TRIM_DEFAULT);
    health_last_ms = service_last_ms = HAL_GetTick();
}

uint8_t Features_Running(void) { return running_mask; }
uint8_t Features_Active(uint8_t feature_bit) { return (uint8_t)((running_mask & feature_bit) != 0); }

void Features_HandleCommand(void) {
    if (rxHeader.StdId == 0x1AE) {
        uint8_t mask;
        if (!(rxHeader.StdId == 0x1AE && rxHeader.DLC == 5 && Feat_ParseCommand((uint8_t)rxHeader.DLC, rxData, &mask))) return; // anything else is ignored, like every magic-protected command
        FeatConfig_t next;
        Feat_ConfigBlank(&next);
        next.mask = mask;
        Feat_ConfigSeal(&next);
        if (FRAM_WriteBytes(FEAT_CONFIG_FRAM_ADDR, (const uint8_t *)&next, sizeof(next))) {
            read_persisted(); // report what the chip really holds, not what was asked
        }
    } else if (rxHeader.StdId != 0x1AF) {
        return;
    }
    uint8_t out[3];
    Feat_EncodeResponse(Feat_ConfigMask(&persisted), running_mask, out);
    send_frame(0x1AF, out, 3);
}

// ---- service: health frame and oscillator trim ------------------------------------------------
void Features_Service(void) {
    if (!(running_mask & (FEAT_HEALTH_PROFILE | FEAT_HSI_AUTOTRIM))) return;
    uint32_t now = HAL_GetTick();
    loop_max_ms = Health_LoopMax(loop_max_ms, now - service_last_ms);
    service_last_ms = now;

    if (running_mask & FEAT_HSI_AUTOTRIM) {
        uint32_t expected, measured;
        if (Features_PlatformHsiMeasure(&expected, &measured) && HsiTrim_Update(&hsi, expected, measured)) {
            Features_PlatformHsiWriteTrim(hsi.trim);
        }
    }
    if (!(running_mask & FEAT_HEALTH_PROFILE)) return;

    uint32_t esr = hcan.Instance->ESR;
    uint8_t bus_off = (uint8_t)((esr >> 2) & 1u);   // BOFF
    if (bus_off && !bus_off_seen) bus_off_count = Health_CountBusOff(bus_off_count);
    bus_off_seen = bus_off;

    if (now - health_last_ms < HEALTH_PERIOD_MS) return;
    health_last_ms = now;
    HealthFrame_t h;
    h.tec = (uint8_t)((esr >> 16) & 0xFF);
    h.rec = (uint8_t)((esr >> 24) & 0xFF);
    h.bus_off_count = bus_off_count;
    h.hsi_trim = hsi.trim;
    h.loop_max_ms = loop_max_ms;
    uint16_t vdda = HEALTH_VDDA_NA;
    h.mcu_temp_c = Features_PlatformMcuTempC(&vdda);
    h.vdda_mv = vdda;
    loop_max_ms = 0;
    uint8_t out[8];
    Health_Encode(&h, out);
    send_frame(0x1B8, out, 8);
}

// ---- stepper ramps ---------------------------------------------------------------------------------------
void Features_NoteMove(uint32_t steps) {
    if (!(running_mask & FEAT_MOTION_RAMPS)) return;
    uint32_t speed;
    uint8_t profile;
    if (!Mp_ParseExtension((uint8_t)rxHeader.DLC, rxData, &speed, &profile)) return;
    if (speed == 0) speed = MP_DEFAULT_CRUISE_SPS;
    if (!Mp_Plan(&motion_plan, steps, speed, MP_DEFAULT_START_SPS, MP_DEFAULT_ACCEL_SPS2, profile)) {
        memset(&motion_plan, 0, sizeof(motion_plan));
    }
}

const MpPlan_t *Features_MotionPlan(void) { return &motion_plan; }

// ---- heater PID ---------------------------------------------------------------------------------------------
static void send_pid_state(void) {
    uint8_t out[8];
    out[0] = (uint8_t)(gain_kp_x100 >> 8); out[1] = (uint8_t)gain_kp_x100;
    out[2] = (uint8_t)(gain_ki_x100 >> 8); out[3] = (uint8_t)gain_ki_x100;
    out[4] = (uint8_t)(gain_kd_x100 >> 8); out[5] = (uint8_t)gain_kd_x100;
    out[6] = (running_mask & FEAT_THERMAL_PID) ? pid_state : PID_STATE_OFF_VALUE;
    out[7] = last_duty;
    send_frame(0x1BA, out, 8);
}

void Features_HandlePidCommand(void) {
    if (rxHeader.StdId == 0x1B9) {
        if (!(rxHeader.StdId == 0x1B9 && rxHeader.DLC == 7) || !(running_mask & FEAT_THERMAL_PID)) { send_pid_state(); return; }
        uint16_t kp = (uint16_t)((rxData[0] << 8) | rxData[1]);
        uint16_t ki = (uint16_t)((rxData[2] << 8) | rxData[3]);
        uint16_t kd = (uint16_t)((rxData[4] << 8) | rxData[5]);
        switch (rxData[6]) {
        case 0: // set the gains
            if (kp <= PID_GAIN_MAX_X100 && ki <= PID_GAIN_MAX_X100 && kd <= PID_GAIN_MAX_X100) {
                gain_kp_x100 = kp; gain_ki_x100 = ki; gain_kd_x100 = kd;
                pid_apply_gains();
                pid_store();
                if (tune.state == TUNE_RUNNING) PidTune_Abort(&tune);
                pid_state = PID_STATE_RUNNING_VALUE;
            }
            break;
        case 1: // start the relay autotune: only on a heater tool, with a sane setpoint and no error
            if ((active_tool == TOOL_SOLDERING_IRON || active_tool == TOOL_HOTAIR_REWORK) && !system_error_flag &&
                target_temperature >= PID_AUTOTUNE_MIN_TARGET_C && target_temperature <= 445) {
                PidTune_Start(&tune, (float)target_temperature, PID_AUTOTUNE_HYST_C, 100.0f, 0.0f, PID_AUTOTUNE_MAX_S);
                pid_state = PID_STATE_AUTOTUNING_VALUE;
            }
            break;
        case 2:
            if (tune.state == TUNE_RUNNING) { PidTune_Abort(&tune); pid_state = PID_STATE_RUNNING_VALUE; }
            break;
        default:
            break;
        }
    } else if (rxHeader.StdId != 0x1BA) {
        return;
    }
    send_pid_state();
}

uint8_t ThermalPid_Heater(uint16_t current_c, uint16_t target_c, uint32_t now_ms) {
    float dt = (now_ms - pid_last_ms) / 1000.0f;
    if (pid_last_ms == 0 || dt <= 0.0f || dt > 0.5f) { dt = 0.02f; Pid_Reset(&pid); }
    pid_last_ms = now_ms ? now_ms : 1;
    float duty;
    if (tune.state == TUNE_RUNNING) {
        if (target_c == 0 || system_error_flag) { PidTune_Abort(&tune); pid_state = PID_STATE_RUNNING_VALUE; duty = 0.0f; }
        else duty = PidTune_Update(&tune, (float)current_c, dt);
        if (tune.state == TUNE_DONE) {
            uint32_t kp = (uint32_t)(tune.kp * 100.0f + 0.5f), ki = (uint32_t)(tune.ki * 100.0f + 0.5f), kd = (uint32_t)(tune.kd * 100.0f + 0.5f);
            if (kp <= PID_GAIN_MAX_X100 && ki <= PID_GAIN_MAX_X100 && kd <= PID_GAIN_MAX_X100) {
                gain_kp_x100 = (uint16_t)kp; gain_ki_x100 = (uint16_t)ki; gain_kd_x100 = (uint16_t)kd;
                pid_apply_gains();
                pid_store();
                pid_state = PID_STATE_DONE_VALUE;
            } else {
                pid_state = PID_STATE_FAILED_VALUE;
            }
            tune.state = TUNE_IDLE;
        } else if (tune.state == TUNE_FAILED) {
            pid_state = PID_STATE_FAILED_VALUE;
            tune.state = TUNE_IDLE;
        }
    } else if (target_c == 0) {
        Pid_Reset(&pid);
        duty = 0.0f;
    } else {
        duty = Pid_Update(&pid, (float)target_c, (float)current_c, dt);
    }
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 100.0f) duty = 100.0f;
    last_duty = (uint8_t)(duty + 0.5f);
    // time-proportional output: on for the first part of every window
    uint32_t on_ms = (uint32_t)(duty * (float)PID_WINDOW_MS / 100.0f + 0.5f);
    return (uint8_t)((now_ms % PID_WINDOW_MS) < on_ms);
}
