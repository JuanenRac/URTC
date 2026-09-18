// =============================================================================
// URTC Firmware - Main CAN receive dispatcher
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The HAL callback fired for every received CAN frame. Structure (exactly
// matching the single-file build): populate rxHeader/rxData, then global
// commands answered regardless of fault state, then the first error-gate
// (blocks everything except 0x100 while system_error_flag is set), then
// global commands that ARE blocked during a fault, then the second
// error-gate (blocks the entire per-tool switch below during a fault),
// then dispatch to whichever of the 25 tool profiles is currently active
// (TOOL_VACUUM_PICKUP and TOOL_SCAN_PROBE take no CAN command of their
// own - see CANBUS.TXT - so they correctly fall to the default case
// alongside TOOL_CONFORMAL_COATING/TOOL_PRESSFIT_INSERTER below).
//
// Adding a new tool profile (see the 16 more planned) means: write its own
// firmware_can_<toolname>.c/.h with a Handle_CAN_<ToolName>(void) function,
// then add one case here calling it. Nothing else in this file changes.
// =============================================================================
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
#include "firmware_can_dispatch_routing.h"

// Keeps firmware_can_dispatch_routing.c's own hardcoded tool-ID literals
// (deliberately NOT #include-ing this file's ToolMode_t, to stay HAL-free
// and host-testable - see that file's own header comment) honest against
// this real enum. A future edit to ToolMode_t's numeric values that
// silently drifts from those literals fails this real on-target build
// loudly, rather than leaving tests/test_can_dispatch_routing.c quietly
// checking numbers that no longer match production.
_Static_assert(TOOL_SOLDERING_IRON == 0, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_PASTE_DISPENSER == 1, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_LIQUID_DISPENSER == 2, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_SCREWDRIVER == 3, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_VACUUM_PICKUP == 4, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_DRILL == 5, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_GRIPPER_GIMBAL == 6, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_GRIPPER_NEMA == 7, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_AOI_INSPECTION == 8, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_LASER_ENGRAVER == 9, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_3D_PRINTER == 10, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_SCAN_PROBE == 11, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_SMT_PICKPLACE == 12, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_ELECTROMAGNET == 13, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_SPOT_WELDER == 14, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_CONFORMAL_COATING == 15, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_VACUUM_GRIPPER_LG == 16, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_FLYING_PROBE == 17, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_UV_CURING == 18, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_HOTAIR_REWORK == 19, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_PRESSFIT_INSERTER == 20, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_CRIMPING_ACTUATOR == 21, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_THERMAL_INSPECTION == 22, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_PASTE_JETTING == 23, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_ULTRASONIC_WELDER == 24, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");
_Static_assert(TOOL_INVALID == 25, "firmware_can_dispatch_routing.c's tool-ID literals are out of sync with ToolMode_t");

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_m) {
    if (HAL_CAN_GetRxMessage(hcan_m, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK) {

        can_led_tick = HAL_GetTick();

        Handle_CAN_GlobalCommands_PreErrorGate();

        // Blocks every incoming command except 0x100 (lighting) while a
        // critical error is declared - movement/power commands must not be
        // processed mid-fault, which would defeat the point of raising the
        // flag. 0x100 stays open because it touches nothing
        // actuation-relevant (just LED color/OLED mode), letting the master
        // command a distinct warning pattern on the LEDs to signal the
        // fault externally, rather than the board going dark on the
        // outside the moment it declares itself unsafe.
        if (system_error_flag && rxHeader.StdId != 0x100) {
            return;
        }

        Handle_CAN_GlobalCommands_PostErrorGate();
        Handle_CAN_SlaveBridge();

        // Blocks every incoming command except 0x100 (lighting) while a
        // critical error is active - nothing here is safe to let through
        // otherwise. A master that didn't notice the fault (or was itself
        // the cause of it) could keep driving motors or firing the laser
        // while the OLED reads SYSTEM BLOCKED. Lighting/night-mode above
        // still works since neither is hazardous either way.
        if (system_error_flag) {
            return;
        }

        // Real tool-ID -> handler routing decision now lives in the pure,
        // host-tested CanDispatch_RouteForTool() (see
        // firmware_can_dispatch_routing.c/.h and
        // tests/test_can_dispatch_routing.c) - this switch only calls the
        // real, HAL-touching handlers, exactly as before this split.
        // TOOL_CONFORMAL_COATING (doc #5) and TOOL_PRESSFIT_INSERTER
        // (doc #11) deliberately have no case here, not an oversight -
        // both tools' own actuator (spray valve solenoid; press-fit
        // linear actuator) and sensor (pressure-reached) are
        // documented as physically "installed in mainboard of robot",
        // outside this board's own scope entirely. This board's role
        // for both is limited to identification (the ID jumper reading
        // already handles that) and status LEDs (handled generically
        // for every tool via 0x100, not per-tool) - there's no
        // URTC-side command for either tool to actually respond to.
        switch (CanDispatch_RouteForTool((uint8_t)active_tool)) {
            case CAN_ROUTE_SOLDERING_AND_MOTION:
                Handle_CAN_SolderingIron();
                Handle_CAN_MotionTools(); // doc #16's own solder-wire-feeder motor,
                                          // sharing CONN_MOT and the 0x120 protocol
                                          // with the 7 tools in the case below -
                                          // called unconditionally here (like they
                                          // are for their own tool IDs) since each
                                          // handler internally filters by its own
                                          // rxHeader.StdId before acting, so calling
                                          // both is safe regardless of which ID this
                                          // particular frame actually is
                break;
            case CAN_ROUTE_MOTION_ONLY: // doc #1/#6's own plain steppers, and the rest of the shared 0x120 group
                Handle_CAN_MotionTools();
                break;
            case CAN_ROUTE_DRILL:
                Handle_CAN_Drill();
                break;
            case CAN_ROUTE_AOI:
                Handle_CAN_AOI();
                break;
            case CAN_ROUTE_LASER:
                Handle_CAN_Laser();
                break;
            case CAN_ROUTE_3DPRINTER:
                Handle_CAN_3DPrinter();
                break;
            case CAN_ROUTE_ELECTROMAGNET: // doc #3
                Handle_CAN_Electromagnet();
                break;
            case CAN_ROUTE_UVCURING:      // doc #8
                Handle_CAN_UVCuring();
                break;
            case CAN_ROUTE_WELDPULSE:     // doc #4/#15
                Handle_CAN_WeldPulse();
                break;
            case CAN_ROUTE_HOTAIR:        // doc #10
                Handle_CAN_HotAirRework();
                break;
            case CAN_ROUTE_CRIMPING:      // doc #12
                Handle_CAN_CrimpingActuator();
                break;
            case CAN_ROUTE_PASTEJETTING:  // doc #14
                Handle_CAN_PasteJetting();
                break;
            case CAN_ROUTE_FLYINGPROBE:   // doc #7
                Handle_CAN_FlyingProbe();
                break;
            case CAN_ROUTE_THERMALINSPECTION: // doc #13
                Handle_CAN_ThermalInspection();
                break;
            case CAN_ROUTE_NONE:
            default:
                break;
        }
    }
}
