// =============================================================================
// URTC Firmware - Pure, host-testable CAN dispatch routing table
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Real gap this closes: tests/ covers the MLX90640 sensor API on host, but
// nothing ever exercised the actual tool-ID -> handler routing table in
// firmware_can_dispatch.c - the table a mistake in (e.g. TOOL_DRILL
// silently routed to Handle_CAN_Laser()) would misroute every CAN command
// for that tool on real hardware, with nothing catching it before a real
// board did.
//
// firmware_can_dispatch.c's own switch (real, on-target, HAL-coupled)
// can't be compiled or run on host - it includes firmware_common.h,
// which pulls in stm32f3xx_hal.h. This file is the pure extraction: it
// takes a raw tool-ID byte (not the ToolMode_t enum itself - depending on
// that type here would require including firmware_common.h, defeating
// the whole point) and returns which route firmware_can_dispatch.c's own
// switch takes for it. It has zero HAL dependency and zero side effects,
// so tests/test_can_dispatch_routing.c can drive it directly on host,
// with gcc, no ARM toolchain or physical board needed.
//
// Kept in sync with the real ToolMode_t enum in firmware_common.h by a
// set of _Static_assert checks in firmware_can_dispatch.c itself (which
// DOES include firmware_common.h) - those only compile as part of the
// real on-target arm-none-eabi-gcc build, so a future edit to
// firmware_common.h's own tool ID values that silently drifts from the
// numbers hardcoded below fails that real build loudly, rather than
// leaving this host test suite quietly checking the wrong numbers.
// =============================================================================
#ifndef FIRMWARE_CAN_DISPATCH_ROUTING_H
#define FIRMWARE_CAN_DISPATCH_ROUTING_H

#include <stdint.h>

typedef enum {
    CAN_ROUTE_NONE = 0,               // TOOL_VACUUM_PICKUP, TOOL_SCAN_PROBE,
                                       // TOOL_CONFORMAL_COATING, TOOL_PRESSFIT_INSERTER,
                                       // TOOL_INVALID, and any unassigned ID (26-31)
    CAN_ROUTE_SOLDERING_AND_MOTION,    // TOOL_SOLDERING_IRON
    CAN_ROUTE_MOTION_ONLY,             // TOOL_PASTE_DISPENSER, TOOL_LIQUID_DISPENSER,
                                       // TOOL_SCREWDRIVER, TOOL_GRIPPER_GIMBAL,
                                       // TOOL_GRIPPER_NEMA, TOOL_SMT_PICKPLACE,
                                       // TOOL_VACUUM_GRIPPER_LG
    CAN_ROUTE_DRILL,                  // TOOL_DRILL
    CAN_ROUTE_AOI,                    // TOOL_AOI_INSPECTION
    CAN_ROUTE_LASER,                  // TOOL_LASER_ENGRAVER
    CAN_ROUTE_3DPRINTER,              // TOOL_3D_PRINTER
    CAN_ROUTE_ELECTROMAGNET,          // TOOL_ELECTROMAGNET
    CAN_ROUTE_UVCURING,               // TOOL_UV_CURING
    CAN_ROUTE_WELDPULSE,              // TOOL_SPOT_WELDER, TOOL_ULTRASONIC_WELDER
    CAN_ROUTE_HOTAIR,                 // TOOL_HOTAIR_REWORK
    CAN_ROUTE_CRIMPING,               // TOOL_CRIMPING_ACTUATOR
    CAN_ROUTE_PASTEJETTING,           // TOOL_PASTE_JETTING
    CAN_ROUTE_FLYINGPROBE,            // TOOL_FLYING_PROBE
    CAN_ROUTE_THERMALINSPECTION       // TOOL_THERMAL_INSPECTION
} CanDispatchRoute_t;

// Pure function: given a raw tool-ID byte (the numeric value of the real
// ToolMode_t enum in firmware_common.h - see this file's own header
// comment on why the enum type itself isn't used here), returns which
// route firmware_can_dispatch.c's own switch takes. Every input value is
// handled - anything not explicitly listed (including 26-31, never a
// real assigned tool) returns CAN_ROUTE_NONE, matching the real switch's
// own `default: break;` behavior.
CanDispatchRoute_t CanDispatch_RouteForTool(uint8_t tool_id);

#endif // FIRMWARE_CAN_DISPATCH_ROUTING_H
