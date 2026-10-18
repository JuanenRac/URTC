// =============================================================================
// URTC Firmware - Host logic tests for firmware_can_dispatch.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The receive callback that every CAN frame goes through: which handlers run,
// in which order, and above all what the critical-error gate lets through. The
// real firmware_can_dispatch.c (and the routing table it uses) is compiled
// against the fake HAL, with every handler replaced by a recorder, so the
// order of the calls and the gating can be asserted exactly.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_can_global.h"
#include "firmware_can_slavebridge.h"
#include "firmware_can_soldering.h"
#include "firmware_can_motion.h"
#include "firmware_can_drill.h"
#include "firmware_can_aoi.h"
#include "firmware_can_laser.h"
#include "firmware_can_printer3d.h"
#include "firmware_can_electromagnet.h"
#include "firmware_can_uvcuring.h"
#include "firmware_can_weldpulse.h"
#include "firmware_can_hotair.h"
#include "firmware_can_crimping.h"
#include "firmware_can_pastejetting.h"
#include "firmware_can_flyingprobe.h"
#include "firmware_can_thermalinspection.h"

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_m);
CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag;
volatile uint32_t can_led_tick;

// ---- recorders -------------------------------------------------------------
static char trace[256];
static int trace_len;
static void note(const char *name)
{
    int n = (int)strlen(name);
    if (trace_len + n + 1 < (int)sizeof trace) {
        memcpy(trace + trace_len, name, (size_t)n);
        trace_len += n;
        trace[trace_len++] = ' ';
        trace[trace_len] = 0;
    }
}
static int make_error_in_post, make_error_in_bridge;

void Handle_CAN_GlobalCommands_PreErrorGate(void) { note("pre"); }
void Handle_CAN_GlobalCommands_PostErrorGate(void) { note("post"); if (make_error_in_post) system_error_flag = 1; }
void Handle_CAN_SlaveBridge(void) { note("bridge"); if (make_error_in_bridge) system_error_flag = 1; }
void Handle_CAN_SolderingIron(void) { note("solder"); }
void Handle_CAN_MotionTools(void) { note("motion"); }
void Handle_CAN_Drill(void) { note("drill"); }
void Handle_CAN_AOI(void) { note("aoi"); }
void Handle_CAN_Laser(void) { note("laser"); }
void Handle_CAN_3DPrinter(void) { note("3dp"); }
void Handle_CAN_Electromagnet(void) { note("magnet"); }
void Handle_CAN_UVCuring(void) { note("uv"); }
void Handle_CAN_WeldPulse(void) { note("weld"); }
void Handle_CAN_HotAirRework(void) { note("hotair"); }
void Handle_CAN_CrimpingActuator(void) { note("crimp"); }
void Handle_CAN_PasteJetting(void) { note("jet"); }
void Handle_CAN_FlyingProbe(void) { note("probe"); }
void Handle_CAN_ThermalInspection(void) { note("thermal"); }

static void deliver(uint32_t id, ToolMode_t tool, int error)
{
    fake_hal_reset_recording();
    trace_len = 0;
    trace[0] = 0;
    make_error_in_post = make_error_in_bridge = 0;
    memset(&fake_rx_queue[0], 0, sizeof fake_rx_queue[0]);
    fake_rx_queue[0].hdr.StdId = id;
    fake_rx_queue[0].hdr.DLC = 1;
    fake_rx_queue_count = 1;
    active_tool = tool;
    system_error_flag = (uint8_t)error;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
}

typedef struct { ToolMode_t tool; const char *expect; } Route;

void run_can_dispatch_tests(int *failures)
{
    static const Route routes[] = {
        { TOOL_SOLDERING_IRON, "pre post bridge solder motion " },
        { TOOL_PASTE_DISPENSER, "pre post bridge motion " },
        { TOOL_LIQUID_DISPENSER, "pre post bridge motion " },
        { TOOL_SCREWDRIVER, "pre post bridge motion " },
        { TOOL_VACUUM_PICKUP, "pre post bridge " },
        { TOOL_DRILL, "pre post bridge drill " },
        { TOOL_GRIPPER_GIMBAL, "pre post bridge motion " },
        { TOOL_GRIPPER_NEMA, "pre post bridge motion " },
        { TOOL_AOI_INSPECTION, "pre post bridge aoi " },
        { TOOL_LASER_ENGRAVER, "pre post bridge laser " },
        { TOOL_3D_PRINTER, "pre post bridge 3dp " },
        { TOOL_SCAN_PROBE, "pre post bridge " },
        { TOOL_SMT_PICKPLACE, "pre post bridge motion " },
        { TOOL_ELECTROMAGNET, "pre post bridge magnet " },
        { TOOL_SPOT_WELDER, "pre post bridge weld " },
        { TOOL_CONFORMAL_COATING, "pre post bridge " },
        { TOOL_VACUUM_GRIPPER_LG, "pre post bridge motion " },
        { TOOL_FLYING_PROBE, "pre post bridge probe " },
        { TOOL_UV_CURING, "pre post bridge uv " },
        { TOOL_HOTAIR_REWORK, "pre post bridge hotair " },
        { TOOL_PRESSFIT_INSERTER, "pre post bridge " },
        { TOOL_CRIMPING_ACTUATOR, "pre post bridge crimp " },
        { TOOL_THERMAL_INSPECTION, "pre post bridge thermal " },
        { TOOL_PASTE_JETTING, "pre post bridge jet " },
        { TOOL_ULTRASONIC_WELDER, "pre post bridge weld " },
        { TOOL_INVALID, "pre post bridge " },
    };
    char label[96];
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        deliver(0x120, routes[i].tool, 0);
        snprintf(label, sizeof label, "tool %d: handlers run as [%s]", (int)routes[i].tool, routes[i].expect);
        TEST_ASSERT(strcmp(trace, routes[i].expect) == 0, label);
    }

    // The diagnostics that must work during a fault run first; an unassigned jumper value reaches no tool.
    deliver(0x120, (ToolMode_t)31, 0);
    TEST_ASSERT(strcmp(trace, "pre post bridge ") == 0, "an unassigned tool id reaches no tool handler");

    // Critical error: only the read-only pre-gate commands and the 0x100 status/LED command pass.
    deliver(0x120, TOOL_SOLDERING_IRON, 1);
    TEST_ASSERT(strcmp(trace, "pre ") == 0, "during a critical error an actuation command reaches no handler at all");
    deliver(0x7F8, TOOL_DRILL, 1);
    TEST_ASSERT(strcmp(trace, "pre ") == 0, "during a critical error only the pre-gate handler sees the frame (queries live there)");
    deliver(0x100, TOOL_SOLDERING_IRON, 1);
    TEST_ASSERT(strcmp(trace, "pre post bridge ") == 0, "0x100 passes the error gate (the host can still clear the fault state) but no tool handler runs");
    deliver(0x101, TOOL_SOLDERING_IRON, 1);
    TEST_ASSERT(strcmp(trace, "pre ") == 0, "0x101 is not 0x100: it is stopped by the gate");

    // A fault raised by an earlier handler for this very frame stops the tool handler.
    deliver(0x120, TOOL_DRILL, 0);
    TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "baseline before provoking a fault in the post-gate handler");
    deliver(0x120, TOOL_DRILL, 0);
    make_error_in_post = 0;
    fake_hal_reset_recording();
    trace_len = 0; trace[0] = 0;
    make_error_in_post = 1;
    memset(&fake_rx_queue[0], 0, sizeof fake_rx_queue[0]);
    fake_rx_queue[0].hdr.StdId = 0x120;
    fake_rx_queue_count = 1;
    active_tool = TOOL_DRILL;
    system_error_flag = 0;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
    TEST_ASSERT(strcmp(trace, "pre post bridge ") == 0 && system_error_flag == 1, "an error raised by the post-gate handler stops the tool handler for that frame");
    trace_len = 0; trace[0] = 0;
    make_error_in_post = 0; make_error_in_bridge = 1;
    fake_rx_queue_count = 1;
    system_error_flag = 0;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
    TEST_ASSERT(strcmp(trace, "pre post bridge ") == 0 && system_error_flag == 1, "an error raised by the slave bridge stops the tool handler for that frame");

    // The frame must have been read; a failed read does nothing.
    fake_hal_reset_recording();
    trace_len = 0; trace[0] = 0;
    fake_rx_queue_count = 0;
    can_led_tick = 0;
    fake_tick = 777;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
    TEST_ASSERT(trace_len == 0 && can_led_tick == 0, "when the frame cannot be read nothing is called and the activity LED tick is untouched");
    deliver(0x120, TOOL_DRILL, 0);
    fake_tick = 777;
    fake_rx_queue_count = 1;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
    TEST_ASSERT(can_led_tick == 777, "a received frame stamps the activity LED tick with the current time");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_can_dispatch.c\n");
    run_can_dispatch_tests(&failures);
    if (failures == 0) {
        printf("  OK   all CAN dispatch (error gate) host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
