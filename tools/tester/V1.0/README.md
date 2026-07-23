<p align="center">
  <img src="/images/URTC_LOGO_TESTER.svg" alt="URTC Tester Logo" width="100%">
</p>

# URTC Tester (Windows / Linux)

**Version:** 1.0 · **Author:** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

License: **GPL-3.0**, same as the URTC firmware and the flasher tool - see
`LICENSE` in the repository root.

A live CAN bus exerciser for the URTC board. It connects over the same
USB-CAN adapter the flasher uses, asks the board which of its 12 tool
profiles it's currently jumpered for, and shows only that tool's own
controls and telemetry - not one window trying to represent all 12 at
once. Everything it does is a runtime command or a telemetry read against
the currently-running application; it never touches flash, so there's
nothing here that can leave the board any less working than it started.

## 1. Relationship to the flasher

This tool and `tools/flasher/` share the same transport layer (SLCAN and
SocketCAN classes are identical) since both ultimately just need to get
CAN frames on and off the same kind of adapter, but they do fundamentally
different jobs:

| | Flasher | Tester |
|---|---|---|
| Touches flash | Yes (that's the whole point) | Never |
| Talks to | The bootloader, mostly | The running application |
| Purpose | Update firmware | Exercise/verify a tool head's actual hardware |

If you're not sure which one you need: if the board is already running
firmware and you want to check a tool actually works (heater heats,
motor turns, LED lights up), you want this one.

## 2. Install and run

Same pattern as the flasher:

```
cd tools/tester
pip install -r requirements.txt
python urtc_tester.py          # Windows
python3 urtc_tester.py         # Linux
```

Or build a standalone binary: `build_exe.bat` on Windows, `./build_exe.sh`
on Linux. Both clean `build/`/`dist/` first and bundle `assets/` (the
banner and icon) into the executable - see the flasher's own README for
the fuller reasoning behind these scripts, since it applies identically here.

**On startup**, the banner shows centered on screen for 5 seconds before
the main window appears, rather than living inside the window itself -
same as the flasher, and for the same reason (keeps the window itself
compact). The window/taskbar icon is likewise a small standalone design,
not the banner shrunk down.

**Linux SLCAN/SocketCAN setup** (adapter reflash, serial permissions,
`ip link` bring-up) is exactly the same as the flasher's section 1 - see
`tools/flasher/README.md` sections 1 and 2 rather than duplicating it
here.

## 3. How it works

**Connect** (section 1, identical to the flasher): pick Serial/SLCAN or
SocketCAN, the port/interface, optionally auto-detect the bitrate, then
Connect.

**Detection happens automatically on connect** (or click **Detect** to
redo it): the tool sends `0x110` (query active tool) and `0x7F8` (query
version), and uses the response to:
- Show which of the 12 tool profiles is active, and the board's overall
  state (any declared error, CAN bus fault, still-in-boot-splash).
- Show the reporting HardwareID and firmware version, flagging a mismatch
  if it doesn't match this project's own `THIS_HARDWARE_ID`.
- Build the **Tool Controls** panel on the right for that specific tool -
  and only that tool. Switching which tool is jumpered and detecting
  again tears down the old panel and builds the new one from scratch.

**Global Controls** (section 2, always visible regardless of which tool
is active): the status LED color override, the ring LED color and
on/off, and OLED display mode (`0x100`) - these apply to every tool, so
they don't move to the dynamic panel. In AOI Inspection mode specifically,
the ring's on/off here is ignored in favor of that tool's own strobe
control (per `CANBUS.TXT`) - color still applies either way.

**Expansion Board** (section 3, always visible): `CONN_EXPANSION`'s own
SPI bus and DIAG0 line - nothing else lives on this connector today -

**Persistence F-RAM** (section 4, also always visible, but deliberately
separate from Expansion Board above): the FM24CL64B shares I2C1 with the
OLED - a core board component, not something wired to `CONN_EXPANSION`
at all. An earlier version of this tool bundled the two together under
"Expansion Board", which implied a connection between them that was
never real - the expansion connector itself has no F-RAM, no EEPROM,
nothing non-volatile on it.
- **SPI passthrough**: type space-separated hex bytes (1-7 of them, e.g.
  `01 02 03`), hit Send, and see exactly what came back on MISO during
  that same transfer (`0x180`/`0x181`) - a raw byte transport, not
  TMC5160-register-aware, matching the firmware's own approach. Useful
  for exercising the bus itself before a specific expansion board's
  register protocol is worth building a dedicated panel for.
- **DIAG0 level**: **Query DIAG0** reads the current state of a TMC5160's
  stall/fault diagnostic line (`0x182`/`0x183`) - HIGH (inactive) or LOW
  (asserted). A simple polled read, not a live/pushed value - hit the
  button again to refresh it.
- **Persistence F-RAM**: **Query State** reads back whatever the board
  last saved before a power loss (`0x190`/`0x191`) - which tool it was,
  the setpoint, whether a critical error was active at the time.
  **Erase F-RAM...** wipes it (`0x192`, with a confirmation dialog first
  - this can't be undone).

**Custom CAN Frame** (section 6, also always visible): a raw ID + hex
bytes entry with one-shot and periodic send - for a command that doesn't
have its own control here yet, or for testing something not (or not yet)
documented in `CANBUS.TXT`. No validation beyond ID range and DLC≤8;
whatever this sends is exactly what goes on the bus. Same section also
opens the **Raw Bus Monitor** (see below).

**Run Self-Test** (next to Detect): runs a small set of safe, at-rest
communication checks for whichever tool is currently detected - confirms
the active-tool query and version query both respond, then (for tools
with telemetry) sends a safe setpoint/speed/power of 0 and checks the
expected telemetry arrives. Deliberately never sends anything that would
actually heat, fire, or spin at meaningful power - this verifies the
communication round-trip works, not that an actuator physically responds,
since confirming that needs a human watching anyway. Asks for
confirmation before sending anything. Tools with no telemetry (plain
motion) or that are purely event-driven (scan probe) get an info-only
note instead of a real pass/fail.

**Live temperature graphs**: the soldering iron and 3D-printer nozzle
panels both show a small rolling line graph alongside their live
temperature reading - a plain Tkinter Canvas widget, not a new
dependency (matplotlib/pyqtgraph would break this tool's zero-dependency
policy beyond pyserial). Fixed Y-axis scale (0 to that tool's own
setpoint ceiling) rather than auto-scaling, so the trend is easy to read
at a glance rather than the scale shifting under it.

**Raw Bus Monitor** (opened from the Custom CAN Frame section): a
separate window showing every frame seen, any ID, independent of the
active tool panel - a live-scrolling table (Time/ID/DLC/Data/Δt),
Pause/Clear, and an approximate bus-load/frame-rate readout (updated
once a second; the load figure doesn't model bit-stuffing overhead, so
treat it as a rough diagnostic figure, not a certified measurement).
**Export .trc...**/**Export .asc...** save the currently-shown table as a
simplified PEAK PCAN-View / Vector CANalyzer-style trace file
respectively - close enough to be readable by most tools that expect
those formats, not guaranteed byte-identical to what the real
applications produce. If `urtc_custom_ids.json` exists next to this
script (optional, not included by default - `{"0x199": "My Sensor"}`),
the ID column shows that friendly name alongside the raw hex ID -
useful for anyone testing a custom expansion board's own traffic without
needing to modify this tool's source.

## 4. Tool coverage

Every one of the 12 profiles has its own panel, built directly from
`CANBUS.TXT`:

| Tool | Controls | Live telemetry |
|---|---|---|
| Soldering Iron | Setpoint temperature, on/off | Actual temperature, endstop |
| Paste/Liquid Dispenser, Screwdriver, both Grippers | Direction + step count (one-shot move) | none (shared 0x120, no telemetry for any of these 5) |
| Vacuum Pickup | none | Analog reading, part-detected |
| Drill | Speed + direction | Actual RPM, endstop |
| AOI Inspection | Ring mode (off/strobe/continuous) + strobe period | Endstop |
| Laser Engraver | Power + interlock arm/safe | Endstop |
| 3D Printer | Nozzle setpoint, extruder direction/steps, layer fan power, hotend fan power | Hotend temperature, layer fan RPM, hotend fan RPM |
| Scan Probe | none | Impact event count + timestamp (max-priority `0x095`) |

**Communication watchdogs are handled for you.** The soldering iron,
laser, and 3D-printer nozzle each have a 250ms watchdog in firmware; the
layer fan has a 1000ms one. Checking the relevant "Active" box doesn't
just send the command once - it resends automatically (150ms for the
250ms-watchdog tools, 400ms for the layer fan) for as long as the box
stays checked, the same way a real master controller has to. Unchecking
it sends a single zero/off frame and stops. The hotend fan has no
watchdog (a stall detector instead - see `CANBUS.TXT`), so it's a plain
one-shot send.

## 5. Logs and debug bundles

Same as the flasher: a timestamped session log is written automatically
to `tools/tester/logs/` (safe to delete), and **Export Debug Bundle**
saves a `.zip` with the current on-screen log plus basic system
diagnostics (OS, Python version, current transport/port/bitrate, detected
tool) for handing to whoever's debugging a tool head issue.

## 6. Known limitations

- **Not tested against real hardware.** Every piece here - the transport
  layer, the CAN ID/byte-layout handling, the watchdog keepalive timing -
  was checked in isolation (mocked frames, a real subprocess for timing
  where relevant) but the environment that built this has no USB access.
  Treat a first real session with the same caution the flasher's own
  README asks for.
- **One tool panel at a time, by design**, not a current limitation to be
  removed later - see the intro above for why.
- **Global LED colors are a straight override**, not a live readback -
  there's no telemetry for what the status/ring LEDs are actually
  currently showing, only what was last commanded.
