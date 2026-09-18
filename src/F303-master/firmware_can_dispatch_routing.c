// =============================================================================
// URTC Firmware - Pure, host-testable CAN dispatch routing table
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// See firmware_can_dispatch_routing.h for the full rationale. The
// numeric literals below are the real ToolMode_t values from
// firmware_common.h, copied deliberately (not #included) to keep this
// file HAL-free - kept honest against drift by the _Static_assert block
// in firmware_can_dispatch.c itself.
// =============================================================================
#include "firmware_can_dispatch_routing.h"

CanDispatchRoute_t CanDispatch_RouteForTool(uint8_t tool_id) {
    switch (tool_id) {
        case 0:  return CAN_ROUTE_SOLDERING_AND_MOTION; // TOOL_SOLDERING_IRON
        case 1:  // TOOL_PASTE_DISPENSER
        case 2:  // TOOL_LIQUID_DISPENSER
        case 3:  // TOOL_SCREWDRIVER
        case 6:  // TOOL_GRIPPER_GIMBAL
        case 7:  // TOOL_GRIPPER_NEMA
        case 12: // TOOL_SMT_PICKPLACE
        case 16: // TOOL_VACUUM_GRIPPER_LG
            return CAN_ROUTE_MOTION_ONLY;
        case 5:  return CAN_ROUTE_DRILL;                // TOOL_DRILL
        case 8:  return CAN_ROUTE_AOI;                  // TOOL_AOI_INSPECTION
        case 9:  return CAN_ROUTE_LASER;                // TOOL_LASER_ENGRAVER
        case 10: return CAN_ROUTE_3DPRINTER;            // TOOL_3D_PRINTER
        case 13: return CAN_ROUTE_ELECTROMAGNET;        // TOOL_ELECTROMAGNET
        case 18: return CAN_ROUTE_UVCURING;              // TOOL_UV_CURING
        case 14: // TOOL_SPOT_WELDER
        case 24: // TOOL_ULTRASONIC_WELDER
            return CAN_ROUTE_WELDPULSE;
        case 19: return CAN_ROUTE_HOTAIR;                // TOOL_HOTAIR_REWORK
        case 21: return CAN_ROUTE_CRIMPING;              // TOOL_CRIMPING_ACTUATOR
        case 23: return CAN_ROUTE_PASTEJETTING;          // TOOL_PASTE_JETTING
        case 17: return CAN_ROUTE_FLYINGPROBE;           // TOOL_FLYING_PROBE
        case 22: return CAN_ROUTE_THERMALINSPECTION;     // TOOL_THERMAL_INSPECTION
        // 4 (TOOL_VACUUM_PICKUP), 11 (TOOL_SCAN_PROBE), 15 (TOOL_CONFORMAL_COATING),
        // 20 (TOOL_PRESSFIT_INSERTER), 25 (TOOL_INVALID), and any unassigned
        // ID (26-31) all fall through here, matching the real dispatcher's
        // own `default: break;`.
        default:
            return CAN_ROUTE_NONE;
    }
}
