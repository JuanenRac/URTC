// =============================================================================
// URTC Firmware - Host logic tests for following the emergency stop frame of the stack (fw_estop_follow.h and the glue in firmware_safety.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Part one replays the frames and the scenarios that URTC-COMMON wrote from its Python reference (urtc_common.estop_follow) against the C twin. Part two
// compiles the glue against the fake HAL with the real switches: with ESTOP_FOLLOW off nothing changes whatever arrives on the identifier; with it on the
// head powers up latched with the critical error, a stop puts the outputs in the safe state, a release lifts only an error that came from a stop, the
// silence of the master and a bus-off latch it again, and the clear-error command never lifts it. The head and the bus are simulated: nothing here has
// run on a board.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_features.h"
#include "firmware_fram.h"
#include "firmware_safety.h"
#include "fw_estop_follow.h"
#include "urtc_can_ids.h"
#include "vectors/estop_follow_vectors.h"

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
I2C_HandleTypeDef hi2c2;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag, system_error_reason;
volatile uint8_t thermal_fault_code;
volatile uint16_t current_temperature, target_temperature;
volatile uint8_t last_reset_cause = 0x08;
volatile uint8_t laser_power_setpoint, drill_speed, layer_fan_duty, uv_curing_duty, hotair_blower_duty;
volatile uint32_t steps_remaining;

static int guards_reset;
static int safe_state_calls;
void Control_ThermalGuardsReset(void) { guards_reset++; }
void MultiNode_PlatformSafeState(void) { safe_state_calls++; }

// ---- part one: the decisions against the vectors of URTC-COMMON ---------------------------------------------------------------------------
static void test_the_frames(int *failures)
{
    int bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_FRAME_CASES); i++) {
        const estop_frame_case_t *c = &ESTOP_FRAME_CASES[i];
        EstopFollowFrame_t f = {0};
        if (!EstopFollow_Decode(c->data, c->dlc, &f) || f.kind != c->kind || f.detail != c->detail || f.source != c->source || f.epoch != c->epoch || f.seq != c->seq) bad++;
    }
    TEST_ASSERT(bad == 0, "every well-formed frame of URTC-COMMON decodes to the same fields");
    bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_MALFORMED_CASES); i++) {
        const estop_malformed_case_t *c = &ESTOP_MALFORMED_CASES[i];
        EstopFollowFrame_t f = {0};
        if (EstopFollow_Decode(c->data, c->dlc, &f)) bad++;
    }
    TEST_ASSERT(bad == 0, "every malformed frame of URTC-COMMON is rejected");
    bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_SEQ_CASES); i++) {
        const estop_seq_case_t *c = &ESTOP_SEQ_CASES[i];
        if (EstopFollow_SeqNewer(c->candidate, c->have_last, c->last) != c->newer) bad++;
    }
    TEST_ASSERT(bad == 0, "the modular order of the sequence is the same");
}

static void test_the_scenarios(int *failures)
{
    int bad = 0, scenario = -1;
    EstopFollow_t n;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_STEPS); i++) {
        const estop_step_t *s = &ESTOP_STEPS[i];
        if (s->scenario != scenario) {
            scenario = s->scenario;
            EstopFollow_Reset(&n);
            EstopFollow_SetTimeout(&n, ESTOP_SCENARIO_CFG[scenario].own);
            if (ESTOP_SCENARIO_CFG[scenario].master) EstopFollow_SetMasterTimeout(&n, ESTOP_SCENARIO_CFG[scenario].master);
        }
        EstopFollowEffect_t e;
        if (s->kind == 0) e = EstopFollow_OnFrame(&n, s->t, s->data, s->dlc);
        else if (s->kind == 1) e = EstopFollow_Tick(&n, s->t);
        else e = EstopFollow_OnBusOff(&n);
        if (EstopFollow_State(&n) != s->state_after || e.stop_reason != s->stop_reason || e.released != s->released) bad++;
        if (s->expect_state != 255 && EstopFollow_State(&n) != s->expect_state) bad++;
    }
    TEST_ASSERT(bad == 0, "every step of every scenario of URTC-COMMON ends in the same state with the same effect");
    TEST_ASSERT(scenario + 1 == ESTOP_SCENARIO_COUNT, "all the scenarios were run");
}

static void test_the_timeout_tables(int *failures)
{
    int bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_TIMEOUT_CONFIG_CASES); i++) {
        const estop_timeout_config_case_t *c = &ESTOP_TIMEOUT_CONFIG_CASES[i];
        uint8_t status = 255;
        if (EstopFollow_ResolveTimeout(c->requested, &status) != c->ms || status != c->status) bad++;
    }
    TEST_ASSERT(bad == 0, "every configured value of URTC-COMMON resolves to the same milliseconds and status");
    bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_TIMEOUT_MASTER_CASES); i++) {
        const estop_timeout_master_case_t *c = &ESTOP_TIMEOUT_MASTER_CASES[i];
        uint8_t status = 255;
        if (EstopFollow_EffectiveTimeout(c->own, c->master, &status) != c->ms || status != c->status) bad++;
    }
    TEST_ASSERT(bad == 0, "every value announced by the master gives the same effective timeout and status");
    TEST_ASSERT(ESTOP_TIMEOUT_MIN_MS == URTC_ESTOP_ALIVE_TIMEOUT_MIN_MS && ESTOP_TIMEOUT_MAX_MS == URTC_ESTOP_ALIVE_TIMEOUT_MAX_MS &&
                URTC_ESTOP_ALIVE_TIMEOUT_MIN_MS == 40u && URTC_ESTOP_ALIVE_TIMEOUT_MAX_MS == 500u, "the limits are the ones of the specification");

    // Every one of the 65536 values: inside the limits it is taken, 0 is the default, anything else is rejected with the default; and the master
    // can only lower the timeout, never raise it.
    int wrong = 0;
    for (uint32_t v = 0; v <= 0xFFFFu; v++) {
        uint8_t status;
        const uint16_t ms = EstopFollow_ResolveTimeout((uint16_t)v, &status);
        if (v == 0u) { if (ms != 100u || status != ESTOPF_TIMEOUT_OK) wrong++; }
        else if (v >= 40u && v <= 500u) { if (ms != v || status != ESTOPF_TIMEOUT_OK) wrong++; }
        else if (ms != 100u || status != ESTOPF_TIMEOUT_REJECTED) wrong++;
        uint8_t mstatus;
        if (EstopFollow_EffectiveTimeout(100u, (uint16_t)v, &mstatus) > 100u) wrong++;
        if (EstopFollow_EffectiveTimeout(40u, (uint16_t)v, &mstatus) > 40u) wrong++;
        if (EstopFollow_EffectiveTimeout(500u, (uint16_t)v, &mstatus) > 500u) wrong++;
    }
    TEST_ASSERT(wrong == 0, "for all 65536 values: taken inside 40 to 500, default for 0, rejected outside, and the master never makes the head more tolerant");

    EstopFollow_t n;
    EstopFollow_Reset(&n);
    TEST_ASSERT(n.timeout_ms == 100u && n.timeout_status == ESTOPF_TIMEOUT_OK, "a head nobody configured keeps 100 ms");
    TEST_ASSERT(EstopFollow_SetTimeout(&n, 7u) == ESTOPF_TIMEOUT_REJECTED && n.timeout_ms == 100u, "a rejected value leaves the default in use");
    TEST_ASSERT(EstopFollow_SetMasterTimeout(&n, 100u) == ESTOPF_TIMEOUT_REJECTED, "a rejected value stays reported when the master agrees");
    TEST_ASSERT(EstopFollow_SetMasterTimeout(&n, 40u) == ESTOPF_TIMEOUT_MASTER_DIFFERS && n.timeout_ms == 40u, "a lower value of the master is obeyed");
}

static void test_the_constants(int *failures)
{
    TEST_ASSERT(URTC_ESTOP_CAN_ID == 1u && URTC_ESTOP_ALIVE_TIMEOUT_MS == 100u && URTC_ERROR_REASON_ESTOP == 9u && URTC_ERROR_REASON_ESTOP_SILENCE == 10u,
                "the identifier, the timeout and the two reasons are the ones of the specification");
    EstopFollow_t n;
    EstopFollow_Reset(&n);
    TEST_ASSERT(EstopFollow_State(&n) == ESTOPF_STATE_LATCHED && EstopFollow_Tick(&n, 1000000).stop_reason == 0, "a head that has just powered up is latched and a silent bus changes nothing");
}

static void test_a_stop_is_never_refused(int *failures)
{
    // Every one of the 256 values of the first byte, with a wrong complement or with a right one, and every length from 0 to 9: a ready head is latched
    // by everything that is not a well-formed release or heartbeat.
    int refused = 0;
    for (unsigned dlc = 0; dlc <= 9; dlc++) {
        for (unsigned first = 0; first < 256; first++) {
            const uint8_t detail = (uint8_t)(dlc & 1u);
            uint8_t d[9] = { (uint8_t)first, detail, 0x00, 0x03, 0x05, 0x00, (uint8_t)(first ^ 0xFFu), (uint8_t)(detail ^ 0xFFu), 0x00 };
            EstopFollow_t n;
            EstopFollow_Reset(&n);
            const uint8_t alive_tripped[8] = { 3, 1, 0, 3, 1, 0, 0xFC, 0xFE };
            const uint8_t release[8] = { 2, 0, 0, 3, 2, 0, 0xFD, 0xFF };
            EstopFollow_OnFrame(&n, 0, alive_tripped, 8);
            EstopFollow_OnFrame(&n, 1, release, 8);
            if (EstopFollow_State(&n) != ESTOPF_STATE_READY) { refused++; continue; }
            EstopFollowFrame_t f = {0};
            const uint8_t well_formed = EstopFollow_Decode(d, (uint8_t)dlc, &f);
            const uint8_t harmless = (uint8_t)(well_formed && (f.kind == ESTOPF_KIND_RELEASE || (f.kind == ESTOPF_KIND_ALIVE && f.detail == ESTOPF_ALIVE_OK)));
            EstopFollowEffect_t e = EstopFollow_OnFrame(&n, 2, d, (uint8_t)dlc);
            if (!harmless && (e.stop_reason != URTC_ERROR_REASON_ESTOP || EstopFollow_State(&n) != ESTOPF_STATE_LATCHED)) refused++;
        }
    }
    TEST_ASSERT(refused == 0, "no frame that is not a well-formed release or heartbeat leaves a ready head ready");
}

// ---- part two: the glue with the fake HAL -------------------------------------------------------------------------------------------------
static void write_mask(uint8_t mask)
{
    FeatConfig_t c;
    Feat_ConfigBlank(&c);
    c.mask = mask;
    Feat_ConfigSeal(&c);
    memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &c, sizeof c);
}

// A power-up with this mask in the F-RAM: the switches are read once, then the head initialises its stop following.
static void boot_with(uint8_t mask)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_fail_reads = fake_fram_fail_writes = 0;
    write_mask(mask);
    fake_tick = 100000;
    fake_tick_step = 0;
    active_tool = TOOL_SOLDERING_IRON;
    system_error_flag = 0;
    system_error_reason = 0;
    thermal_fault_code = 0;
    current_temperature = 25;
    target_temperature = 0;
    laser_power_setpoint = drill_speed = layer_fan_duty = uv_curing_duty = hotair_blower_duty = 0;
    steps_remaining = 0;
    guards_reset = 0;
    safe_state_calls = 0;
    can_regs.ESR = 0;
    Features_Init();
    Safety_EstopInit();
}

// A frame as the receive interrupt would hand it to the glue; returns what the glue answered (1 = taken).
static uint8_t deliver(uint32_t now_ms, const uint8_t *data, uint32_t dlc)
{
    fake_tick = now_ms;
    memset(rxData, 0, sizeof rxData);
    if (data) memcpy(rxData, data, dlc < 8 ? dlc : 8);
    rxHeader.StdId = URTC_ESTOP_CAN_ID;
    rxHeader.DLC = dlc;
    return Safety_HandleEstopFrame();
}

static uint8_t frame_kind(uint32_t now_ms, uint8_t kind, uint8_t detail, uint8_t source, uint8_t epoch, uint16_t seq)
{
    const uint8_t d[8] = { kind, detail, source, epoch, (uint8_t)(seq & 0xFFu), (uint8_t)(seq >> 8), (uint8_t)(kind ^ 0xFFu), (uint8_t)(detail ^ 0xFFu) };
    return deliver(now_ms, d, 8);
}

static void make_ready(uint32_t t)
{
    frame_kind(t, ESTOPF_KIND_ALIVE, ESTOPF_ALIVE_TRIPPED, 0, 3, 1);
    frame_kind(t + 5, ESTOPF_KIND_RELEASE, 0, 0, 3, 2);
}

static void test_with_the_switch_off_nothing_changes(int *failures)
{
    boot_with(0);
    TEST_ASSERT(system_error_flag == 0 && safe_state_calls == 0, "with the switch off the head does not power up latched");
    TEST_ASSERT(frame_kind(100000, ESTOPF_KIND_TRIP, 1, 0, 1, 1) == 0, "the glue says a frame on the identifier is not taken");
    TEST_ASSERT(deliver(100001, NULL, 0) == 0, "neither is an empty one");
    fake_tick = 100000000;
    can_regs.ESR = 4;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0 && safe_state_calls == 0 && system_error_reason == 0, "no stop, no silence and no bus-off acts");
    boot_with(FEAT_COMMS_WATCHDOG | FEAT_CLEAR_ERROR | FEAT_MAGNET_RELEASE | FEAT_THERMAL_PID);
    TEST_ASSERT(system_error_flag == 0 && frame_kind(100000, ESTOPF_KIND_TRIP, 1, 0, 1, 1) == 0, "the other switches do not turn it on");
}

static void test_the_head_powers_up_latched(int *failures)
{
    boot_with(FEAT_ESTOP_FOLLOW);
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP && safe_state_calls == 1, "the head powers up with the critical error and in its safe state");
    fake_tick = 100000000;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP, "a silent bus never readies it");
}

static void test_a_trip_a_release_and_the_silence(int *failures)
{
    boot_with(FEAT_ESTOP_FOLLOW);
    TEST_ASSERT(frame_kind(100000, ESTOPF_KIND_ALIVE, ESTOPF_ALIVE_TRIPPED, 0, 3, 1) == 1 && system_error_flag == 1, "a heartbeat that says tripped is taken and the head stays latched");
    TEST_ASSERT(frame_kind(100005, ESTOPF_KIND_RELEASE, 0, 0, 3, 2) == 1, "the release is taken");
    TEST_ASSERT(system_error_flag == 0 && Safety_ErrorReason() == URTC_ERROR_REASON_NONE && guards_reset == 1, "the release lifts the error of the stop and the guards start afresh");

    safe_state_calls = 0;
    frame_kind(100010, ESTOPF_KIND_TRIP, 6, 3, 3, 3);
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP && safe_state_calls == 1, "a trip puts the outputs in the safe state and latches the error");
    frame_kind(100015, ESTOPF_KIND_TRIP, 6, 3, 3, 4);
    TEST_ASSERT(safe_state_calls == 2 && system_error_flag == 1, "a repeated trip holds the safe state again");

    frame_kind(100100, ESTOPF_KIND_ALIVE, ESTOPF_ALIVE_OK, 0, 0, 5);
    frame_kind(100110, ESTOPF_KIND_RELEASE, 0, 0, 2, 6);
    TEST_ASSERT(system_error_flag == 1, "a release for the old epoch does not end the new stop");
    frame_kind(100200, ESTOPF_KIND_ALIVE, ESTOPF_ALIVE_TRIPPED, 0, 4, 10);
    frame_kind(100205, ESTOPF_KIND_RELEASE, 0, 0, 4, 11);
    TEST_ASSERT(system_error_flag == 0, "a release for the epoch of the stop ends it");

    fake_tick = 100200 + 100;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "exactly at the timeout the head is still ready");
    fake_tick = 100200 + 101;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP_SILENCE, "one millisecond later the silence of the master latches it with its own reason");
}

static void test_a_release_never_lifts_another_error(int *failures)
{
    boot_with(FEAT_ESTOP_FOLLOW);
    frame_kind(100000, ESTOPF_KIND_ALIVE, ESTOPF_ALIVE_TRIPPED, 0, 3, 1);
    system_error_flag = 0;
    system_error_reason = 0;
    RAISE_SYSTEM_ERROR(URTC_ERROR_REASON_THERMAL);
    frame_kind(100005, ESTOPF_KIND_RELEASE, 0, 0, 3, 2);
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_THERMAL && guards_reset == 0, "an error that did not come from the stop stays");

    boot_with(FEAT_ESTOP_FOLLOW);
    RAISE_SYSTEM_ERROR(URTC_ERROR_REASON_ESTOP);
    system_error_reason = URTC_ERROR_REASON_TOOL_ID;
    frame_kind(100000, ESTOPF_KIND_ALIVE, ESTOPF_ALIVE_TRIPPED, 0, 3, 1);
    frame_kind(100005, ESTOPF_KIND_RELEASE, 0, 0, 3, 2);
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_TOOL_ID, "the error for lack of a valid tool is never lifted by a release");
}

static void test_a_bus_off(int *failures)
{
    boot_with(FEAT_ESTOP_FOLLOW);
    make_ready(100000);
    TEST_ASSERT(system_error_flag == 0, "the head is ready");
    fake_tick = 100010;
    can_regs.ESR = 4;   // BOFF
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP, "a bus-off latches the head");
    can_regs.ESR = 0;
    fake_tick = 100015;
    Safety_Service();   // the bus is back: the next bus-off is a new edge
    frame_kind(100020, ESTOPF_KIND_RELEASE, 0, 0, 3, 3);
    TEST_ASSERT(system_error_flag == 1, "a release is refused until a new heartbeat is heard");
    frame_kind(100030, ESTOPF_KIND_ALIVE, ESTOPF_ALIVE_OK, 0, 3, 4);
    frame_kind(100035, ESTOPF_KIND_RELEASE, 0, 0, 3, 5);
    TEST_ASSERT(system_error_flag == 0, "after a new heartbeat the release works");
    can_regs.ESR = 4;
    fake_tick = 100040;
    safe_state_calls = 0;
    Safety_Service();
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1 && safe_state_calls == 1, "a bus-off that stays is one stop, not one per pass");
}

static void test_a_malformed_frame_and_a_wrong_length_are_a_stop(int *failures)
{
    boot_with(FEAT_ESTOP_FOLLOW);
    make_ready(100000);
    TEST_ASSERT(system_error_flag == 0, "ready");
    const uint8_t two[2] = { 0x01, 0x01 };
    TEST_ASSERT(deliver(100010, two, 2) == 1 && system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP, "two bytes are a stop");
    boot_with(FEAT_ESTOP_FOLLOW);
    make_ready(100000);
    const uint8_t bad_complement[8] = { 0x03, 0x00, 0x00, 0x03, 0x09, 0x00, 0x00, 0xFF };
    deliver(100010, bad_complement, 8);
    TEST_ASSERT(system_error_flag == 1, "a heartbeat with a wrong complement is a stop");
    boot_with(FEAT_ESTOP_FOLLOW);
    make_ready(100000);
    deliver(100010, NULL, 0);
    TEST_ASSERT(system_error_flag == 1, "an empty frame is a stop");
}

// A power-up with the stop following on and this configured silence of the master in the F-RAM record.
static void boot_with_timeout(uint16_t timeout_ms)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_fail_reads = fake_fram_fail_writes = 0;
    FeatConfig_t c;
    Feat_ConfigBlank(&c);
    c.mask = FEAT_ESTOP_FOLLOW;
    Feat_ConfigSetEstopTimeoutMs(&c, timeout_ms);
    Feat_ConfigSeal(&c);
    memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &c, sizeof c);
    fake_tick = 100000;
    fake_tick_step = 0;
    active_tool = TOOL_SOLDERING_IRON;
    system_error_flag = 0;
    system_error_reason = 0;
    thermal_fault_code = 0;
    guards_reset = 0;
    safe_state_calls = 0;
    can_regs.ESR = 0;
    Features_Init();
    Safety_EstopInit();
}

static void test_the_configured_silence_reaches_the_glue(int *failures)
{
    boot_with_timeout(0);
    make_ready(100000);
    fake_tick = 100000 + 100;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "a record without a timeout keeps the 100 ms: still ready at the edge");
    fake_tick = 100000 + 101;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP_SILENCE, "and latches one millisecond later");

    boot_with_timeout(50);
    make_ready(100000);
    fake_tick = 100000 + 50;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "a record with 50 ms: still ready at 50 ms");
    fake_tick = 100000 + 51;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP_SILENCE, "and latched at 51 ms, before the default would");

    boot_with_timeout(30);   // 20 to 39 ms were accepted before the lowest became 40 ms: now rejected, the default is used
    make_ready(100000);
    fake_tick = 100000 + 100;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "a record with 30 ms is rejected since the lowest is 40 ms: the default 100 ms is in use");

    boot_with_timeout(400);
    make_ready(100000);
    fake_tick = 100000 + 400;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "a record with 400 ms waits the 400 ms");
    fake_tick = 100000 + 401;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1, "and latches after them");

    boot_with_timeout(5);   // below the limit: rejected, the default is used
    make_ready(100000);
    fake_tick = 100000 + 100;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "a record with 5 ms is rejected: the default 100 ms is in use");
    fake_tick = 100000 + 101;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1, "and it latches after 100 ms");

    boot_with_timeout(60000);   // above the limit: rejected as well, never accepted in silence
    make_ready(100000);
    fake_tick = 100000 + 101;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1, "a record with 60000 ms is rejected: the head does not wait for it");
}

static void test_the_clear_error_command_does_not_lift_a_stop(int *failures)
{
    boot_with(FEAT_ESTOP_FOLLOW | FEAT_CLEAR_ERROR);
    const uint8_t cler[8] = { 0x43, 0x4C, 0x45, 0x52, 0, 0, 0, 0 };
    memcpy(rxData, cler, 8);
    rxHeader.StdId = URTC_CAN_ID_CLEAR_ERROR;
    rxHeader.DLC = 4;
    TEST_ASSERT(Safety_HandleClearError() == 1 && system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_NOT_CLEARABLE,
                "the command is answered and the stop stays: it ends with a release");
    fake_tick = 100000000;
    Safety_Service();   // a latched head has no silence to report
    TEST_ASSERT(Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP, "the reason stays the one of the stop");
}

// ---- the silence of the master over the bus: 0x1AE with DLC 8 writes it, 0x1AF with the byte 03 or 04 asks for it --------------------------------------------
static void feed(uint32_t id, const uint8_t *data, uint32_t dlc)
{
    fake_hal_reset_recording();
    memset(rxData, 0, sizeof rxData);
    if (data) memcpy(rxData, data, dlc);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
}

static const FakeTxFrame *answer(void)
{
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == URTC_CAN_ID_FEATURES_RESP) return &fake_tx[i];
    return 0;
}

static void test_the_wire_vectors(int *failures)
{
    int bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_WRITE_CASES); i++) {
        const estop_write_case_t *c = &ESTOP_WRITE_CASES[i];
        uint8_t mask = 0xEE, mask_ext = 0xEE;
        uint32_t timeout = 0xEEEEu;
        const uint8_t ok = Feat_ParseWrite(c->dlc, c->data, &mask, &mask_ext, &timeout);
        if (ok != c->valid) { bad++; continue; }
        if (!ok) continue;
        const uint32_t expected = (c->timeout_ms < 0) ? FEAT_TIMEOUT_KEEP : (uint32_t)c->timeout_ms;
        if (mask != c->mask || mask_ext != c->mask_ext || timeout != expected) bad++;
    }
    TEST_ASSERT(bad == 0, "every write of 0x1AE of URTC-COMMON is accepted or ignored as the twin says, with the same masks and silence");
    bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_QUERY_CASES); i++) {
        const estop_query_case_t *c = &ESTOP_QUERY_CASES[i];
        if (Feat_QueryForm(c->dlc, c->data) != c->form) bad++;
    }
    TEST_ASSERT(bad == 0, "every query of 0x1AF gets the form the twin says");
    bad = 0;
    for (int i = 0; i < ESTOP_VEC_COUNT(ESTOP_ANSWER_CASES); i++) {
        const estop_answer_case_t *c = &ESTOP_ANSWER_CASES[i];
        uint8_t out[8];
        Feat_EncodeResponseTimeout(c->stored, c->running, c->stored_ext, c->running_ext, c->timeout_ms, out);
        if (memcmp(out, c->data, 8) != 0) bad++;
    }
    TEST_ASSERT(bad == 0, "the eight-byte answer is the one of the twin, silence high byte first");

    // all 65536 values: the whole command is taken only for 0 and 40 to 500
    int wrong = 0;
    for (uint32_t v = 0; v <= 0xFFFFu; v++) {
        const uint8_t cmd[8] = { 'F', 'E', 'A', 'T', 0x80, 0x00, (uint8_t)(v >> 8), (uint8_t)(v & 0xFFu) };
        uint8_t mask, mask_ext;
        uint32_t timeout;
        const uint8_t ok = Feat_ParseWrite(8, cmd, &mask, &mask_ext, &timeout);
        const uint8_t expect = (uint8_t)(v == 0u || (v >= URTC_ESTOP_ALIVE_TIMEOUT_MIN_MS && v <= URTC_ESTOP_ALIVE_TIMEOUT_MAX_MS));
        if (ok != expect || (ok && timeout != v)) wrong++;
    }
    TEST_ASSERT(wrong == 0, "for all 65536 values a write is taken only for 0 and 40 to 500, and then as it is");
}

// A power-up that keeps the F-RAM as it is.
static void power_up_again(void)
{
    fake_hal_reset_recording();
    fake_tick = 100000;
    system_error_flag = 0;
    system_error_reason = 0;
    thermal_fault_code = 0;
    can_regs.ESR = 0;
    Features_Init();
    Safety_EstopInit();
}

static void test_the_silence_over_the_bus_through_the_glue(int *failures)
{
    boot_with_timeout(0);
    const uint8_t q_in_force[1] = { 0x03 }, q_stored[1] = { 0x04 };
    feed(URTC_CAN_ID_FEATURES_RESP, q_in_force, 1);
    Features_HandleCommand();
    const FakeTxFrame *r = answer();
    TEST_ASSERT(r && r->dlc == 8 && r->data[0] == FEAT_ESTOP_FOLLOW && r->data[1] == FEAT_ESTOP_FOLLOW && r->data[6] == 0 && r->data[7] == 100,
                "the query 03 answers the silence in force: the 100 ms of the default");
    feed(URTC_CAN_ID_FEATURES_RESP, q_stored, 1);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->dlc == 8 && r->data[6] == 0 && r->data[7] == 0, "the query 04 answers the stored silence: 0, the default");

    // a write of eight bytes stores the silence, answers with the stored value, and acts after the next power-up
    const uint8_t w50[8] = { 'F', 'E', 'A', 'T', 0x80, 0x00, 0x00, 0x32 };
    feed(URTC_CAN_ID_SET_FEATURES, w50, 8);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->dlc == 8 && r->data[0] == FEAT_ESTOP_FOLLOW && r->data[6] == 0 && r->data[7] == 50, "a write of 50 ms is answered with the eight-byte form and the stored value");
    feed(URTC_CAN_ID_FEATURES_RESP, q_in_force, 1);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->data[7] == 100, "and the silence in force is still the old one until the next power-up");
    power_up_again();
    feed(URTC_CAN_ID_FEATURES_RESP, q_in_force, 1);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->data[6] == 0 && r->data[7] == 50, "after the power-up the silence in force is 50 ms");
    make_ready(100000);
    fake_tick = 100000 + 51;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_ESTOP_SILENCE, "and the head latches after 50 ms of silence");

    // the old forms keep the stored silence
    boot_with_timeout(300);
    const uint8_t five[5] = { 'F', 'E', 'A', 'T', 0x80 };
    const uint8_t six[6] = { 'F', 'E', 'A', 'T', 0x80, 0x00 };
    feed(URTC_CAN_ID_SET_FEATURES, five, 5);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->dlc == 3, "a five-byte write is still answered with three bytes");
    feed(URTC_CAN_ID_SET_FEATURES, six, 6);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->dlc == 6, "a six-byte write is still answered with six bytes");
    feed(URTC_CAN_ID_FEATURES_RESP, q_stored, 1);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->dlc == 8 && r->data[6] == 0x01 && r->data[7] == 0x2C, "and neither of them changed the stored silence (300 ms)");

    // a write with 0 clears the silence
    const uint8_t w0[8] = { 'F', 'E', 'A', 'T', 0x80, 0x00, 0x00, 0x00 };
    feed(URTC_CAN_ID_SET_FEATURES, w0, 8);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->dlc == 8 && r->data[6] == 0 && r->data[7] == 0, "a write of 0 clears the stored silence (the default applies)");

    // a value outside the limits: the whole command is ignored, nothing is written and nothing is answered
    boot_with_timeout(300);
    const uint8_t w39[8] = { 'F', 'E', 'A', 'T', 0x00, 0x00, 0x00, 0x27 };
    const int writes = fake_fram_write_calls;
    feed(URTC_CAN_ID_SET_FEATURES, w39, 8);
    Features_HandleCommand();
    TEST_ASSERT(fake_fram_write_calls == writes && fake_tx_count == 0, "a silence of 39 ms is ignored as a whole: nothing written, nothing answered");
    const uint8_t seven[7] = { 'F', 'E', 'A', 'T', 0x00, 0x00, 0x32 };
    feed(URTC_CAN_ID_SET_FEATURES, seven, 7);
    Features_HandleCommand();
    TEST_ASSERT(fake_fram_write_calls == writes && fake_tx_count == 0, "a DLC of 7 is ignored");

    // ESTOP_FOLLOW off: nothing is in force
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    Features_Init();
    Safety_EstopInit();
    feed(URTC_CAN_ID_FEATURES_RESP, q_in_force, 1);
    Features_HandleCommand();
    r = answer();
    TEST_ASSERT(r && r->dlc == 8 && r->data[6] == 0 && r->data[7] == 0, "with the switch off the silence in force is 0");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - following the emergency stop frame\n");
    test_the_frames(&failures);
    test_the_scenarios(&failures);
    test_the_timeout_tables(&failures);
    test_the_constants(&failures);
    test_a_stop_is_never_refused(&failures);
    test_with_the_switch_off_nothing_changes(&failures);
    test_the_head_powers_up_latched(&failures);
    test_a_trip_a_release_and_the_silence(&failures);
    test_a_release_never_lifts_another_error(&failures);
    test_a_bus_off(&failures);
    test_a_malformed_frame_and_a_wrong_length_are_a_stop(&failures);
    test_the_clear_error_command_does_not_lift_a_stop(&failures);
    test_the_configured_silence_reaches_the_glue(&failures);
    test_the_wire_vectors(&failures);
    test_the_silence_over_the_bus_through_the_glue(&failures);
    if (failures == 0) {
        printf("  OK   all emergency stop following host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
