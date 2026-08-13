# -*- coding: utf-8 -*-
import sys
sys.path.insert(0, '.')
from render_engine import build_tool_image
from tool_data import TOOLS, UNIVERSAL, UNIVERSAL_CONNECTORS

# Mapeo ID hex -> nombre de archivo exacto, verificado contra firmware_common.h
# (nombre del enum TOOL_* tal cual) y contra el README raiz para los primeros 12.
FILENAMES = {
    0x00: "TOOL_SOLDERING_IRON",
    0x01: "TOOL_PASTE_DISPENSER",
    0x02: "TOOL_LIQUID_DISPENSER",
    0x03: "TOOL_SCREWDRIVER",
    0x04: "TOOL_VACUUM_PICKUP",
    0x05: "TOOL_DRILL",
    0x06: "TOOL_GRIPPER_GIMBAL",
    0x07: "TOOL_GRIPPER_NEMA",
    0x08: "TOOL_AOI_INSPECTION",
    0x09: "TOOL_LASER_ENGRAVER",
    0x0A: "TOOL_3D_PRINTER",
    0x0B: "TOOL_SCAN_PROBE",
    0x0C: "TOOL_SMT_PICKPLACE",
    0x0D: "TOOL_ELECTROMAGNET",
    0x0E: "TOOL_SPOT_WELDER",
    0x0F: "TOOL_CONFORMAL_COATING",
    0x10: "TOOL_VACUUM_GRIPPER_LG",
    0x11: "TOOL_FLYING_PROBE",
    0x12: "TOOL_UV_CURING",
    0x13: "TOOL_HOTAIR_REWORK",
    0x14: "TOOL_PRESSFIT_INSERTER",
    0x15: "TOOL_CRIMPING_ACTUATOR",
    0x16: "TOOL_THERMAL_INSPECTION",
    0x17: "TOOL_PASTE_JETTING",
    0x18: "TOOL_ULTRASONIC_WELDER",
}

assert set(FILENAMES.keys()) == set(TOOLS.keys()), "Desajuste entre FILENAMES y TOOLS"

OUT_DIR = "/home/claude/urtc_images/generated"
import os
os.makedirs(OUT_DIR, exist_ok=True)

for tid in sorted(TOOLS.keys()):
    data = TOOLS[tid]
    used_connectors = list(UNIVERSAL_CONNECTORS)
    for conn_name, _, _ in data["specific"]:
        if conn_name not in used_connectors:
            used_connectors.append(conn_name)
    if data.get("expansion_note"):
        used_connectors.append("CONN_EXPANSION")

    tool = {
        "id": tid,
        "name": data["name"],
        "used_connectors": used_connectors,
        "universal": UNIVERSAL,
        "specific": data["specific"],
        "notes": data["notes"],
        "expansion_note": data.get("expansion_note"),
    }

    img = build_tool_image(tool)
    fname = FILENAMES[tid] + ".png"
    img.save(os.path.join(OUT_DIR, fname))
    print(f"  0x{tid:02X}  {fname}  ({img.size[0]}x{img.size[1]})")

print(f"\n{len(TOOLS)} imagenes generadas en {OUT_DIR}")
