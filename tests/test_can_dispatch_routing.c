// =============================================================================
// URTC Firmware - Host logic tests for firmware_can_dispatch_routing.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
// Real gap this closes: tests/ covered the MLX90640 sensor API but never
// the general CAN tool-ID -> handler routing table in
// firmware_can_dispatch.c - the exact case grouping that decides which
// real handler runs for which tool head. This drives the pure, HAL-free
// extraction of that table (firmware_can_dispatch_routing.c) directly on
// host, checking EVERY real ToolMode_t value (0-25) plus a few
// deliberately-unassigned IDs (26-31), matching firmware_common.h's own
// enum exactly - see that file and firmware_can_dispatch.c's own
// _Static_assert block for how the two are kept in sync.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "test_runner.h"
#include "firmware_can_dispatch_routing.h"

void run_can_dispatch_routing_tests(int *failures)
{
    // TOOL_SOLDERING_IRON (0) - the one tool routed to two handlers.
    TEST_ASSERT(CanDispatch_RouteForTool(0) == CAN_ROUTE_SOLDERING_AND_MOTION,
                "TOOL_SOLDERING_IRON (0) routes to soldering+motion");

    // The 7 tools sharing the plain-stepper CAN_ROUTE_MOTION_ONLY route:
    // TOOL_PASTE_DISPENSER, TOOL_LIQUID_DISPENSER, TOOL_SCREWDRIVER,
    // TOOL_GRIPPER_GIMBAL, TOOL_GRIPPER_NEMA, TOOL_SMT_PICKPLACE,
    // TOOL_VACUUM_GRIPPER_LG.
    {
        const uint8_t motion_only_ids[] = {1, 2, 3, 6, 7, 12, 16};
        for (size_t i = 0; i < sizeof(motion_only_ids) / sizeof(motion_only_ids[0]); i++) {
            char msg[96];
            snprintf(msg, sizeof(msg), "tool ID %u routes to CAN_ROUTE_MOTION_ONLY", motion_only_ids[i]);
            TEST_ASSERT(CanDispatch_RouteForTool(motion_only_ids[i]) == CAN_ROUTE_MOTION_ONLY, msg);
        }
    }

    TEST_ASSERT(CanDispatch_RouteForTool(5) == CAN_ROUTE_DRILL, "TOOL_DRILL (5) routes to CAN_ROUTE_DRILL");
    TEST_ASSERT(CanDispatch_RouteForTool(8) == CAN_ROUTE_AOI, "TOOL_AOI_INSPECTION (8) routes to CAN_ROUTE_AOI");
    TEST_ASSERT(CanDispatch_RouteForTool(9) == CAN_ROUTE_LASER, "TOOL_LASER_ENGRAVER (9) routes to CAN_ROUTE_LASER");
    TEST_ASSERT(CanDispatch_RouteForTool(10) == CAN_ROUTE_3DPRINTER, "TOOL_3D_PRINTER (10) routes to CAN_ROUTE_3DPRINTER");
    TEST_ASSERT(CanDispatch_RouteForTool(13) == CAN_ROUTE_ELECTROMAGNET, "TOOL_ELECTROMAGNET (13) routes to CAN_ROUTE_ELECTROMAGNET");
    TEST_ASSERT(CanDispatch_RouteForTool(18) == CAN_ROUTE_UVCURING, "TOOL_UV_CURING (18) routes to CAN_ROUTE_UVCURING");

    // TOOL_SPOT_WELDER (14) and TOOL_ULTRASONIC_WELDER (24) share one route.
    TEST_ASSERT(CanDispatch_RouteForTool(14) == CAN_ROUTE_WELDPULSE, "TOOL_SPOT_WELDER (14) routes to CAN_ROUTE_WELDPULSE");
    TEST_ASSERT(CanDispatch_RouteForTool(24) == CAN_ROUTE_WELDPULSE, "TOOL_ULTRASONIC_WELDER (24) routes to CAN_ROUTE_WELDPULSE");

    TEST_ASSERT(CanDispatch_RouteForTool(19) == CAN_ROUTE_HOTAIR, "TOOL_HOTAIR_REWORK (19) routes to CAN_ROUTE_HOTAIR");
    TEST_ASSERT(CanDispatch_RouteForTool(21) == CAN_ROUTE_CRIMPING, "TOOL_CRIMPING_ACTUATOR (21) routes to CAN_ROUTE_CRIMPING");
    TEST_ASSERT(CanDispatch_RouteForTool(23) == CAN_ROUTE_PASTEJETTING, "TOOL_PASTE_JETTING (23) routes to CAN_ROUTE_PASTEJETTING");
    TEST_ASSERT(CanDispatch_RouteForTool(17) == CAN_ROUTE_FLYINGPROBE, "TOOL_FLYING_PROBE (17) routes to CAN_ROUTE_FLYINGPROBE");
    TEST_ASSERT(CanDispatch_RouteForTool(22) == CAN_ROUTE_THERMALINSPECTION, "TOOL_THERMAL_INSPECTION (22) routes to CAN_ROUTE_THERMALINSPECTION");

    // Deliberately-inert tool IDs: TOOL_VACUUM_PICKUP (4) and
    // TOOL_SCAN_PROBE (11) take no CAN command of their own;
    // TOOL_CONFORMAL_COATING (15) and TOOL_PRESSFIT_INSERTER (20) are
    // physically out of this board's scope; TOOL_INVALID (25) is the
    // real "no jumper set" state. All five - and every unassigned ID
    // above that (26-31) - must call nothing.
    {
        const uint8_t none_ids[] = {4, 11, 15, 20, 25, 26, 27, 28, 29, 30, 31};
        for (size_t i = 0; i < sizeof(none_ids) / sizeof(none_ids[0]); i++) {
            char msg[96];
            snprintf(msg, sizeof(msg), "tool ID %u routes to CAN_ROUTE_NONE (inert/unassigned)", none_ids[i]);
            TEST_ASSERT(CanDispatch_RouteForTool(none_ids[i]) == CAN_ROUTE_NONE, msg);
        }
    }
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_can_dispatch_routing.c\n");
    run_can_dispatch_routing_tests(&failures);
    if (failures == 0) {
        printf("  OK   all CAN dispatch routing host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
