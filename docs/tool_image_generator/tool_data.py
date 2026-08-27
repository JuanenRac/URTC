# -*- coding: utf-8 -*-
"""
Datos reales de las 25 herramientas URTC, verificados contra:
docs/TOOLS.TXT, docs/PINOUT_CONNECTORS.TXT, docs/CANBUS.TXT, docs/EXPANSION.TXT
"""

# --- Bloque universal, identico para las 25 herramientas ---
UNIVERSAL = [
    ("CONN_MAIN", "24V power and CAN bus to the master controller (always required)",
     ["Pin 1: +24V  -  wire: red (convention, no documented color)",
      "Pin 2: GND  -  wire: black (convention)",
      "Pin 3: CAN_H  -  wire: no standard color - depends on your cable",
      "Pin 4: CAN_L  -  wire: no standard color - depends on your cable"]),
    ("CONN_OLED", "OLED display over I2C (optional but recommended)",
     ["Pin 1: GND", "Pin 2: +3.3V", "Pin 3: SCL", "Pin 4: SDA"]),
    ("CONN_LED1", "Status NeoPixel LED strip (optional)",
     ["Pin 1: GND", "Pin 2: DATA", "Pin 3: +5V"]),
    ("CONN_LED2", "Camera/inspection ring light (optional)",
     ["Pin 1: GND", "Pin 2: DATA", "Pin 3: +5V"]),
]
UNIVERSAL_CONNECTORS = ["CONN_MAIN", "CONN_OLED", "CONN_LED1", "CONN_LED2"]

# CONN_MOT pin block, reused verbatim by every stepper/gimbal-motion tool
def mot_pins(mode_label):
    return ["Pin 1: OA1 (Coil A, wire 1)" + (f"  -  {mode_label}" if mode_label else ""),
            "Pin 2: OA2 (Coil A, wire 2)",
            "Pin 3: OB1 (Coil B, wire 1)",
            "Pin 4: OB2 (Coil B, wire 2)"]

MOT_GIMBAL_PINS = ["Pin 1: Phase 1", "Pin 2: Phase 2", "Pin 3: Phase 3",
                    "Pin 4: 75ohm/3W resistor to Pin 3 (gimbal 3-phase BLDC mode)"]

SEN_ENDSTOP_PINS = ["Pin 1: GND", "Pin 2: NC",
                     "Pin 3: Endstop switch  -  wire: active low", "Pin 4: NC"]

TOOLS = {}

def add(tid, name, specific, notes=None, expansion_note=None, extra_universal_desc_overrides=None):
    TOOLS[tid] = {
        "id": tid,
        "name": name,
        "specific": specific,
        "notes": notes or [],
        "expansion_note": expansion_note,
    }

# 0x00 Soldering Iron - datos exactos del ejemplo real del usuario
add(0x00, "T12 SOLDERING IRON", [
    ("CONN_T12", "T12 soldering cartridge",
     ["Pin 1: +24V  -  wire: red (convention)",
      "Pin 2: T12- (MOSFET control)",
      "Pin 3: NC  -  wire: no connection"]),
    ("CONN_MOT", "Solder wire feeder motor (NEMA stepper, forward/reverse)", mot_pins(None)),
], notes=["This tool's own generic endstop input on CONN_SEN is NOT available - PB3 (the endstop line) is the same physical pin this tool's own wire feeder needs as its STEP output, and the two can't share it at once. A deliberate trade-off, not an oversight - the endstop was already optional for this tool.",
          "Wire feeder position is open-loop (no encoder) - tracked as an estimate and persisted across power cycles. Reset it after loading a fresh spool (CAN 0x131) so it starts meaning something again - see CANBUS.TXT."])

# 0x01 Paste Dispenser
add(0x01, "SMT SOLDER PASTE DISPENSER", [
    ("CONN_MOT", "Dispenser feed motor (NEMA stepper)", mot_pins(None)),
])

# 0x02 Liquid Dispenser
add(0x02, "THERMAL PASTE / LIQUID DISPENSER", [
    ("CONN_MOT", "Dispenser feed motor (NEMA stepper)", mot_pins(None)),
])

# 0x03 Screwdriver
add(0x03, "SMART ELECTRIC SCREWDRIVER", [
    ("CONN_MOT", "Screwdriver drive motor (NEMA stepper)", mot_pins(None)),
])

# 0x04 Vacuum Pickup
add(0x04, "VACUUM / PNEUMATIC GRIPPER", [
    ("CONN_SEN", "Vacuum pressure sensor (TCRT5000-style analog + digital touch)",
     ["Pin 1: GND", "Pin 2: TCRT_A0 (analog pressure reading)",
      "Pin 3: TOUCH (digital, part-detected)", "Pin 4: +5V (sensor supply)"]),
    ("CONN_MOT", "Positioning motor (NEMA stepper)", mot_pins(None)),
])

# 0x05 Drill
add(0x05, "DRILL (BL4260)", [
    ("CONN_DRILL", "Drill motor - brake, tachometer, direction, PWM speed, power",
     ["Pin 1: ENABLE/BRAKE  -  own dedicated line",
      "Pin 2: FGIN (tachometer)",
      "Pin 3: FRIN (rotation direction)",
      "Pin 4: PWM (speed control)",
      "Pin 5: GND", "Pin 6: +24V"]),
    ("CONN_SEN", "Limit switch / endstop (optional)", SEN_ENDSTOP_PINS),
], notes=["Also covers micro-spindle depaneling - same hardware path, a different bit for a different job."])

# 0x06 Gripper Gimbal
add(0x06, "GIMBAL GRIPPER", [
    ("CONN_MOT", "3-phase brushless gimbal motor", MOT_GIMBAL_PINS),
])

# 0x07 Gripper NEMA
add(0x07, "NEMA GRIPPER", [
    ("CONN_MOT", "Heavy-duty clamping motor (NEMA stepper)", mot_pins(None)),
])

# 0x08 AOI Inspection
add(0x08, "AOI INSPECTION SYSTEM", [
    ("CONN_SEN", "Limit switch / endstop (optional)", SEN_ENDSTOP_PINS),
], notes=["This tool's own actual inspection light is CONN_LED2 above (universal connector) - it runs in synchronous stroboscopic mode here rather than the plain on/off status use other tools give it. Off/strobe/continuous timing is set over CAN - see CANBUS.TXT 0x150."])

# 0x09 Laser Engraver
add(0x09, "ENGRAVING LASER DIODE (10W)", [
    ("CONN_DRILL", "Laser diode - safety interlock and PWM power (shares drill's own PWM channel)",
     ["Pin 1: Safety interlock (locks down on comms loss)",
      "Pin 2: NC", "Pin 3: NC",
      "Pin 4: PWM (emission power)",
      "Pin 5: GND", "Pin 6: +24V"]),
    ("CONN_SEN", "Limit switch / endstop (optional)", SEN_ENDSTOP_PINS),
])

# 0x0A 3D Printer - datos exactos del ejemplo real del usuario
add(0x0A, "3D PRINTER (the busiest one)", [
    ("CONN_T12", "Hotend heater",
     ["Pin 1: +24V  -  wire: red (convention)", "Pin 2: MOSFET control", "Pin 3: NC"]),
    ("CONN_MOT", "Extruder motor (NEMA stepper)", mot_pins(None)),
    ("CONN_SEN", "Hotend's 100k NTC thermistor (NOT the same circuit as the T12)",
     ["Pin 1: GND (to NTC)", "Pin 2: NTC (to PB0)", "Pin 3: NC", "Pin 4: NC"]),
    ("CONN_DRILL", "LAYER fan (cools the printed part) - Delta EFB0424VHD-CP0",
     ["Pin 1: Extruder enable (do NOT connect here)",
      "Pin 2: FGIN tachometer  -  wire: blue",
      "Pin 3: NC",
      "Pin 4: PWM 25kHz  -  wire: yellow",
      "Pin 5: GND  -  wire: black",
      "Pin 6: +24V  -  wire: red"]),
    ("CONN_FAN1", "HOTEND fan (cools the heatsink/heat break) - CFM-4010 example",
     ["Pin 1: +5V  -  wire: red, per the CFM-40 example",
      "Pin 2: FG tachometer  -  wire: yellow, per the CFM-40 example",
      "Pin 3: PWM 25kHz  -  wire: blue, per the CFM-40 example",
      "Pin 4: GND  -  wire: black, per the CFM-40 example"]),
], notes=[
    "Two DIFFERENT fans: the one on CONN_DRILL cools the part (layer), the one on CONN_FAN1 cools the hotend itself. Don't mix them up.",
    "CONN_FAN1's manufacturer colors are only the Same Sky CFM-40 datasheet example - check your actual fan's colors, they vary.",
    "MS1/MS2 set the extruder's microstepping resolution - adjust to your preference.",
])

# 0x0B Scan Probe
add(0x0B, "3D SCANNER PROBE", [
    ("CONN_SEN", "Touch impact probe (max-priority interrupt input)",
     ["Pin 1: GND", "Pin 2: NC",
      "Pin 3: Probe trigger  -  wire: active low, EXTI3 max priority", "Pin 4: NC"]),
], notes=["Also covers metrology touch probing - same hardware path, a different physical probe on the same tool head."])

print(f"herramientas 0-11 definidas: {len(TOOLS)}")

# 0x0C SMT Pick & Place
add(0x0C, "SMT PICK & PLACE HEAD", [
    ("CONN_MOT", "Rotary A-axis motor (NEMA stepper, pad alignment)", mot_pins(None)),
])

# 0x0D Electromagnet
add(0x0D, "HEAVY-DUTY ELECTROMAGNET", [
    ("CONN_T12", "Electromagnet coil - plain GPIO on/off (NOT a thermal loop for this tool)",
     ["Pin 1: +24V  -  wire: red (convention)",
      "Pin 2: Coil on/off (repurposed T12 MOSFET control pin, plain GPIO not PWM)",
      "Pin 3: NC"]),
], notes=["This tool repurposes the T12 heater's own MOSFET control pin as a plain on/off GPIO driver - it never runs the thermal PID loop the way the soldering iron does."])

# 0x0E Spot Welder
add(0x0E, "SPOT WELDER HEAD", [
    ("CONN_T12", "Weld pulse driver (timed pulse, ~20ms resolution)",
     ["Pin 1: +24V  -  wire: red (convention)",
      "Pin 2: Pulse control (repurposed T12 MOSFET control pin)",
      "Pin 3: NC"]),
    ("CONN_SEN", "Surface-contact sensor (gates the pulse - refuses to fire unless HIGH)",
     ["Pin 1: GND", "Pin 2: NC",
      "Pin 3: Contact sensor  -  wire: active high, same input Vacuum Pickup uses", "Pin 4: NC"]),
], notes=["Refuses to fire unless the contact sensor reads HIGH first - \"ensure pressure before firing\" per this tool's own design brief.",
          "Real timing resolution is ~20ms (this project's own cyclic safety loop), not a true 1ms."])

# 0x0F Conformal Coating - no CAN handler, no physical connector on this board
add(0x0F, "CONFORMAL COATING AIRBRUSH", [], notes=[
    "This tool's own actuator (spray valve solenoid) and its own sensor live on the robot's own mainboard, outside this board's own scope. This tool's own ID exists purely for identification - there is no CAN handler for it, and no tool-specific connector on this board."])

# 0x10 Vacuum Gripper LG
add(0x10, "LARGE-FORMAT VACUUM GRIPPER", [
    ("CONN_MOT", "Positioning motor (NEMA stepper, multi-cup suction array)", mot_pins(None)),
])

# 0x11 Flying Probe - dual path, basic onboard + advanced via expansion
add(0x11, "FUNCTIONAL TESTING HEAD (FLYING PROBE)", [
    ("CONN_SEN", "Basic reading - shares the same onboard ADC channel as Vacuum Pickup",
     ["Pin 1: GND", "Pin 2: Analog reading (basic mode)", "Pin 3: NC", "Pin 4: +5V"]),
], expansion_note="Advanced reading needs an ADS1115 16-bit ADC on CONN_EXPANSION - either a Basic+ADS1115 board (direct connection) or either Advanced board variant (relayed through the expansion slave chip). See EXPANSION.TXT.")

# 0x12 UV Curing
add(0x12, "UV CURING HEAD", [
    ("CONN_DRILL", "UV LED driver - shares the drill/laser/3D-printer-fan's own PWM channel",
     ["Pin 1: NC (this tool's own mode doesn't use ENABLE/BRAKE)",
      "Pin 2: NC", "Pin 3: NC",
      "Pin 4: PWM (LED driver duty cycle, 0-255)",
      "Pin 5: GND", "Pin 6: +24V"]),
])

# 0x13 Hot Air Rework
add(0x13, "HOT AIR REWORK NOZZLE", [
    ("CONN_T12", "Heating element - shares the soldering iron's own exact thermal loop",
     ["Pin 1: +24V  -  wire: red (convention)",
      "Pin 2: MOSFET control (same thermal PID loop as the soldering iron)",
      "Pin 3: NC"]),
    ("CONN_DRILL", "Air turbine blower - shares the UV/drill/laser's own PWM channel",
     ["Pin 1: NC", "Pin 2: NC", "Pin 3: NC",
      "Pin 4: PWM (blower duty cycle, 0-255)",
      "Pin 5: GND", "Pin 6: +24V"]),
], notes=["Shares the exact same physical T12 heater+thermocouple mechanism as the Soldering Iron - same 445C ceiling, stuck-heater detection, and comms-loss shutdown all apply unchanged."])

# 0x14 Press-Fit Inserter - no CAN handler, no physical connector on this board
add(0x14, "PNEUMATIC PRESS-FIT INSERTER", [], notes=[
    "This tool's own actuator (linear press-fit cylinder) and its own sensor live on the robot's own mainboard, outside this board's own scope. This tool's own ID exists purely for identification - there is no CAN handler for it, and no tool-specific connector on this board."])

# 0x15 Crimping Actuator - entirely expansion-driven
add(0x15, "WIRE HARNESSING / CRIMPING ACTUATOR", [],
    expansion_note="No main-board connector for the actuator itself - driven entirely off an expansion board's own driver (EXP_TMC_STEP/DIR/EN on CONN_EXPANSION). Requires a Basic or Advanced expansion board variant with its own driver (TMC2209 or TMC5160A) populated. See EXPANSION.TXT.")

# 0x16 Thermal Inspection
add(0x16, "PCB ADVANCED INSPECTION (THERMAL)", [], notes=[
    "Illumination is CONN_LED2 above (universal connector) - ring light only, not the thermal sensor itself.",
    "Also covers micro-spindle depaneling via the shared drill hardware path."],
   expansion_note="The thermal sensor itself needs an expansion board on CONN_EXPANSION - either a Basic+MLX9064x board (direct connection) or either Advanced board variant (relayed through the expansion slave chip). Which of the 3 MLX9064x-family sensors is populated is set separately over CAN (0x1A6/0x1A7). See EXPANSION.TXT.")

# 0x17 Paste Jetting - entirely expansion-driven (local PWM at the tool head)
add(0x17, "SOLDER PASTE JETTING VALVE", [],
    expansion_note="Needs local PWM generated at the tool head itself, not routed over a cable from the main board - requires an Advanced expansion board variant, whose own slave chip generates this PWM locally on CONN_EXPANSION. See EXPANSION.TXT.")

# 0x18 Ultrasonic Welder
add(0x18, "ULTRASONIC WELDER / PACKAGING SEALER", [
    ("CONN_T12", "Weld pulse driver (timed pulse, same shared mechanism as Spot Welder)",
     ["Pin 1: +24V  -  wire: red (convention)",
      "Pin 2: Pulse control (repurposed T12 MOSFET control pin)",
      "Pin 3: NC"]),
], notes=["Same shared pulse-timing mechanism as the Spot Welder - only one of the two tools is ever active at once.",
          "Real timing resolution is ~20ms (this project's own cyclic safety loop), not a true 1ms."])

print(f"Total herramientas definidas: {len(TOOLS)}")
assert len(TOOLS) == 25, f"Esperaba 25, hay {len(TOOLS)}"
assert sorted(TOOLS.keys()) == list(range(25)), "Faltan IDs"
print("Todos los 25 IDs (0x00-0x18) presentes y verificados")
