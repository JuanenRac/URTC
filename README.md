<p align="center">
  <img src="images/URTC_LOGO.svg" alt="URTC Logo" width="100%">
</p>

# 🚀 URTC — Universal Robot Tool Controller (v1.1)

> **⚠️ Safety notice:** this board drives a **10W engraving laser diode** and multiple heater stages (T12 soldering iron cartridge, 3D printer hotend). Building and using it means working with equipment that can cause **burns, fire, or eye damage** if assembled or operated without proper safety measures (laser goggles rated for the diode's wavelength, thermal protection, an accessible power cutoff). This is a hobbyist/maker project shared as-is — build and use at your own risk, and don't skip basic safety practice just because the firmware has watchdogs.

Hi everyone! I wanted to share a project I've been developing called URTC (Universal Robot Tool Controller). It is a monolithic, highly integrated control board designed specifically to expand the capabilities of robotic arms and automation setups, making it a perfect match for platforms like PAROL6 and Faze4 — two open-source robotic arms designed and developed by [Source-Robotics](https://source-robotics.com/) ([GitHub](https://github.com/Source-Robotics)).

**URTC is an independent, unofficial project.** It isn't developed or endorsed by Source-Robotics — it's a compatible tool-head controller built to work well with PAROL6 and Faze4, and the same CAN-based architecture is open to adapting for other robotic arm platforms too.

Here is the complete breakdown of what it is, what it does, and the hardware ecosystem it currently manages.

**Status: 🚧 Actively evolving project — no Release yet.** URTC is under continuous, active development on both fronts at once: firmware (new tool profiles, the expansion slave ecosystem, protocol changes) and hardware (schematic and BOM still being finalized, no populated board exists yet). Because both sides keep moving together, what's in this repository at any given moment is a snapshot of ongoing work, not a stable, versioned product — file names, folder structure, tool counts, and documentation can all still change as the design settles. Once both firmware and hardware reach a genuinely stable, verified-on-real-hardware state, a proper **Release** will be tagged bundling everything together (firmware, bootloader, PC tools, hardware design files, and documentation) as a coherent, frozen snapshot. Until then, treat `main` as the actively-moving target it is.

---

## ⚙️ What is URTC?

URTC is an all-in-one, compact control board powered by an STM32 microcontroller (STM32F303CCT6, LQFP48). It communicates with the main robot controller via CAN bus, allowing for real-time, low-latency execution of complex tasks right at the tool head or axis. It features an onboard OLED display for instant diagnostics — animated boot splash, per-tool animated icons, live telemetry on a two-tone panel — a single-pixel RGB status LED plus an addressable RGB LED ring for camera illumination, a 20-pin expansion connector for add-on boards, an onboard F-RAM that persists the active tool's setpoints across a power loss, and dedicated analog and high-current power stages.

## 🛠️ Scalable Architecture & Tool Matrix

The core strength of URTC is its extreme versatility. Instead of swapping out electronics for every different job, the board features a scalable matrix architecture:

* **32-Address Identification Scheme:** the hardware and communication protocol are designed to identify up to 32 different tools or end-effectors directly at the robot head, via a 5-bit solder-jumper ID matrix (ID0-ID4). Of those 32 readings, 31 map directly to a tool profile; the 32nd (all 5 jumpers installed, `11111`) is reserved as a "free configuration" address instead - see below.
* **25 Plug-and-Play Automated Profiles:** the firmware natively handles 25 tool profiles - the board reads the physical identity of the tool head and configures the power stages, sensors, and logic switching seamlessly without needing a full re-flash. 6 more addresses remain free within the existing scheme for future tool profiles.
* **Free tool configuration:** the reserved `11111` jumper reading doesn't pick a fixed tool - it tells the board to look up which tool to use from a register in its own persistent F-RAM instead, set ahead of time over CAN (via `URTC Flasher`). Useful for a board that needs reprogramming to a different tool without physically re-soldering jumpers. See `docs/EEPROM.TXT` section 5 for the full mechanism.

## 🔌 Hardware Flexibility & Motor Support

To handle such a wide variety of applications, the URTC hardware is fully equipped to control:

* **NEMA Stepper Motors:** NEMA 8, 11, 14, and 17 run directly off the onboard TMC2209, same as NEMA 23 and 34 — up to **2.0A** on any of them via the main board's driver stage. For NEMA 23/34 at their full rated torque, a TMC5160 on the expansion connector (see below) supports up to **10A**, current-scaled by the external MOSFETs/sense resistor chosen for that board — the onboard 2.0A limit doesn't apply once a motor's moved to the expansion driver.
* **3-Phase BLDC / Gimbal Motors** for high-precision movement.
* **Motors with Hall sensors and tachometers** for closed-loop control.
* **Dedicated inputs** for reflective optical proximity sensors like the TCRT5000, plus a generic active-low endstop/limit-switch input shared across four tool profiles.

## 🧩 Expansion Connector

A 20-pin header, separate from the tool-specific connectors, for add-on boards that need more than what a given tool profile alone exposes — an extra stepper axis (TMC2209 or TMC5160), a second sensor board, that kind of thing.

| Pins | Signal |
|---|---|
| 4 | 24V |
| 1 | 3.3V |
| 1 | 5V |
| 3 | GND |
| 2 | Bit-banged I2C (SCL/SDA) — its own bus, separate from the OLED/F-RAM's hardware I2C2 |
| 3 | STEP/DIR/EN — universal to either driver chip below |
| 4 | Bit-banged SPI (CS/SCK/MISO/MOSI) — for a TMC5160's configuration/diagnostics interface, or any other SPI-configurable chip |
| 1 | General-purpose GPIO (EXTI-capable interrupt input if a future add-on needs a fast sensor response, e.g. an endstop) |
| 1 | TMC5160 DIAG0 (stall/fault diagnostic line, polled via `0x182`/`0x183`) |

20 pins total.

**Two separate I2C buses on purpose:** the OLED/F-RAM use this chip's one usable hardware I2C peripheral (I2C2, on PA9/PA10); the expansion connector gets its own, independent bit-banged I2C bus (PB10/PB11 - this chip's only other I2C-capable pin pairs were already committed to other functions, so bit-banging was the way to give this connector its own bus without a hardware conflict). Anything hanging off the expansion header — an I2C ADC/DAC, a port expander, whatever a given add-on board needs — shares this bit-banged bus with any other expansion-side I2C device, but can't stretch the clock or otherwise interfere with the OLED's own timing on its separate, hardware I2C2 bus.

**A TMC2209 or a TMC5160, not necessarily both.** Both chips use the same STEP/DIR/EN interface for actual motion, so that part is universal. Where they differ is configuration/diagnostics: a TMC2209 uses its own single-wire UART for that, while a TMC5160 uses SPI — and since the two are mutually exclusive on any given expansion board, the 4 SPI pins double as a natural home for a TMC2209's single UART line too, rather than needing yet another dedicated pin nobody uses at the same time as the SPI bus. The bit-banged SPI bus talks the exact protocol a TMC5160 expects (SPI Mode 3, MSB first, CS held low for the whole transaction — see `docs/CANBUS.TXT`'s `0x180`/`0x181` for the generic byte-passthrough command that drives it) rather than this firmware needing to know that chip's specific register layout. A TMC5160's DIAG0 stall/fault line is wired too (`0x182`/`0x183`) — it reuses one of the two general-purpose GPIO pins, which were already earmarked for exactly this kind of fast interrupt-driven input.

Full pin-by-pin detail — which MCU pin backs which signal, and the reasoning behind a couple of layout constraints this chip's 48-pin package has — lives in `docs/PINOUT_CONNECTORS.TXT` and `src/F303-master/README.md`.

### The 6 expansion board variants

4 of the 6 expansion board variants carry a stepper driver — either a TMC2209 (up to 2A/coil, integrated power MOSFETs) or a TMC5160A (up to 10A+/coil, needs 8 external power MOSFETs the driver itself doesn't include). Independent of that driver choice, a driver-carrying board is either **basic** (driver + connectors only, no MCU — STEP/DIR/EN routed straight from the main board) or **advanced** (adds a second microcontroller, STM32F303CBT6, plus 2 local sensor chips — an ADS1115 16-bit ADC and an MLX9064x-family thermal camera — and local PWM generation for tools whose timing needs generating right at the tool head rather than routed over a cable). 2×2 combinations, plus 2 more sensor-only basic boards (ADS1115 or MLX9064x, wired directly to the main board's own STM32F303CC, no driver and no slave MCU) for a tool that only needs one of those 2 chips and nothing else an advanced board also carries — 6 boards total — see `BOM/BOM_EXPANSION_*.TXT` (6 files), `docs/EXPANSION.TXT`, and `docs/PINOUT_SLAVE.txt`.

The advanced variant's own STM32F303CBT6 talks to the main board over the expansion connector's existing bit-banged I2C bus above — main board as master, slave chip answering as a real hardware I2C slave — and drives its own second, local-only I2C bus for the 2 sensor chips. It has its own bootloader and application firmware, updated the same way the main board is (CAN-OTA from `URTC Flasher`), just relayed across that I2C link rather than reaching the slave chip directly. See `src/F303-slave/README.md` and `src/F303-slave/boot/README.md` for the full technical detail.

## 💾 Parameter Persistence

An onboard FM24CL64B F-RAM (64Kbit, I2C) keeps a periodically-updated snapshot of the active tool's setpoints and the global LED/OLED settings, so a sudden power loss doesn't leave "what was this board doing" as unknowable as the loss itself was unplanned. It shares the OLED's hardware I2C2 bus rather than getting one of its own — this MCU only has one usable hardware I2C peripheral for this purpose, already spoken for by the OLED (see `src/F303-master/README.md` section 6 for the full reasoning).

**Recovered state is queryable, never auto-applied to anything hazardous.** On boot, whatever was saved becomes readable over CAN (`0x190`/`0x191`) — but a heater setpoint, laser power, or motor command is never silently re-armed on its own. Only the safe, passive settings (LED colors, OLED mode) get restored directly. Deliberately re-sending a setpoint after actually reviewing what happened is left as the master controller's call, not something this board decides by itself the instant power comes back.

## 💼 Natively Automated Tool Catalog (25 Firmware Profiles)

Through its dynamic switching logic, the firmware natively manages the following tool heads:

1. **Soldering Station (T12):** precise PID temperature control using direct ADC feedback to handle standard T12 soldering tips. Generic endstop input available. [Jumper/wiring config →](images/TOOL_SOLDERING_IRON.png)
2. **SMT Solder Paste Dispenser:** millimetric feed control for precise solder paste deposition on PCBs. [Jumper/wiring config →](images/TOOL_PASTE_DISPENSER.png)
3. **Thermal Paste / Liquid Dispenser:** fluidity management for high-viscosity pastes or liquid adhesives. [Jumper/wiring config →](images/TOOL_LIQUID_DISPENSER.png)
4. **Smart Electric Screwdriver:** rotation and stop control based on torque limits or end-stops. [Jumper/wiring config →](images/TOOL_SCREWDRIVER.png)
5. **Vacuum / Pneumatic Gripper:** vacuum pump control and pressure level reading for safe Pick-and-Place operations. [Jumper/wiring config →](images/TOOL_VACUUM_PICKUP.png)
6. **Drill (BL4260):** PWM speed control, direction switching, and dynamic electric braking with real-time RPM readings, on its own dedicated enable/brake line, independent from the stepper-tool driver enable. Generic endstop input available. [Jumper/wiring config →](images/TOOL_DRILL.png)
7. **Gimbal Gripper:** high-sensitivity manipulation using 3-phase brushless gimbal motors. [Jumper/wiring config →](images/TOOL_GRIPPER_GIMBAL.png)
8. **NEMA Gripper:** robust clamping force controlled via a heavy-duty stepper motor. [Jumper/wiring config →](images/TOOL_GRIPPER_NEMA.png)
9. **AOI (Automated Optical Inspection) System:** synchronous stroboscopic control of the LED lighting array for machine vision camera capture. Generic endstop input available. [Jumper/wiring config →](images/TOOL_AOI_INSPECTION.png)
10. **Engraving Laser Diode (10W optical):** PWM beam power modulation with a safety hardware loop (CAN watchdog) that locks down if host communication is lost. Generic endstop input available. [Jumper/wiring config →](images/TOOL_LASER_ENGRAVER.png)
11. **3D Printing Hotend:** PID control of the heater cartridge, NTC thermistor reading, extruder control, and a dedicated 25kHz PWM-controlled layer cooling fan (4-wire, tachometer feedback, own communication watchdog) — all integrated into a single block. [Jumper/wiring config →](images/TOOL_3D_PRINTER.png)
12. **3D Scanner Probe:** ultra-fast hardware interrupt input (EXTI) with absolute priority for real-time surface digitization and impact sensing without lag. Also covers metrology touch probing - the same hardware path, a different physical probe on the same tool head. [Jumper/wiring config →](images/TOOL_SCAN_PROBE.png)
13. **SMT Pick & Place Head:** rotary A-axis for correct pad alignment, on the same stepper interface as the paste/liquid dispensers and both grippers above. [Jumper/wiring config →](images/TOOL_SMT_PICKPLACE.png)
14. **Heavy-Duty Electromagnet:** on/off pickup control for ferromagnetic parts, off the T12 heater output repurposed as a generic GPIO driver. [Jumper/wiring config →](images/TOOL_ELECTROMAGNET.png)
15. **Spot Welder Head:** millisecond-precise weld pulses for battery-pack nickel strips, with a surface-contact sensor gating the pulse. [Jumper/wiring config →](images/TOOL_SPOT_WELDER.png)
16. **Conformal Coating Airbrush:** protective coating spray control for finished PCBs - the spray valve and its own sensor live on the robot's own mainboard, outside this board's own scope. [Jumper/wiring config →](images/TOOL_CONFORMAL_COATING.png)
17. **Large-Format Vacuum Gripper:** multi-cup suction array for unpopulated FR4 boards, on the same stepper interface as tool #13 above. [Jumper/wiring config →](images/TOOL_VACUUM_GRIPPER_LG.png)
18. **Functional Testing Head:** flying-probe voltage/continuity testing — basic reading off the onboard ADC, advanced reading via an ADS1115 16-bit ADC on an **advanced** expansion board. [Jumper/wiring config →](images/TOOL_FLYING_PROBE.png)
19. **UV Curing Head:** high-power UV LED driver for instant glue/mask curing. [Jumper/wiring config →](images/TOOL_UV_CURING.png)
20. **Hot Air Rework Nozzle:** heating element, turbine blower, and thermocouple feedback for reflowing misaligned SMD parts - shares the soldering iron's own thermal control loop. [Jumper/wiring config →](images/TOOL_HOTAIR_REWORK.png)
21. **Pneumatic Press-Fit Inserter:** linear actuator control for pressing connectors into PCBs - the actuator and its own sensor live on the robot's own mainboard, outside this board's own scope. [Jumper/wiring config →](images/TOOL_PRESSFIT_INSERTER.png)
22. **Wire Harnessing / Crimping Actuator:** high-torque jaw for stripping/crimping terminals, driven off an **expansion board's own driver** rather than the main board's. [Jumper/wiring config →](images/TOOL_CRIMPING_ACTUATOR.png)
23. **PCB Advanced Inspection:** thermal imaging (MLX9064x-family array - all 3 family members, MLX90640/MLX90641/MLX90642, supported today, either via an **advanced** expansion board's own slave chip or a **basic** MLX9064x expansion board wired directly to the main board) to spot shorts by temperature signature, alongside ring-LED illumination. Also covers micro-spindle depaneling - the same drill hardware path above, a different bit for a different job. [Jumper/wiring config →](images/TOOL_THERMAL_INSPECTION.png)
24. **Solder Paste Jetting Valve:** piezoelectric micro-droplet dispensing, sub-millisecond pulse precision generated locally on an **advanced** expansion board. [Jumper/wiring config →](images/TOOL_PASTE_JETTING.png)
25. **Ultrasonic Welder / Packaging Sealer:** high-frequency transducer trigger for plastic enclosure welding. [Jumper/wiring config →](images/TOOL_ULTRASONIC_WELDER.png)

*(Tool config images exist for tools 1-12; images for tools 13-25 will populate as the hardware documentation catches up — filenames above match the naming convention already in use for `images/`.)*

## 🖥️ Local OLED Interface

Every tool head shows live, tool-specific telemetry on a 128×64 two-tone OLED: an animated boot splash on power-up, a blinking CAN-activity indicator, a live "hero" reading in the top strip (temperature, RPM, power — whatever matters most for the active tool), and a small four-frame animated icon per tool profile.

### The module

Both physical variants below are the same panel electrically (SSD1306 or SSD1315-driven — the firmware's init sequence is verified compatible with both, see `OLED_Init()` in `firmware_oled_driver.c`; the SSD1315 is a newer, drop-in replacement controller that many modules ship with today under the same "SSD1306" listing/silkscreen), **128×64**, and the same two-tone "yellow/blue" split, where the physical LED material itself is divided into two fixed-color zones (this isn't software-selectable):

* **Top 16 pixels (pages 0-1): yellow.** URTC uses this strip for whatever's most useful to see at a glance without reading closely — the CAN-activity indicator, live hero readings, or (on the boot splash / invalid-tool screens) short status text.
* **Bottom 48 pixels (pages 2-7): blue.** Everything else — tool icons, detailed telemetry, the animated JuanenBOT face on the splash screen, the big blinking ERROR wordmark.

Both land on the same I2C2 bus and the same `OLED_Init()` — the firmware can't tell which of the two is attached, and doesn't need to. They're mutually exclusive on a given board (see `BOM/BOM.TXT`'s `CONN_OLED2` note - this document's name for what the schematic calls `LCD1`).

#### Option A — direct mount (`CONN_OLED2`, the footprint actually populated on the board)

<img src="images/OLED_DIRECT_MOUNT.jpg" width="220">

A bare panel with no separate breakout PCB — just the glass and its 30-pin FPC ribbon, soldered straight into the `CONN_OLED2` footprint (`FPC30`, WiseChip UG-2864, this document's name for what the schematic calls `LCD1` — see `BOM/BOM.TXT` and `URTC_NETLIST.TXT`). Of the 30 pins, only a subset is actually wired — the rest is the panel's parallel-interface bus (`D2`–`D7`, `RW`, `E/!RD`), left unconnected since the board only ever talks to it over I2C:

| CONN_OLED2 pin(s) | Net | Function |
|---|---|---|
| 1, 8, 29, 30 | GND / AGND | Ground |
| 9 | VDD | Logic supply (from `+3V3B`, the OLED-only rail — see BOM §1) |
| 28 | VCC | Panel supply |
| 2–5 | C2P/C2N/C1P/C1N | Charge-pump caps — `C26`/`C27` in the BOM |
| 26 | IREF | Reference-current set resistor |
| 27 | VCOMH | Internal common-voltage decoupling |
| 10, 12 | BS0, BS2 | Tied to GND |
| 11 | BS1 | Tied to `+3V3B` |
| 18 | D0/SCK | I2C2 SCL — PA9 |
| 19 | D1/DIN/SDA | I2C2 SDA — PA10 |

`BS0`/`BS1`/`BS2` are the panel's own interface-select strap (GND/VCC/GND here), fixed in hardware rather than exposed to the MCU — this is what puts the controller in I2C mode in the first place, rather than the 8080/6800 parallel mode the other 22 FPC pins belong to.

#### Option B — breakout module (`CONN_OLED`, external alternative)

<img src="images/OLED_BREAKOUT_MODULE.jpg" width="220">

The same panel pre-mounted on a small carrier board with a 4-pin header — useful if you'd rather wire an off-the-shelf module than source the bare FPC panel. Wired straight to `CONN_OLED` with no crossing needed — the module's own pin order (`GND · VDD · SCK · SDA`) matches `CONN_OLED`'s pinout exactly, pin for pin:

| OLED module pin | CONN_OLED pin | Signal |
|---|---|---|
| GND | 1 | Ground |
| VDD | 2 | +3.3V (display logic power) |
| SCK | 3 | SCL — PA9, hardware I2C2 |
| SDA | 4 | SDA — PA10, hardware I2C2 |

### Boot splash

<img src="ani/splash_boot.gif" width="480">


### Tool icons (one per profile, 4-frame animation)

<table>
<tr>
<td align="center"><img src="ani/00_soldering_iron.gif" width="80"><br>T12 Soldering Iron</td>
<td align="center"><img src="ani/01_paste_dispenser.gif" width="80"><br>Paste Dispenser</td>
<td align="center"><img src="ani/02_liquid_dispenser.gif" width="80"><br>Liquid Dispenser</td>
<td align="center"><img src="ani/03_screwdriver.gif" width="80"><br>Screwdriver</td>
</tr>
<tr>
<td align="center"><img src="ani/04_vacuum_pickup.gif" width="80"><br>Vacuum Pickup</td>
<td align="center"><img src="ani/05_drill.gif" width="80"><br>Drill (BL4260)</td>
<td align="center"><img src="ani/06_gripper_gimbal.gif" width="80"><br>Gimbal Gripper</td>
<td align="center"><img src="ani/07_gripper_nema.gif" width="80"><br>NEMA Gripper</td>
</tr>
<tr>
<td align="center"><img src="ani/08_aoi_inspection.gif" width="80"><br>AOI Inspection</td>
<td align="center"><img src="ani/09_laser_engraver.gif" width="80"><br>Laser Engraver</td>
<td align="center"><img src="ani/10_3d_printer.gif" width="80"><br>3D Printer Hotend</td>
<td align="center"><img src="ani/11_scan_probe.gif" width="80"><br>3D Scanner Probe</td>
</tr>
<tr>
<td align="center"><img src="ani/12_smt_pickplace.gif" width="80"><br>SMT Pick & Place</td>
<td align="center"><img src="ani/13_electromagnet.gif" width="80"><br>Electromagnet</td>
<td align="center"><img src="ani/14_spot_welder.gif" width="80"><br>Spot Welder</td>
<td align="center"><img src="ani/15_conformal_coating.gif" width="80"><br>Conformal Coating</td>
</tr>
<tr>
<td align="center"><img src="ani/16_vacuum_gripper_lg.gif" width="80"><br>Vacuum Gripper (LG)</td>
<td align="center"><img src="ani/17_flying_probe.gif" width="80"><br>Flying Probe</td>
<td align="center"><img src="ani/18_uv_curing.gif" width="80"><br>UV Curing</td>
<td align="center"><img src="ani/19_hotair_rework.gif" width="80"><br>Hot Air Rework</td>
</tr>
<tr>
<td align="center"><img src="ani/20_pressfit_inserter.gif" width="80"><br>Press-Fit Inserter</td>
<td align="center"><img src="ani/21_crimping_actuator.gif" width="80"><br>Crimping Actuator</td>
<td align="center"><img src="ani/22_thermal_inspection.gif" width="80"><br>Thermal Inspection</td>
<td align="center"><img src="ani/23_paste_jetting.gif" width="80"><br>Paste Jetting</td>
</tr>
<tr>
<td align="center"><img src="ani/24_ultrasonic_welder.gif" width="80"><br>Ultrasonic Welder</td>
</tr>
</table>


### Invalid tool ID warning

If the ID jumpers don't match any of the 25 assigned profiles, the board blocks every actuator and blinks this instead:

<img src="ani/error_warning.gif" width="480">

All animation source GIFs live in [`/ani`](ani/).

## 🔴🟢🔵 Digital Status LED

Separate from the OLED and the 8-pixel illumination ring, `CONN_LED1` carries a single addressable RGB LED (WS2812B-family, SPI/DMA-driven) dedicated to at-a-glance status.

**Automatic by default, host-overridable on demand.** The firmware colors this LED on its own, three-way priority:

* 🔴 **Red** — a hardware fault is active (`system_error_flag`). Always wins, regardless of anything else going on.
* 🔵 **Blue** — the board is actively functioning: a CAN frame (any ID) arrived within the last 1.5 seconds.
* 🟢 **Green** — idle, waiting for commands: no CAN traffic in over 1.5 seconds.

The master can still override this at any point by sending CAN ID `0x100` (DLC 8) with the red, green, and blue intensity as the first three bytes (0-255 each — full 24-bit color, not just the three automatic ones). A host-sent color holds for 10 seconds before falling back to the automatic scheme — long enough to actually be seen, short enough that the board doesn't get stuck showing a stale custom color if the host stops updating it. Sending `0x100` again (whether the same color or a new one) refreshes that 10-second window, so a host that wants to keep custom control just needs to keep sending it periodically. A hardware fault always interrupts an active override — red takes priority over any color the host had set.

See `docs/CANBUS.TXT` (ID `0x100`) for the exact byte layout, which also shares this same message with the ring LED and OLED night-mode control.

## 📸 Photos

![URTC v1.0](images/URTC_BOARD.png)

*(Work in progress — more angles and a populated board coming soon.)*

## 🔧 Building & Flashing

URTC's flash is split into two independent pieces, so the board can be reflashed over the same CAN umbilical it already uses for everything else — without ever needing physical access to the JTAG/SWD header again after the first setup.

### Flash memory layout (256K total, golden-image / A-B update model)

```
0x08000000 ┌─────────────────────────────────┐
           │  Bootloader (30K)                 │  Always runs first on every boot.
           │                                   │  Listens briefly on CAN, then either
           │                                   │  jumps to the app or waits for an
           │                                   │  update. Drives the OLED directly
           │                                   │  during an update (see below).
0x08007800 ├─────────────────────────────────┤
           │  Metadata page (2K)               │  Describes whatever's in the main
           │                                   │  slot right now: HardwareID,
           │                                   │  version, size, CRC32, and an
           │                                   │  HMAC-SHA256 signature. The
           │                                   │  bootloader checks all of it
           │                                   │  before ever jumping to the app.
0x08008000 ├─────────────────────────────────┤
           │  Main slot (112K)                 │  This is the application firmware /
           │                                   │  URTC_V1.1_F303CC.* — the actual
           │                                   │  firmware that runs day to day,
           │                                   │  described everywhere else in
           │                                   │  this README. Never touched by
           │                                   │  an update until a verified,
           │                                   │  known-good image is ready to
           │                                   │  replace it.
0x08024000 ├─────────────────────────────────┤
           │  Backup / staging slot (112K)     │  Raw storage only, never
           │                                   │  directly executed. Every CAN
           │                                   │  update writes here first.
0x08040000 └─────────────────────────────────┘
```

**Why a backup slot.** A CAN update is never written into the slot that's currently running. It goes into backup first, gets fully verified there — size, CRC32, and an HMAC-SHA256 signature proving it actually came from this project's own build process, not just that it arrived intact — and only then gets copied into the main slot. A power loss at any point before that copy starts leaves the currently running firmware completely untouched, so there's no window where an interrupted download can brick the board. If the power loss happens *during* the copy itself, the bootloader notices on the next boot (backup, never touched during the copy, is still fully intact) and simply resumes copying from it until it succeeds.

### 1. First-time setup — requires JTAG/SWD (once)

The bootloader can only get onto the chip via physical programming — there's no way to CAN-flash a board that doesn't have a bootloader on it yet. This is a one-time step:

1. Open the project in **STM32CubeIDE** (built and tested against the STM32F303CC target), or use **STM32CubeProgrammer** directly with the compiled outputs below.
2. Flash **both** images over SWD (ST-Link) via the onboard `STM_JTAG` header — each `.hex` file has its target address baked in, so most tools (including STM32CubeProgrammer) can load both in the same session:
   * `URTC_BOOTLOADER.hex` → `0x08000000`
   * `URTC_V1.1_F303CC.hex` → `0x08008000`
3. Set the tool identity via the ID solder jumpers before powering up — the board reads them once at boot, same as always. Five jumpers (ID0-ID4), covering the full 32-address space (31 direct tool addresses, plus the reserved `11111` free-configuration address - see the Tool Matrix section above).
4. Power up. The bootloader listens for ~600ms, sees nothing, and jumps straight into the application — from here on, everything behaves exactly as described in the rest of this README.

**The JTAG header is never removed or disabled.** It's always there as a fallback — if a CAN update ever goes wrong, or you just prefer it, you can reflash either image over SWD at any time.

**Two onboard pushbuttons, BOOT and RESET**, are also there for recovery — RESET is an ordinary hardware reset (`NRST`), and BOOT pulls `BOOT0` high, which is a chip-level decision made *before* anything in this repository runs at all: normally (not held) the chip boots from flash into this project's own bootloader as described above; held at reset, it boots into ST's own factory System Memory bootloader instead (USB DFU/UART recovery, entirely separate from anything here). See `src/F303-master/README.md` section 4a for the full technical detail.

### 2. Subsequent updates — over CAN bus

Once the bootloader is in place, updating the application no longer needs physical access to the board at all — just send the new firmware build over the same umbilical CAN line already carrying commands to the tool head.

**The update sequence:**

1. **Trigger.** The master sends `0x7F0` (DLC 4, payload `B0 07 1D 5A`) to the *running application*. It safely cuts power to every actuator inline — motors, heaters, laser — and resets the chip. This magic-payload requirement means a corrupted or malformed frame can't accidentally trigger a reset into update mode.
2. **Start.** After reset, the bootloader is listening. The master sends `0x7F1` (DLC 8, big-endian total firmware size + big-endian HardwareID). An image built for different hardware is rejected right here, before a single byte of flash gets touched. The bootloader erases exactly as many backup-slot pages as the new image needs and replies with a status frame (`0x7F5`).
3. **Signature.** The master sends the expected HMAC-SHA256 signature as four `0x7F7` frames (8 bytes each, in order) — computed over the firmware image with a key shared between the bootloader and whatever tool signs the build.
4. **Data.** The master streams the `.bin` file as a sequence of `0x7F2` frames (up to 8 bytes of raw firmware data each), sent back-to-back — CAN guarantees frames arrive in the order they were sent on a single bus, so no per-frame sequence number is needed. The bootloader buffers incoming bytes into a 2KB page in RAM and writes it to the *backup* slot once full, reading every half-word back and comparing it against what was meant to be written before considering the page done, and sending a `0x7F3` acknowledgement (with the page index) after each verified write. A reasonable master implementation waits for each page's ACK before sending the next page's worth of data, to avoid overrunning the bootloader's receive buffer.
5. **End & verify.** Once every byte has been sent, the master sends `0x7F4` (DLC 8, big-endian CRC32 + version major/minor). The bootloader checks the backup slot's size, computes its CRC32 and HMAC-SHA256 and compares both against what the master declared. Only if everything matches does it proceed to copy backup into the main slot, page by page, with the same read-back verification as above. Once that copy is complete and confirmed, it saves the new metadata and resets into the updated application. On any mismatch — size, CRC32, HMAC, or HardwareID — the main slot is never touched at all, and the bootloader just goes back to listening for a fresh attempt.

**Status frames (`0x7F5`, DLC 1):** `0x01` listening, `0x02` erasing, `0x03` receiving, `0x06` verifying, `0x07` copying backup into main, `0x04` verified OK (about to jump), `0x05` verification failed, `0xFF` error.

**Heartbeat (`0x7F6`, DLC 2, every ~1s while listening or updating):** status byte + progress percent (0-100, or `0xFF` where a percentage doesn't apply). Lets the master tell "node is alive but hasn't started listening yet" apart from "node is completely unresponsive" - useful for automated bring-up and for spotting a stuck bootloader without waiting for a timeout.

**On-screen progress.** The bootloader drives the OLED directly during an update — nobody has to guess whether anything is happening. It shows "UPDATING" plus a live progress bar and percentage while pages are being written or copied, "FLASH OK" for a beat before it resets into the new firmware, and "ERROR" if a page write fails, the transfer stalls for more than 10 seconds, or verification comes back with a mismatch.

**⚠️ Bench-test this before trusting it in the field.** The protocol above compiles and links clean and the logic has been reasoned through carefully, but a bootloader is exactly the kind of firmware where "builds correctly" is a long way from "trustworthy on hardware" — the real flash-programming timing, CAN behavior across a multi-thousand-frame transfer, and the bootloader-to-application handoff all need to be verified on an actual board (ideally with JTAG on hand as a fallback) before relying on this for an unattended update with real actuators connected.

### PC Tools

Two standalone, cross-platform (Windows/Linux) GUI tools support this
board - **URTC Flasher** (CAN-OTA and full-chip SWD/JTAG updates, for
both this board and, on an Advanced expansion variant, its own
expansion slave chip) and **URTC Tester** (a live CAN-bus exerciser
showing whichever tool profile is currently jumpered). Both used to
live inside this repository under `tools/`; each is now its own
independent project, with its own README, license, and translations:

- [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)
- [URTC Tester](https://github.com/JuanenRac/URTC-TESTER)

A web-based alternative covering similar ground (live monitoring, CAN
analysis, OTA flashing, thermal inspection) without installing anything
locally also exists: [URTC Web Studio](https://github.com/JuanenRac/URTC-WEB-STUDIO).

## 📋 Changelog

Firmware and bootloader are versioned and released independently -
flashing a new bootloader doesn't imply a new application version and
vice versa, so each gets its own history here rather than one combined
version number that would imply they always move together.

### Firmware (`src/F303-master/`)

| Version | Notes |
|---|---|
| **1.1** | Project-wide version migration: both PC tools and this firmware/bootloader's own source folder move from `V1.0/` to `V1.1/` (`tools/flasher/`, `tools/tester/`, `src/F303-master/`). Compiled firmware binaries renamed to match (`URTC_V1.1_F303CC.bin/.hex/.elf`). No functional firmware changes beyond what's already listed under 1.0 below - this bump is the migration itself. The bootloader is unaffected by this migration and keeps its own independent version numbering (see its own changelog below) - only its folder moved alongside the firmware's. Later, still within this same version (no version bump, per this project's own rule that the firmware's version only changes on explicit request): IDs 12-24 assigned to the 13-tool expansion catalog (see the unified Tool Catalog section above), landing tool by tool - `TOOL_INVALID` moved to 25 to make room, and the `0x0C-0x1B`-style ID range check extended accordingly. The new companion expansion-slave chip (STM32F303CBT6, `src/F303-slave/`) got its own bootloader and application firmware, versioned entirely independently of this firmware (see the new section below). Later still, within this same version: the monolithic single-file build (this project's earlier dual-form maintenance approach) was retired in favor of maintaining a single source form - what had lived in `partitioned/` moved up to `src/F303-master/` directly (and `src/F303-master/boot/`, `src/F303-slave/`, `src/F303-slave/boot/` for the rest of this project's firmware), the `V1.1/` version-numbered subfolder dropped from every path in the process. No functional change - this is a source-tree reorganization, not a firmware behavior change. Later still, within this same version: 2 new expansion board variants (Basic+ADS1115, Basic+MLX9064x - `expansion_board_type` 5/6) get an ADS1115 and any of the 3 MLX9064x-family sensors wired directly to this board's own bit-banged expansion I2C bus, no slave MCU involved - `firmware_ads1115.c` and 3 new `melexis_mlx90640/mlx90641/mlx90642/` folders, with `Handle_CAN_FlyingProbe()`/`Handle_CAN_ThermalInspection()` both branching on `expansion_board_type` (and `mlx_sensor_variant` for the thermal sensor) to reach either this board's own direct driver or the expansion slave chip's own relayed one, transparently to whatever host is talking to them. A real bug from an earlier tool-count update was also found and fixed here: `0x1A2`'s own validation still capped `free_tool_selection` at 12 despite this project's own comment already saying 25 - silently rejecting any of the 13 newer tools in that specific configuration path. |
| **1.0** | Initial versioned release. Full support for all 12 tool profiles, hardware I2C2 OLED, per-tool CAN telemetry, and the communication/stall watchdogs described throughout this README. Also includes the `0x110`/`0x111` active-tool query added for the Tester tool, the `11111`-jumper free tool configuration mechanism (`0x1A2`/`0x1A3` - see `docs/EEPROM.TXT` section 5), and peripheral type + device serial number reporting (`0x1A4`/`0x1A5` - see `docs/EEPROM.TXT` section 6), for telling multiple otherwise-identical boards apart on a shared CAN bus. Later fixes within this same version (no version bump, per this project's own rule that the firmware's version only changes on explicit request): the laser's PWM duty is now forced to 0 in software whenever the interlock isn't armed, instead of being generated purely from the power setpoint independent of interlock state; the bit-banged expansion I2C bus now actually runs at ~100kHz instead of the ~800kHz an incorrect delay loop had been producing; and a GPIO config struct wasn't being fully reset between pins, silently giving some digital outputs a faster edge rate than intended. |

### Bootloader (`src/F303-master/boot/`)

| Version | Notes |
|---|---|
| **1.1.1** | `HandleVersionQuery` now actually returns an all-zero HardwareID when no valid firmware is installed, matching what its own comment already promised - it had been returning this bootloader's own compiled-in HardwareID instead, which broke `URTC Flasher`'s own `hardware_id==0` check for showing "no valid firmware currently installed". Later, within this same version: the monolithic single-file build was retired, same reorganization as the firmware's own 1.1 entry above - `bootloader_main.c` and the rest of what lived in `boot/partitioned/` moved up to `src/F303-master/boot/` directly. No functional change. |
| **1.1.0** | The entry point file is now named `bootloader_main.c` (matches the firmware's own `STM32F303CC_main.c` convention for its equivalent file). |
| **1.0.9** | The jump-to-application stack pointer check no longer accepts a value sitting exactly at the base of either RAM region, which would have faulted on the very first `PUSH` after handoff - both bounds now require a small margin instead. The version-query response (`0x7F9`) now retries on a busy CAN mailbox the same way `0x7FA` already did, instead of silently dropping the whole reply. Added an explicit I2C2 peripheral teardown and a memory barrier before the post-write flash verification read, both matching safety patterns already used elsewhere in this same handoff path. The signing key constant is now defined once and referenced from every module that needs it, instead of a private copy compiled into each one. |
| **1.0.8** | Fixed the OLED's I2C peripheral: PA9/PA10 with AF4 is I2C2, not I2C1 (confirmed against ST's own datasheet) - the same mistake already found and fixed in the application firmware, but never checked here until now, meaning this bootloader's OLED (the "UPDATING"/progress/error screens) would never have actually worked on real hardware. Also: hardware ID now reports the compiled-in value instead of 0 on a blank chip, an immediate first heartbeat instead of waiting a full second, and general cleanup (dual-CAN filter-bank setting that had no effect on this single-CAN chip, a lowercase-to-uppercase font fallback, a wider I2C timeout during the boot-time OLED wake-up window). |
| **1.0.7** | Batches the CRC32 calculation once per received CAN frame instead of once per byte (an 8x reduction in call count across a full transfer). A defensive `__disable_irq()` before the vector-table/stack-pointer handoff to the application. Explicit state cleanup on an erase failure during a new update attempt. 1px of inter-character spacing added to the boot-screen font renderer, which previously ran letters together into a hard-to-read block. |
| **1.0.6** | The bootloader's own version-query response (`0x7FA`) now retries if the CAN mailbox was still busy from the `0x7F9` frame sent right before it, instead of silently dropping. |
| **1.0.5** | Extended the CAN mailbox retry window from 5ms to 50ms, giving genuine bus congestion far more room to clear before a status frame gets dropped (still a small fraction of the ~800ms watchdog window). Added a periodic watchdog refresh inside the flash program/verify loops themselves, not just once per page, as extra margin against unexpected timing delays. Stack pointer bounds now reference CMSIS's own base-address constants instead of hardcoded literals. |
| **1.0.3** | Corrected the jump-to-application stack pointer validation: this chip has two real RAM regions a legitimate stack pointer could sit in (40KB standard SRAM at `0x20000000`, and a separate 8KB CCM - Core-Coupled Memory - region at `0x10000000`, confirmed against ST's own datasheet), and the check previously only accepted the first. |
| **1.0.2** | Fixed an off-by-one stack buffer overflow in the OLED status-message renderer (not triggered by any of this bootloader's own current status messages, but a real defect for any future one long enough to reach the affected column). Added frame-type validation (rejects RTR/extended-ID frames) before any CAN command is acted on, so a Remote Transmission Request can't be mistaken for a real data frame carrying stale bytes. Forces GPIO to a safe, disconnected state and clears any latched-pending SysTick interrupt immediately before jumping to the application, on top of the peripheral teardown already done there. |
| **1.0.1** | Anti-rollback protection (a validly-signed image older than what's installed is rejected - `0x05` verify-fail reason `0x05`), stricter jump-to-application checks (stack alignment, Thumb-state and address-range validation on the reset vector), a corrected RAM bound matching this chip's actual 40KB of contiguous SRAM, and general hardening around the OLED/CAN/flash timing paths. |
| **1.0.0** | Initial versioned release. HMAC-SHA256 signed OTA updates, golden-image A/B backup slot, and `0x7FA` - the bootloader's own version, reported alongside `0x7F9` (the installed application's version) whenever the bootloader itself answers a version query. |

### Expansion Slave (`src/F303-slave/`, STM32F303CBT6)

Versioned entirely independently of the main board's own firmware/
bootloader above - a separate chip, on a separate board, in a separate
source tree (`src/F303-slave/`).

| Component | Version | Notes |
|---|---|---|
| Application (`src/F303-slave/`) | **1.0** | Initial release. I2C1 link-bus protocol (this chip as slave) to the main board; I2C2 local sensor bus (this chip as master) driving an ADS1115 16-bit ADC and an MLX9064x thermal camera, built on Melexis's own official library rather than a hand-rolled register map; 4-channel local PWM generation (TIM1) for tools needing sub-millisecond pulse timing generated at the tool head itself. Later, still within this same version (no version bump, per this project's own rule that the slave's own application version only changes on explicit request): MLX90641 and MLX90642 support added alongside the original MLX90640 support - 2 genuinely separate Melexis libraries (`melexis_mlx90641/`, `melexis_mlx90642/`), not variants of the first one; MLX90641's own upstream form is this project's own first C++ code, built into an otherwise all-C firmware, while MLX90642 turned out genuinely simpler than the other two (no host-side calibration math at all - it reports temperature already calculated). A new register (`REG_MLX_SENSOR_VARIANT`) and matching main-board CAN command (`0x1A6`/`0x1A7`) let the host say which of the 3 MLX9064x family members is actually populated, relayed to this chip at boot since it has no persistent storage of its own. |
| Bootloader (`src/F303-slave/boot/`) | **1.0.0** | Initial release. Same A/B golden-image update model as the main board's own bootloader, scaled to this chip's 128KB flash, relayed over the I2C link rather than CAN directly - this chip has no CAN peripheral of its own. |

## 🔍 Current Status

**Firmware (`src/F303-master/`):** feature-complete for all 25 tool profiles — thermal PID control, per-tool telemetry, communication watchdogs, stall/fault detection, and the OLED's own live diagnostics, alongside an active-tool query pair (`0x110`/`0x111`), a generic SPI passthrough (`0x180`/`0x181`) for the expansion connector, an onboard F-RAM that persists setpoints across a power loss (`0x190`/`0x191`), the `11111`-jumper free tool configuration mechanism (`0x1A2`/`0x1A3`), peripheral type + device serial number reporting (`0x1A4`/`0x1A5`) for telling multiple otherwise-identical boards apart on a shared bus, and a CAN-to-I2C bridge (`0x210`-`0x221`) reaching the expansion slave chip on advanced expansion boards. Versioned independently of the bootloader (see the Changelog below).

**Bootloader (`src/F303-master/boot/`):** feature-complete golden-image A/B update system — HMAC-SHA256 signed OTA updates over CAN, a backup slot that guarantees a failed update never bricks the board, and its own version reporting (`0x7FA`) independent of the application. Compiles and links clean; see the bench-test caveat above before trusting it unattended with real actuators connected.

**PC tools:** both [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER) (CAN OTA updates + full-chip SWD/JTAG programming) and [URTC Tester](https://github.com/JuanenRac/URTC-TESTER) (live per-tool control/telemetry exerciser) are feature-complete for what they set out to do, each its own independent project now with its own README covering setup and every control in detail.

**Hardware:** schematic and BOM are still being finalized; no populated board exists yet to validate any of the above against real silicon. Everything above compiles, links, and has been reasoned through carefully, but "builds correctly" and "verified on hardware" are two different claims — see the safety notice at the top of this README, and treat a first bring-up with the caution any new board deserves.

If anyone in the community is working on custom end-effectors, smart tool-changers, or advanced tool integration for PAROL6, Faze4, or any other robot arm platform, I'd love to chat, swap ideas, or dive deeper into the CAN commands!

## 📂 Repository Structure

```
/
├── 3D/
│   ├── STL/                     Directory of 3D Tools parts for print in STL format
│   └── OpenSCAD/                Directory of 3D Tools parts
├── ani/                          27 GIFs: one 4-frame animation per tool profile (00-24,
│                                 matching each tool's own numeric ID), the boot splash
│                                 (splash_boot.gif), and the invalid-ID warning
│                                 (error_warning.gif) - all decoded straight from this
│                                 project's own firmware source (firmware_render.c's own
│                                 ToolIcons[]/SplashFace[]/ErrorText[] tables), not
│                                 hand-drawn separately, so they always match what the
│                                 real OLED actually shows
├── BOM/
│   ├── BOM.TXT                  Full bill of materials of PCB board
│   ├── BOM_EXPANSION_BASIC_TMC2209.TXT     Expansion board, basic + TMC2209
│   ├── BOM_EXPANSION_BASIC_TMC5160A.TXT    Expansion board, basic + TMC5160A
│   ├── BOM_EXPANSION_ADVANCED_TMC2209.TXT  Expansion board, advanced + TMC2209
│   ├── BOM_EXPANSION_ADVANCED_TMC5160A.TXT Expansion board, advanced + TMC5160A
│   ├── BOM_PARTS.TXT            Full bill of materials and mechanical parts for 3D parts
│   └── BOM_PARTS.PDF            Full bill of materials and mechanical parts for 3D parts
├── docs/
│   ├── MANUAL.PDF               Service manual of URTC board and 3D Files
│   ├── MANUAL.ODT               Service manual of URTC board and 3D Files
│   ├── CANBUS.TXT               CAN bus protocol reference (all command/telemetry IDs)
│   ├── ECOVIA.TXT               Tool identification matrix and pin-mutation logic
│   ├── TOOLS.TXT                High-level catalog of all 25 tools - what each does and
│   │                            which peripherals it uses, no pin-level detail
│   ├── PINOUT.TXT               Full MCU pinout, block by block
│   ├── PINOUT_CONNECTORS.TXT    Physical connector pinouts (CONN_DRILL, CONN_SEN, etc.)
│   ├── EXPANSION.TXT            CONN_EXPANSION connector and the add-on board variants
│   ├── PINOUT_SLAVE.txt         Full pinout for the expansion slave chip (advanced variants only)
│   ├── EEPROM.TXT               Full F-RAM register map (every persisted setting, byte offsets)
│   ├── COMPILE_STM32F303.TXT    From-scratch build guide for all 4 firmware binaries -
│   │                            toolchain, ST HAL/CMSIS setup, exact compile/link commands
│   └── tool_image_generator/    Toolkit that generates images/TOOL_*.png (see below) - PCB.png
│                                blank reference, render_engine.py + tool_data.py +
│                                generate_all.py, and PROCEDURE.TXT explaining how to add
│                                a new tool's own image or regenerate an existing one
├── src/
│   ├── F303-master/
│   │   ├── STM32F303CC_main.c    Entry point - global definitions and main()
│   │   ├── firmware_*.c/.h       ~40 more files, one per subsystem (OLED, LEDs, per-tool
│   │   │                         CAN handlers, init, persistence, etc.), including
│   │   │                         firmware_ads1115.c (direct ADS1115 driver, Basic+ADS1115
│   │   │                         board) - see this folder's own README.md for the full
│   │   │                         file-by-file table
│   │   ├── melexis_mlx90640/     Melexis's own official MLX90640 library (Apache-2.0,
│   │   │                         plain C) plus this board's own direct-connection driver
│   │   │                         on top of it, for the Basic+MLX9064x expansion board
│   │   ├── melexis_mlx90641/     Same idea, MLX90641 library (Apache-2.0, C++ - see this
│   │   │                         folder's own README.md section 8a for why this one
│   │   │                         library is C++ in an otherwise all-C project)
│   │   ├── melexis_mlx90642/     Same idea, MLX90642 library (Apache-2.0, plain C) - see
│   │   │                         section 8a for why this sensor's own driver is genuinely
│   │   │                         simpler than the other 2
│   │   ├── STM32F303CCTx_APP.ld  Linker script for the application (112K main slot at 0x08008000)
│   │   ├── README.md             Technical reference: hardware platform, the ID-jumper
│   │   │                         tool-selection system, per-tool peripheral wiring - see
│   │   │                         CANBUS.TXT for the wire-level protocol this explains the why of
│   │   └── boot/
│   │       ├── bootloader_main.c  Entry point for the bootloader
│   │       ├── bootloader_*.c/.h  9 more files (shared types/constants, crypto,
│   │       │                      flash/metadata, OLED, CAN protocol)
│   │       ├── STM32F303CCTx_BOOTLOADER.ld  Linker script for the bootloader (30K region at 0x08000000)
│   │       └── README.md          Same technical-reference role as the application's, for the bootloader
│   └── F303-slave/               Companion chip (STM32F303CBT6) on the 2 ADVANCED expansion
│       │                         board variants only - see the Expansion Connector section
│       │                         above. Own bootloader/application pair, own I2C-based
│       │                         (not CAN) update protocol, own independent versioning.
│       ├── slave_main.c          Entry point
│       ├── slave_*.c/.h          4 more files (I2C link protocol, local sensor bus, local PWM)
│       ├── STM32F303CBTx_SLAVEAPP.ld  Linker script (54K main slot at 0x08005000)
│       ├── README.md             Technical reference: why this chip exists, the local
│       │                         ADS1115/MLX9064x sensor bus, local PWM, the I2C link
│       │                         protocol to the main board
│       ├── melexis/              Melexis's own official MLX90640 library (Apache-2.0,
│       │                         plain C, unmodified, own license file) - kept as its own
│       │                         separate compilation unit, deliberately never folded into
│       │                         this project's own source, since Apache-2.0 requires that
│       │                         code's own copyright notice stay intact
│       ├── melexis_mlx90641/     Melexis's own official MLX90641 library (Apache-2.0, C++ -
│       │                         a genuinely separate library from MLX90640's own, not a
│       │                         variant of it - see this folder's own README.md section 3
│       │                         for why it's C++ and how the build handles that)
│       ├── melexis_mlx90642/     Melexis's own official MLX90642 library (Apache-2.0, plain
│       │                         C) - genuinely simpler transport interface than the other
│       │                         2 sensors' own, see README.md section 3 for why
│       └── boot/
│           ├── slaveboot_main.c   Entry point for the bootloader
│           ├── slaveboot_*.c/.h   7 more files (crypto, flash/metadata, protocol)
│           ├── STM32F303CBTx_SLAVEBOOT.ld  Linker script (18K region at 0x08000000)
│           └── README.md          Same technical-reference role as the application's
├── firmware/
│   ├── URTC_BOOTLOADER.bin       Bootloader compiled, flash to 0x08000000
│   ├── URTC_BOOTLOADER.elf       Bootloader compiled, flash to 0x08000000
│   ├── URTC_BOOTLOADER.hex       Bootloader compiled, flash to 0x08000000 (address baked in)
│   ├── URTC_V1.1_F303CC.bin      Application bin compiled, flash to 0x08008000
│   ├── URTC_V1.1_F303CC.elf      Application elf compiled, flash to 0x08008000
│   ├── URTC_V1.1_F303CC.hex      Application HEX compiled, flash to 0x08008000 (address baked in)
│   ├── URTC_SLAVE_BOOTLOADER.{bin,elf,hex}  Expansion slave's own bootloader, flash to 0x08000000
│   │                             on the STM32F303CBT6 (advanced expansion boards only)
│   └── URTC_SLAVE_APP.{bin,elf,hex}  Expansion slave's own application, flash to 0x08005000
├── images/
│   ├── OLED_DIRECT_MOUNT.jpg     LCD1/CONN_OLED2 - bare 30-pin FPC panel, direct-mount option
│   ├── OLED_BREAKOUT_MODULE.jpg  CONN_OLED - external I2C breakout module, alternate option
│   ├── URTC_LOGO.svg             General project logo, embedded at the top of this README
│   ├── URTC_BOARD.png           Board photo (when added)
│   ├── URTC_SCHEMATIC.png       Board schematic (when added)
│   ├── URTC_PCB_TOP.png         Board TOP layer (when added)
│   ├── URTC_PCB_BOTTOM.png      Board BOTTOM layer (when added)
│   └── TOOL_*.png               Per-tool jumper/wiring reference diagram, one per profile
│                                (all 25 present - see each tool's own link in the Tool Catalog above)
├── PCB/
│   ├── URTC_V1.0.sch            Eagle schematic (when added)
│   ├── URTC_V1.0.brd            Eagle board layout (when added)
│   ├── URTC_V1.0_JLCPCB.ZIP     Gerbers, bom and cpl files 
│   ├── datasheet/               Datasheets of all parts used in board
│   └── *_PARLIST/PINLIST/NETLIST.TXT   Eagle-exported netlists (ground truth for pin mapping)
├── LICENSE
└── README.md                    This file
```

Hardware design files (Eagle schematic/board/netlists) will be added as the layout stabilizes.

## 👤 Author

**JuanenRac** (Electro Hobby 3D)
📧 electrohobby3d@gmail.com
📺 [youtube.com/@electrohobby3d](https://youtube.com/@electrohobby3d)

## 📜 License and Copyright Notices

URTC is (c) 2026 JuanenRac (Electro Hobby 3D). This notice must be included in any distributions of this project or derivative works.

Because this project consists of several different types of content, individual parts are made available under different licenses - each suited to what it actually covers, rather than forcing one license to fit everything:

1. The **firmware** located at `./firmware` (application and CAN bootloader alike) is available under the **GNU General Public License v3.0 (GPL-3.0)**. Full text at https://www.gnu.org/licenses/gpl-3.0.html.

2. The **hardware designs** (Eagle schematic/board files, gerbers, and the 3D-printable parts under `./PCB` and `./3D`) are available under the **CERN Open Hardware Licence v2 - Strongly Reciprocal (CERN-OHL-S v2)**. Full text at https://cern-ohl.web.cern.ch/.

3. The **documentation** (this README, the service manual, and the reference files under `./docs`) is available under **Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)**. Full text at https://creativecommons.org/licenses/by-sa/4.0/.

If you build on this project, keep the licensing split in mind: code changes to the firmware should stay GPL-3.0, hardware modifications should stay CERN-OHL-S, and documentation derivatives should stay CC BY-SA - each with attribution back to this project.

This repository covers the URTC board's own firmware and hardware only - the PC tools (URTC Flasher, URTC Tester) that used to live here are now independent projects with their own licensing, see "PC Tools" above.
