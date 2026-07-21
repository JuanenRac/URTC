<p align="center">
  <img src="/images/URTC_LOGO_FLASHER.svg" alt="URTC Flasher Logo" width="100%">
</p>

# URTC Flasher (Windows / Linux)

**Version:** 1.0 (this tool's own version - shown in the window banner and
title bar, tracked separately from the URTC board firmware version it
writes)

**Author:** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

License: **GPL-3.0**, same as the URTC firmware itself — see `LICENSE` in
the repository root. This covers `urtc_flasher.py` and any binary
built from it.

A small cross-platform GUI tool for updating URTC board firmware over CAN
bus. It implements the exact bootloader protocol from `CANBUS.TXT`: the
HardwareID check, HMAC-SHA256 signing, the golden-image backup-slot update
flow, live progress via the bootloader's heartbeat messages, and a
version-query (does this board's application *or* bootloader identify
itself over CAN) so you can see what's currently installed before deciding
what to flash.

Two ways to talk to the board, both speaking the same protocol underneath:

- **Serial / SLCAN** — works on Windows and Linux. Needs a USB-CAN adapter
  running SLCAN firmware, connected as a virtual serial port.
- **SocketCAN** — **Linux only**, and only shown in the tool's UI on
  Linux. Talks directly to a kernel `can0`/`slcan0` network interface. If
  your adapter already runs `gs_usb`/candleLight firmware (most CANable
  boards do out of the box), this path needs **no adapter reflash at
  all** — Linux's own driver handles it natively.

**Status:** the CRC32 and HMAC-SHA256 computation in this tool have been
verified byte-for-byte against the bootloader's own C implementation, and
the SocketCAN frame packing was verified against Linux's `struct can_frame`
layout with a round-trip pack/unpack test. What has **not** been tested on
either platform is a real board over real hardware — treat a first
real-world flash attempt with the same caution you'd give any new tool
talking to a bootloader: have JTAG on hand as a fallback.

## 1. Get your adapter talking CAN

Which of these you need depends on your platform and which transport
you'll use:

**Linux, SocketCAN path (recommended if your adapter supports it):**
Nothing to flash on the adapter itself. Bring the interface up once per
boot (or add it to your network config to persist):
```
sudo modprobe can vcan gs_usb   # gs_usb covers most CANable-family boards
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up
```
If your adapter enumerates as something other than `can0`, check `ip link
show` (or `dmesg` right after plugging it in) for the actual name. Some
adapters need `slcand` instead of a native driver — if `ip link show`
doesn't show a CAN interface at all after plugging in, this is likely your
case; see your adapter's documentation for the `slcand` invocation, which
creates an `slcan0` interface you then bring up the same way as above.

**Windows, or Linux via the Serial/SLCAN path:**
A CANable Pro v2 ships by default running **candleLight** firmware, which
talks to the host using the `gs_usb` protocol - the same one Linux's
SocketCAN `gs_usb` driver expects natively (see above). That protocol does
**not** present as a serial port, which is what this path needs. To use
Serial/SLCAN instead (required on Windows; optional on Linux):

1. Download SLCAN-compatible firmware for your adapter (search for
   "canable slcan firmware" — there are a few maintained forks; use
   whichever your adapter's own documentation points to).
2. Put the adapter into DFU/bootloader mode (usually a BOOT button held
   during power-up, or a jumper — check your adapter's documentation).
3. Flash the SLCAN firmware using your adapter vendor's flashing tool or
   `dfu-util`.
4. Reconnect — it should now enumerate as a serial port: a COM port on
   Windows, or `/dev/ttyACM0`/`/dev/ttyUSB0`-style on Linux.

If your adapter already runs SLCAN firmware, skip straight to step 2 below.

## 2. Install and run

**Windows:**
```
python -m pip install -r requirements.txt
python urtc_flasher.py
```
Or build a standalone `.exe` with `build_exe.bat` (see that file).

**Linux:**
```
python3 -m pip install -r requirements.txt
python3 urtc_flasher.py
```
Or build a standalone binary with `./build_exe.sh` (`chmod +x` it first).

**On startup**, the banner shows centered on screen for 5 seconds before
the main window appears - it's not part of the main window itself (which
is why the window is fairly compact for how much it actually does). The
window and taskbar icon is a small standalone design (`assets/urtc_icon.png`
/ `.ico`), not the banner shrunk down - the full banner artwork doesn't
hold up at 16-32px.

Tkinter (the GUI toolkit) ships with Python on Windows, but on
Debian/Ubuntu-family distros it's a separate OS package:
```
sudo apt install python3-tk
```
(Fedora: `sudo dnf install python3-tkinter`. Arch: `sudo pacman -S tk`.)
`build_exe.sh` checks for this itself and tells you if it's missing rather
than failing partway through.

**Linux serial permissions:** if you're using the Serial/SLCAN path and
connecting fails with "Permission denied", your user needs to be in the
group that owns serial devices (`dialout` on Debian/Ubuntu; varies on
other distros):
```
sudo usermod -a -G dialout $USER
```
Log out and back in (group membership is read at login), then try again.
The tool detects this specific error and shows this same fix in a dialog,
but it's worth knowing ahead of time. SocketCAN doesn't have this
particular gotcha — access to a `can0`-style interface isn't gated by the
`dialout` group — but bringing the interface up in the first place (step 1
above) does need `sudo`, since that's a network-device configuration
change.

Using `python -m pip`/`python3 -m pip` rather than a bare `pip` sidesteps a
common issue on both platforms: `pip`'s own wrapper script isn't always on
PATH even right after a successful install, while `-m pip` finds the
installed module directly.

## 3. Where firmware files go

This tool expects a `firmware/` folder **inside `tools/flasher/`**, right next to
`urtc_flasher.py`:

```
tools/flasher/
├── assets/
│   ├── URTC_LOGO_FLASHER.svg      <- banner source (vector)
│   └── urtc_banner.png            <- shown at the top of the window, rendered from the .svg above
├── firmware/
│   ├── URTC_v1_0_F303CC.bin      <- put new .bin files here
│   └── URTC_v1_0_F303CC_old.bin  <- can keep older versions around too
├── logs/                          <- created automatically, one file per session
├── urtc_config.json               <- optional, not included by default (see "Changing the HMAC key" below)
├── urtc_flasher.py            <- this tool
├── requirements.txt
├── build_exe.bat                  <- Windows standalone build
├── build_exe.sh                   <- Linux standalone build
└── README.md
```

`assets/urtc_banner.png` is optional - if it's missing, the tool just
starts without a banner rather than failing. It's loaded through
tkinter's own native PNG support (Tk 8.6+, which every current Python
ships with), not Pillow, so it doesn't add a new dependency. Both
`build_exe.bat` and `build_exe.sh` already bundle `assets/` into the
standalone executable via PyInstaller's `--add-data`, so this works the
same way whether you run from source or from a built binary.

This is deliberate: keeping `firmware/` inside `tools/flasher/` instead of at
the repo root means the whole `tools/flasher/` folder is self-contained. If
you just want to flash a board — on a shop floor PC, from a USB stick,
wherever — you can copy `tools/flasher/` on its own with nothing else from
the repo, and it still works.

**You can keep more than one `.bin` in there.** Every file gets checked and
listed - the tool doesn't just grab whatever it finds. On startup (and
whenever you click **Refresh**), each `.bin` in `firmware/` is checked
against the same plausibility test the bootloader itself applies to a
fresh image (its first 4 bytes have to look like a real initial stack
pointer for this chip's RAM, and its size has to fit the main slot). Every
file shows up in the list with a clear ✓ or ✗ and the reason why:

| File | Size | Status |
|---|---|---|
| URTC_v1_0_F303CC.bin | 30.9 KB | ✓ looks valid |
| URTC_v1_0_F303CC_old.bin | 30.4 KB | ✓ looks valid |
| notes.txt.bin | 0.1 KB | ✗ first word doesn't look like a valid stack pointer |

- **Exactly one file passes the check** → it's selected for you the moment
  the tool starts. An invalid file sitting alone in the folder does *not*
  get auto-selected just because nothing else is competing with it.
- **More than one valid file** → nothing is auto-selected; pick the one you
  want from the list.
- **You select an invalid-looking file anyway** → the tool asks you to
  confirm first. This check exists to catch obvious mistakes (wrong file,
  truncated download, an empty placeholder) - it can't catch everything
  (a corrupted-but-plausible file, or one signed with the wrong key), which
  is what the bootloader's own CRC32/HMAC check during the real transfer
  is for.
- **Nothing found, or you want a file from somewhere else entirely** → use
  the **Browse .bin...** button, which works regardless of where the file
  actually lives (and runs the same validation check either way).

Adding a new build later: just drop it into `firmware/` and click
**Refresh** - no restart needed.

## 4. Checking what's currently installed

If you're on Linux and SocketCAN is available, you'll see a **Transport**
choice at the top - pick Serial/SLCAN or SocketCAN before connecting. On
Windows this row doesn't appear at all; Serial/SLCAN is the only option.

Click **Connect**, and the tool automatically asks the board what it's
currently running (CAN ID `0x7F8` → `0x7F9` - see `CANBUS.TXT`). This works
whether the board is running its application normally *or* sitting in the
bootloader, so you don't need to trigger a reset just to find out. Click
**Query** any time afterward to check again (useful right after a flash
completes, to confirm the new version actually took).

**When the bootloader itself answers** (board sitting in the bootloader,
not running its application), it also reports its own version - a
separate thing from the installed application's version, tracked via its
own `BOOTLOADER_VERSION_MAJOR/MINOR/PATCH` in `BOOTLOADER.C` and sent as
a second frame (`0x7FA`) right alongside `0x7F9`. The running application
never sends this - it has no way to know a currently-flashed bootloader's
version other than asking the bootloader itself, so this only shows up
when the board is actually sitting there (right after `0x7F0`, or on a
fresh boot before it's jumped to the application).

What you'll see:

- **`v1.0 (application, HardwareID 0x0303CC01)`** - normal case, application
  running, everything matches.
- **`Bootloader running, no valid firmware currently installed, bootloader
  v1.0.0`** - the board is stuck in the bootloader with nothing to jump
  to (blank chip, or every check on the main slot failed). This is
  exactly the situation this tool exists to fix - flash it. The
  bootloader version shown here is the bootloader itself, unrelated to
  whatever application version failed its checks.
- **`⚠ HardwareID mismatch!`** shown in red - something answered, but its
  HardwareID doesn't match what this tool expects. Don't flash without
  understanding why first; the bootloader would reject the update anyway,
  but a mismatch here can also mean you're pointed at the wrong board
  entirely.
- **No response** (red) - board unresponsive, wrong bitrate, or not
  actually connected. Check the physical connection and, on the SocketCAN
  path, that the interface is actually up (`ip link show`).

## 5. Flashing

1. **Connect**: pick Serial/SLCAN or SocketCAN (Linux only), then the
   port/interface, then click Connect. For Serial/SLCAN this opens the CAN
   channel at 500 kbit/s (URTC's fixed bus speed); for SocketCAN the
   interface is expected to already be at that bitrate (step 1 above) -
   this tool doesn't set it. Either way, the current version is queried
   automatically - see section 4 above.
2. **Select firmware**: pick from the detected list, or Browse - see
   section 3 above for exactly how detection and validation work.
3. **Flash**:
   - Leave "Board is currently running the application" checked if the
     board is powered up and running normally - the tool sends the
     `0x7F0` magic-payload trigger first, which safely shuts down every
     actuator before resetting into the bootloader.
   - Uncheck it if the board is already sitting in the bootloader (right
     after a fresh JTAG flash, or if the version check above showed "no
     valid firmware currently installed").
   - Click **Flash Firmware** and confirm. The log shows every protocol
     step; the progress bar tracks page-by-page write progress during
     transfer, then copy progress during the final backup-to-main copy.

If verification fails at any point (CRC32, HMAC, or HardwareID mismatch),
the bootloader's main slot is never touched - the board keeps running
whatever firmware it already had. It's always safe to just try again.

## 6. Programming the complete chip via SWD/JTAG (advanced)

Section "4. Program complete chip via SWD/JTAG" in the tool does a full
bring-up flash - mass-erase the entire chip, then write both the
bootloader (`0x08000000`) and application (`0x08008000`) images fresh.
This is a **different kind of operation** from sections 1-5 above:

|  | CAN OTA update (sections 1-5) | Full-chip SWD/JTAG (section 6) |
|---|---|---|
| Self-healing if interrupted | Yes - golden-image backup slot guarantees the running firmware survives | No - won't run anything until reprogrammed |
| Recoverable | Automatically, no action needed | Yes - just reconnect and flash again via SWD; the debug port doesn't depend on flash contents. Only a true permanent lock-out (RDP2 option byte) would prevent this, and nothing in this tool sets option bytes |
| Touches the bootloader | Never | Yes, by design |
| Needs | A USB-CAN adapter | An SWD/JTAG probe (ST-Link or similar) |
| Typical use | Routine firmware updates | First bring-up on a blank chip, or recovering a bricked board |

**Requires one of** (the tool auto-detects which is available and only
enables the ones it finds):
- **pyOCD** - `pip install pyocd`. Free, open-source, no separate install
  beyond the pip package.
- **STM32CubeProgrammer** - ST's official tool, installed separately from
  [st.com](https://www.st.com). If you already have it for other STM32
  work, no extra install needed here.

Both are driven as command-line subprocesses, not imported as Python
libraries - you'll see the exact command logged before it runs.

**File formats:** `.bin` (needs the fixed address this tool already
knows - you don't enter it) or `.hex` (carries its own address, used
as-is). Mixing is fine - bootloader as `.hex` and application as `.bin`,
or vice versa, both work. Both file pickers validate the selected file
(plausible size for the target slot, and - where the format allows
confidently checking it - a plausible initial stack pointer) before
letting you proceed, the same way the CAN path's firmware picker already did.

**Connection is checked before anything destructive runs.** An earlier
version of this tool did not do this: a real test with no probe connected
at all still logged a full "complete" message, because
STM32CubeProgrammer's own exit code turned out not to be a reliable
success/failure signal by itself. Every run now starts with a dedicated
connection check (`pyocd list --probes`, or a connect-only
`-c port=SWD` for STM32CubeProgrammer) that requires **positive evidence**
of a real probe/target - not just the absence of an error - before the
mass-erase step ever runs. Every subsequent command's output is also
screened for known failure text as a second layer, in case a tool's exit
code alone isn't trustworthy again in some other situation this hasn't
hit yet.

**Dry run is on by default.** The first time, leave it checked and hit
"Flash Complete Chip" - it prints the exact commands to the log without
touching the board. Read through them, confirm the paths and addresses
look right, *then* uncheck dry run and do it for real.

**Testing status:** the connection-check logic above was verified against
real STM32CubeProgrammer output (both a genuine successful-connect log and
a documented "No target connected" failure, both sourced from ST's own
community forum) and against the exact false-success scenario a real user
hit. The full erase/program/verify sequence against a real ST-Link and a
real STM32F303CC has still not been exercised end-to-end - the environment
that wrote this has no USB access. Treat a first full real attempt with
appropriate caution - a spare/test board first if you have one, and keep a
fallback plan (STM32CubeIDE's own flash tool, or `st-flash`) in mind in
case something about your specific pyOCD version or probe doesn't match
what this assumes.

## 7. CLI mode (headless, no GUI)

For CI pipelines, test benches, or production-line scripting where
there's no display:

```
python3 urtc_flasher.py --cli --port /dev/ttyACM0 --file firmware.bin
```

```
usage: urtc_flasher.py --cli [-h] [--transport {serial,socketcan}] --port PORT
                             --file FILE [--no-trigger] [--force]
```

Exit codes: `0` success, `1` protocol/connection error, `2` bad arguments
or a firmware file that fails validation (pass `--force` to flash it
anyway), `130` cancelled with Ctrl+C. Only covers the CAN OTA update path
(sections 1-3) - the SWD/JTAG full-chip path is deliberately GUI-only for
now, given how much more is at stake if a scripted run gets a wrong
file/target combination wrong with nobody watching.

## 8. Auto-retry and session logs

If a page's ACK doesn't arrive within the normal 3s window during a CAN
update, the tool retries the *wait* (not a resend of the page's data) up
to twice more with a short backoff before giving up, recovering from an
ACK that was delayed or lost on a noisy bus without the underlying data
being lost. It deliberately does not resend page data on a timeout - if
the original data actually arrived fine and only the ACK was lost,
resending would make the bootloader read those bytes as the start of the
*next* page, desyncing the transfer.

Every session also writes a timestamped log file to `tools/flasher/logs/`
(`urtc_flasher_YYYYMMDD_HHMMSS.log`), independent of the on-screen log -
useful for handing a full trace to whoever wrote the firmware if
something goes wrong in the field. This folder is created automatically
and is safe to delete; nothing reads old logs back in.

## 9. Bitrate, option bytes, and debug bundles

**Bitrate selector + auto-detect** (Serial/SLCAN only): URTC's bus is fixed
at 500 kbit/s, which stays the default - this is for a misconfigured
adapter or troubleshooting a non-standard board. **Auto-detect** tries
each standard SLCAN bitrate in turn against a version query and stops at
the first one that gets a real response; not connected yet when you click
it. SocketCAN's bitrate is set at the OS level (`ip link`), so this
control is disabled for that transport - there's nothing here for it to try.

**Check Option Bytes** (section 4, STM32CubeProgrammer only - pyOCD doesn't
expose this the same way via CLI): a read-only `-ob displ` dump, no
erase/write. Flags RDP level with the same care this whole tool takes
around SWD risk:
- **RDP0** - no protection, normal for a dev board.
- **RDP1** - reversible via CubeProgrammer's Read Unprotect, but that
  mass-erases the chip as part of removing it - not something this tool
  does for you automatically.
- **RDP2** - the one genuinely **permanent** lock-out in this whole
  project. Unlike every other risk documented above (all recoverable via
  SWD), RDP2 disables the debug port forever by ST's own design. This
  check exists to catch it before a full-chip operation, not after.

**Export Debug Bundle** (above the log): saves a `.zip` with the current
on-screen log, basic system diagnostics (OS, Python version, which tools
were found, current transport/port/bitrate), and the currently-selected
CAN firmware file - useful for handing a complete picture to whoever wrote
the firmware if something goes wrong in the field, instead of copying log
text by hand.

## 10. Bus activity check, and native .ELF/.AXF support

**Bus activity** ("Check (2s)", next to Query): counts real protocol
frames actually seen over a fixed 2-second window on whichever transport
is connected. This is deliberately **not** the same thing as a true CAN
bus-load percentage or the controller's own error counters (REC/TEC) -
those need a netlink query (SocketCAN) or adapter-specific extensions
(SLCAN) this tool doesn't have a standard, dependency-free way to get.
What it does give: a genuine, directly-measured "is anything talking on
this bus, and roughly how often" signal, on either transport. For
SocketCAN specifically, it also shows the 2-second delta of Linux's own
interface statistics (`/sys/class/net/<iface>/statistics/`) - basic
rx/tx/error/drop counters every interface exposes, read as plain files,
no extra dependency.

**Native .ELF/.AXF support**: the SWD section's bootloader/application
pickers now accept `.elf`/`.axf` alongside `.bin`/`.hex`. Parsed with a
small amount of hand-written struct-unpacking (ELF header + program
headers only - no symbols, no section headers), deliberately not using
`pyelftools`: this project has stayed at zero non-stdlib dependencies
throughout, and full ELF parsing is more than this specific plausibility
check needs. Tested against the actual compiled `BOOTLOADER.elf`/`APP.elf`
from this project's own build - both correctly validated at their real
load addresses (`0x08000000`/`0x08008000`), not just synthetic test files.
32-bit little-endian ARM only, which is all a Cortex-M target ever is.

## 11. Catching a swapped bootloader/application file, and other fixes

**Bootloader/application cross-check** (section 4): the file pickers now
also verify the image is meant for the slot it's being put into, not just
that it looks like *some* valid firmware. A bootloader image and an
application image both have an equally plausible stack pointer - same
chip, same RAM - so that check alone couldn't tell them apart if one
ended up in the other's slot. What can: a linked image's **reset
handler** is a real, absolute address baked in at link time, and it only
ever points inside the region it was actually linked for. Verified
against this project's own real compiled `BOOTLOADER.bin`/`APP.bin`:
their reset handlers are `0x080030F1` and `0x0800C725` respectively, each
correctly inside its own slot's address range and outside the other's -
so putting either one in the wrong slot is now caught and blocked, not
silently accepted. Same logic applies to `.hex`/`.elf`, checked against
their own embedded load address instead.

**`build_exe.bat`/`build_exe.sh` now pass `--noconfirm`** to PyInstaller.
Without it, rebuilding over an existing `dist/URTC_Flasher` stops and
waits for a "replace it?" prompt that's easy to miss in a script's output
- the old file was being silently left in place when that prompt never
got answered.

**Specific verify-failure reasons** (needed a bootloader change,
not just the flasher): `BOOTLOADER.C` now sends a reason byte alongside
status `0x05` (verify failed) - incomplete transfer, CRC32 mismatch, HMAC
mismatch, or HardwareID mismatch, instead of every failure looking
identical. HardwareID mismatch used to report the generic `0xFF` error
code; it now uses this same reason-coded path for one consistent "why did
it fail" answer. See `CANBUS.TXT` for the exact frame format. **This
changes the bootloader's CAN protocol slightly (DLC 1->2 specifically for
status 0x05)** - reflash the bootloader from this same delivery alongside
this version of the flasher, and rebuild it together with the same
version of `STM32F303CC.C` if you change either again later.

## 12. Multiple probes, verified pyOCD writes, and transfer telemetry

**Probe selection** (section 4): if more than one ST-Link/probe is
connected at once, every command now requires picking one explicitly from
the Probe dropdown - there's no more "whichever one the OS happens to
enumerate first". With exactly one probe connected, it's auto-selected;
with zero or several, you'll need to hit Refresh and choose. This applies
to the full-chip flash and the option-bytes check alike, since both are
destructive-adjacent enough that guessing the wrong board is a real risk
on a multi-device bench.

**pyOCD writes are now verified with an explicit read-back**, not just
trusted on exit code. pyOCD's own `flash` command already skips
rewriting pages that already match (a speed optimization, not a
verification report), so this adds a `commander compare` step against
both images after writing - a genuine byte-for-byte check, matching what
STM32CubeProgrammer's `-v` flag already did. Only for `.bin`: `compare`
checks flash content against the file's raw bytes, which wouldn't
correctly match a `.hex`/`.elf` file's own encoding even after a
successful flash, so those two formats skip this specific step and rely
on pyOCD's own internal write-time verification instead.

**Transfer telemetry**: the log now shows effective KB/s and elapsed time
per page during a CAN update, plus a summary line at the end (total time,
average KB/s, how many page-ACK retries happened). Purely informational -
doesn't change flashing behavior, just makes it easier to tell "this is
just slow" from "something's actually wrong" at a glance.

**Memory-map values are now configurable too** - `urtc_config.json` (see
below) can override `app_max_size`, `bootloader_max_size`,
`flash_page_size`, `bootloader_flash_addr`, and `app_flash_addr` on top of
the HMAC key and HardwareID it already supported, useful if this tool is
ever adapted to a different chip variant or partition scheme.

**SocketCAN carrier check**: connecting now also reads
`/sys/class/net/<iface>/carrier` - a plain 0/1 file every Linux interface
exposes. When a CAN controller goes bus-off, the kernel driver genuinely
does call `netif_carrier_off()`, so "no carrier" here is real evidence of
a bus-off or similarly dead link, logged as a warning with the exact
recovery command (`sudo ip link set <iface> down && sudo ip link set
<iface> up type can bitrate 500000 restart-ms 100`). This tool doesn't
run that command itself - clearing a real bus-off needs the interface
taken down and back up at the kernel level, which needs root and counts
as changing system network configuration, not something to do silently
on the user's behalf.

## 13. Smaller robustness fixes

A handful of edge cases fixed alongside the above:

- **Config file with a plain number**: `urtc_config.json`'s `hardware_id`
  now accepts either a JSON string (`"0x0303CC01"`) or a plain JSON number
  (`50580689`) - previously only the string form worked, and a number
  crashed the tool at startup entirely rather than falling back gracefully.
- **Sparse `.hex` files**: a `.hex` with a small block of real firmware
  plus a distant, separate block (option bytes, calibration data - some
  STM32 toolchains do bundle these into a single export) no longer gets
  rejected as if it were hundreds of megabytes. Size is now checked as
  actual occupied bytes, not the address span from the lowest to the
  highest record.
- **Raw binaries under other extensions**: a raw firmware image named
  `.img`, `.rom`, or with no extension at all (selectable via the file
  picker's "All files" option) now correctly gets the base address it
  needs, instead of silently getting none just because its name isn't
  literally `.bin`.
- **SLCAN line-length validation**: a received line whose actual length
  doesn't match what its own declared DLC implies is now treated as
  malformed and skipped, rather than parsed from its first N hex
  characters regardless of what follows.
- **Page-ACK retries are more than a passive wait**: each retry now also
  checks the bootloader's own heartbeat (already sent roughly once a
  second) against what receiving the current page in full would imply -
  when they're consistent, the log says so, which is real evidence the
  data got through and only the ACK was lost, not just a longer wait and
  a hope.

## 14. Optional EEPROM erase before flashing

Section 3 has a checkbox, **"Also erase the persistence EEPROM before
flashing"** - off by default. If checked, it sends the magic-payload
erase command (`0x192` - see `CANBUS.TXT`) to the board's onboard
FL24LC64 persistence EEPROM before the update sequence starts, wiping
whatever tool-parameter state it had saved.

**Not required for a normal update.** A version mismatch in the saved
record's own layout is already detected and safely ignored on the next
boot (see `firmware/README.md`'s parameter-persistence section) - this
checkbox exists for a genuinely clean slate, not because skipping it
would leave anything broken.

**Only works while the application is running** - the bootloader itself
doesn't handle `0x192` at all, only `STM32F303CC.C` does. This checkbox
is silently skipped (with a log line explaining why) if the "board is
currently running the application" checkbox above it is unchecked, since
in that case the board's assumed to already be sitting in the bootloader.

**A missing confirmation doesn't stop the flash.** If the erase command's
own confirmation frame doesn't come back within 2 seconds, this is logged
as a warning and the actual firmware update proceeds anyway - erasing is
a secondary, optional step alongside the real point of this tool, not
something that should abort an otherwise-successful update over its own
confirmation frame going missing. Check the EEPROM state separately
(`URTC Tester`'s own Query State button) if that matters to you.

## Changing the HMAC key / HardwareID

The shared signing key lives in two places that must always match:
`BOOTLOADER.C`'s `HMAC_KEY` array, and this tool's `HMAC_KEY` constant near
the top of `urtc_flasher.py`. If you change one, change the other and
rebuild/reflash the bootloader before trying to sign anything with the new
key - an image signed with a key the bootloader doesn't have will always
fail verification, safely, with the main slot left untouched.

**Or override any of this without touching the script:** an optional
`urtc_config.json` next to `firmware/` can set the signing key, the
HardwareID, and the memory-map values - useful for a different board
revision, a rotated key, or (for the memory-map fields) adapting this
tool to a different chip variant or partition scheme, without needing a
new script version per deployment:
```json
{
  "hardware_id": "0x0303CC01",
  "hmac_key_hex": "555254432D4859445241...",
  "app_max_size": 114688,
  "bootloader_max_size": 32768,
  "flash_page_size": 2048,
  "bootloader_flash_addr": "0x08000000",
  "app_flash_addr": "0x08008000"
}
```
Every field is optional - only override what's actually changing. Missing
file falls back silently to the compiled-in defaults; a present-but-broken
file logs a warning and also falls back, rather than crashing the tool
over a typo. Whichever source is active gets logged at startup, so it's
always visible which values a given session actually used.
