#!/usr/bin/env python3
"""
URTC Flasher - cross-platform firmware update tool for the URTC board
Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>

Licensed under the GNU General Public License v3.0 (GPL-3.0), matching the
URTC firmware itself. See LICENSE in the repository root.

Flashes a compiled URTC application .bin file to a board over CAN bus.
Implements the exact CAN bootloader protocol documented in CANBUS.TXT:
HardwareID check, HMAC-SHA256 signing, the golden-image backup-slot update
flow, and live progress via the bootloader's heartbeat messages.

Two transports are supported, both talking the same protocol:

  1. Serial / SLCAN - works on Windows, Linux, and macOS. Needs a USB-CAN
     adapter running SLCAN firmware (e.g. a CANable Pro v2 flashed with a
     "candleLight"->slcan alternative firmware, or any other adapter
     presenting an SLCAN-compatible virtual COM port / /dev/tty* device).

  2. SocketCAN - Linux only, not shown on other platforms. Talks directly
     to a kernel can0/slcan0 network interface via a raw AF_CAN socket -
     no serial layer, no firmware reflash needed on adapters that already
     support Linux's native gs_usb driver (most CANable-family boards ship
     this way by default). The interface itself must already be brought up
     at 500 kbit/s at the OS level first - see the README's Linux section.

See this file's own "SLCAN FIRMWARE NOTE" and "SOCKETCAN NOTE" comments
(in flasher_transports.py) and the README's tool section, for setup
details on each path.

This is the entry point only - CLI argument handling, the splash screen,
and main-window setup. Everything else lives in its own module:
flasher_config.py (config/language/constants), flasher_transports.py
(SLCAN/SocketCAN/MockCAN), flasher_swd_tools.py (CubeProgrammer/pyOCD),
flasher_validation.py (firmware file validation), flasher_protocol.py
(the CAN OTA state machine), and flasher_gui.py (the main window).
"""

import sys
import os
import argparse

# tkinter (and, with it, flasher_gui.py, which imports tkinter itself) is
# only imported when NOT running in --cli mode - an unconditional
# module-level import would mean ModuleNotFoundError on a genuinely
# headless system (a CI runner, a server, a Raspberry Pi with no
# python3-tk installed) even when --cli is explicitly requested, defeating
# the entire point of having a CLI mode, since that path never touches
# tkinter at all. Checking sys.argv directly here (rather than waiting for
# argparse further down) is deliberate: this has to happen before the
# import is ever attempted.
if "--cli" not in sys.argv:
    import tkinter as tk

try:
    import serial
    import serial.tools.list_ports
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False

# SocketCAN (socket.AF_CAN) is only ever present in Python's stdlib on
# Linux - it doesn't exist as an attribute on Windows/macOS builds, so this
# check alone is enough to gate the whole feature without an explicit
# sys.platform test too. No extra pip package needed for the raw-socket
# approach used here - just the standard library.
import socket
HAVE_SOCKETCAN = hasattr(socket, "AF_CAN")

if not HAVE_SERIAL and not HAVE_SOCKETCAN:
    print("This tool needs pyserial for serial/SLCAN support. Install it with:")
    print("    pip install pyserial")
    print("(SocketCAN support, Linux only, would still work without pyserial,")
    print(" but no transport at all is available on this system as-is.)")
    # Raising rather than sys.exit(1) here - the latter would kill the
    # entire Python process on import, even for a programmatic importer
    # (e.g. a test script doing `import urtc_flasher` to inspect
    # constants) that has no way to catch and handle a SystemExit at
    # import time the way it could catch this.
    raise RuntimeError("No CAN transport available: neither pyserial nor SocketCAN is present")
elif not HAVE_SERIAL:
    print("NOTE: pyserial isn't installed, so Serial/SLCAN mode is unavailable.")
    print("SocketCAN mode will still work. To also enable Serial/SLCAN:")
    print("    pip install pyserial")

from flasher_config import _, _CONFIG_LOADED, FLASHER_VERSION, BANNER_IMAGE_PATH, _center_geometry
from flasher_transports import SLCAN, SLCANError, SocketCAN, SocketCANError, MockCAN
from flasher_protocol import URTCFlasher, FlashError
from flasher_validation import validate_firmware_file

def _cli_log(msg):
    print(msg, flush=True)


def run_cli(argv):
    """Headless CAN OTA update, no GUI/tkinter involved - for CI pipelines,
    test benches, or production-line scripting where there's no display.
    Only covers the CAN update path (section 1-3 of the GUI); the SWD/JTAG
    full-chip path is deliberately GUI-only for now, given how much more
    is at stake if a scripted run gets a wrong file/target combination
    wrong with nobody watching.

    Exit codes: 0 success, 1 protocol/connection error, 2 bad arguments
    or invalid firmware file, 130 cancelled (Ctrl+C).
    """
    parser = argparse.ArgumentParser(
        prog="urtc_flasher.py --cli",
        description="Headless URTC CAN OTA firmware update (no GUI).",
    )
    parser.add_argument("--transport", choices=["serial", "socketcan", "mock"], default="serial",
                         help="Serial/SLCAN (default, all platforms), SocketCAN (Linux only), "
                              "or mock (simulated in-memory bootloader, for testing this tool's "
                              "own logic - not for use against a real board)")
    parser.add_argument("--port",
                         help="Serial port (e.g. COM3 or /dev/ttyACM0) for --transport serial, "
                              "or interface name (e.g. can0) for --transport socketcan. "
                              "Not needed for --transport mock.")
    parser.add_argument("--file", required=True, help="Firmware .bin file to flash")
    parser.add_argument("--no-trigger", action="store_true",
                         help="Skip the 0x7F0 bootloader-entry trigger - use this if the board "
                              "is already sitting in the bootloader (fresh JTAG flash, or no "
                              "valid application currently present)")
    parser.add_argument("--force", action="store_true",
                         help="Flash even if the file fails the plausibility check")
    parser.add_argument("--mock-fail", type=lambda s: int(s, 0), default=None, metavar="REASON",
                         help="With --transport mock: simulate a verify failure with this "
                              "VERIFY_FAIL_REASON_* value (e.g. 0x03 for HMAC mismatch) instead "
                              "of a successful update. Ignored for real transports.")
    args = parser.parse_args(argv)

    if args.transport != "mock" and not args.port:
        _cli_log("ERROR: --port is required for --transport serial/socketcan.")
        return 2

    if _CONFIG_LOADED:
        _load_config_overrides(_cli_log)  # re-run just to emit its own log line - values don't change

    if args.transport == "socketcan" and not HAVE_SOCKETCAN:
        _cli_log("ERROR: --transport socketcan requires Linux with socket.AF_CAN support.")
        return 2
    if args.transport == "serial" and not HAVE_SERIAL:
        _cli_log("ERROR: --transport serial requires pyserial (pip install pyserial).")
        return 2

    is_valid, reason, size = validate_firmware_file(args.file)
    _cli_log(f"Firmware: {args.file} ({size} bytes) - {reason}")
    if not is_valid and not args.force:
        _cli_log("Refusing to flash a file that fails the plausibility check "
                  "(pass --force to override).")
        return 2

    transport = None
    try:
        if args.transport == "serial":
            transport = SLCAN(args.port, log=_cli_log)
        elif args.transport == "socketcan":
            transport = SocketCAN(args.port, log=_cli_log)
        else:
            transport = MockCAN(simulate_failure=args.mock_fail, log=_cli_log)
            _cli_log("Using the mock transport - nothing here is touching real hardware.")
        transport.open_channel()
        _cli_log(f"Connected via {args.transport}" + (f" on {args.port}." if args.port else "."))

        flasher = URTCFlasher(transport, log=_cli_log, progress_cb=lambda pct: None)
        if not args.no_trigger:
            flasher.trigger_bootloader_entry()
        flasher.flash(args.file)
        _cli_log("SUCCESS: firmware update complete.")
        return 0
    except KeyboardInterrupt:
        _cli_log("Cancelled.")
        return 130
    except (FlashError, SLCANError, SocketCANError) as e:
        _cli_log(f"FAILED: {e}")
        return 1
    except Exception as e:
        _cli_log(f"UNEXPECTED ERROR: {e}")
        return 1
    finally:
        if transport is not None:
            try:
                transport.close()
            except Exception:
                pass



def _show_splash_then(root, on_done):
    """Shows the banner centered on screen for 5s, then calls on_done() and
    closes the splash. Built as a borderless Toplevel rather than the
    banner being part of the main window itself, so the main window can
    stay smaller - the banner only needs screen space for those first 5
    seconds, not for the entire session. Skipped straight to on_done() if
    the banner image can't be loaded for any reason (missing file, etc.) -
    a missing splash was never worth blocking startup over.
    """
    try:
        banner_img = tk.PhotoImage(file=BANNER_IMAGE_PATH)
    except (tk.TclError, OSError):
        on_done()
        return

    splash = tk.Toplevel(root)
    splash.withdraw()  # stays hidden until correctly positioned below - see the note above
    splash.overrideredirect(True)  # no title bar/border - a splash isn't a normal window
    splash.configure(bg="#14171C")
    label = tk.Label(splash, image=banner_img, bg="#14171C", borderwidth=0)
    label.image = banner_img  # keep a reference - PhotoImage is garbage-collected otherwise, a classic Tkinter gotcha
    label.pack()

    # banner_img.width()/height() are known immediately from the loaded
    # image itself - not dependent on the label/splash having already
    # been drawn, unlike winfo_width()/height() on the widget.
    splash.geometry(_center_geometry(splash, banner_img.width(), banner_img.height()))
    splash.attributes("-topmost", True)
    splash.deiconify()  # only shown now that it's already correctly positioned - avoids
                        # ever actually appearing at the default (often top-left) position
                        # first, a real Tkinter/Windows quirk with overrideredirect windows

    def _finish():
        splash.destroy()
        on_done()

    splash.after(5000, _finish)


def main():
    if "--cli" in sys.argv:
        argv = [a for a in sys.argv[1:] if a != "--cli"]
        sys.exit(run_cli(argv))
    from flasher_gui import FlasherGUI  # deferred: only needed once --cli is ruled out above, matching the tkinter import's own reasoning at the top of this file
    root = tk.Tk()
    root.withdraw()  # hidden until the splash finishes, rather than flashing empty then populated
    # Sized from actual measured content across all 5 supported languages
    # (root.winfo_reqwidth/reqheight after building the real GUI, not a
    # guess) - Italian needs the most width (914px measured), French/German
    # the most height (~967px measured), both with help text at the same
    # wraplength as English. A small margin above those worst-case
    # measurements, rather than sizing for English alone and letting other
    # languages clip.
    # Sized from actual measured content across all 5 supported languages
    # (root.winfo_reqwidth/reqheight after building the real GUI, not a
    # guess) - re-measured after redesigning the CAN-OTA tab into 2
    # columns (firmware selection + flashing on the left, free tool
    # config + peripheral info on the right) instead of everything
    # stacked in one column. Much closer to square now: 1221x1022
    # measured (worst case: Spanish for width, German for height),
    # down from 914x1235 when everything was stacked vertically. A
    # small margin above those worst-case measurements.
    root.geometry(_center_geometry(root, 1235, 1040))
    app = FlasherGUI(root)

    def _reveal_main():
        root.deiconify()

    _show_splash_then(root, _reveal_main)
    root.mainloop()


if __name__ == "__main__":
    main()
