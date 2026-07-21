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
below, and the README's tool section, for setup details on each path.
"""

import sys
import os
import time
import re
import logging
import argparse
import json
import struct
import zlib
import hashlib
import hmac
import threading
import glob
import socket
import subprocess
import shutil
import zipfile
import platform
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

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
HAVE_SOCKETCAN = hasattr(socket, "AF_CAN")

if not HAVE_SERIAL and not HAVE_SOCKETCAN:
    print("This tool needs pyserial for serial/SLCAN support. Install it with:")
    print("    pip install pyserial")
    print("(SocketCAN support, Linux only, would still work without pyserial,")
    print(" but no transport at all is available on this system as-is.)")
    sys.exit(1)
elif not HAVE_SERIAL:
    print("NOTE: pyserial isn't installed, so Serial/SLCAN mode is unavailable.")
    print("SocketCAN mode will still work. To also enable Serial/SLCAN:")
    print("    pip install pyserial")

# =============================================================================
# Protocol constants - must match BOOTLOADER.C exactly
# =============================================================================
CAN_ID_ENTER_BOOTLOADER = 0x7F0
CAN_ID_START_UPDATE     = 0x7F1
CAN_ID_DATA             = 0x7F2
CAN_ID_PAGE_ACK         = 0x7F3
CAN_ID_END_UPDATE       = 0x7F4
CAN_ID_STATUS           = 0x7F5
CAN_ID_HEARTBEAT        = 0x7F6
CAN_ID_HMAC_CHUNK       = 0x7F7
CAN_ID_QUERY_VERSION    = 0x7F8
CAN_ID_VERSION_RESPONSE = 0x7F9
CAN_ID_QUERY_EEPROM_STATE = 0x190  # Query the FL24LC64's recovered state (application-side, not the bootloader)
CAN_ID_EEPROM_STATE_RESP = 0x191   # Answers CAN_ID_QUERY_EEPROM_STATE, also sent after an erase
CAN_ID_ERASE_EEPROM = 0x192        # Magic-payload erase - see ERASE_EEPROM_MAGIC below
ERASE_EEPROM_MAGIC = bytes([0xE3, 0xA5, 0xE0, 0xFF])
CAN_ID_BOOTLOADER_VERSION_RESPONSE = 0x7FA  # sent only by the bootloader, alongside 0x7F9, when it's the one answering

STATUS_NAMES = {
    0x01: "Listening",
    0x02: "Erasing backup slot",
    0x03: "Receiving firmware data",
    0x06: "Verifying (size/CRC32/HMAC/HardwareID)",
    0x07: "Copying backup into main slot",
    0x04: "Verify OK - jumping to new firmware",
    0x05: "Verify FAILED - main slot untouched",
    0xFF: "Error",
}

# Matches BOOTLOADER.C's VERIFY_FAIL_REASON_* defines exactly - present as
# a second byte on STATUS_VERIFY_FAIL (0x05) frames specifically, DLC=2
# instead of the usual DLC=1, so a failed update can say WHY rather than
# just THAT. Byte[0] is still 0x05 regardless, so any code that
# only reads that first byte keeps working unchanged.
VERIFY_FAIL_REASONS = {
    0x01: "incomplete transfer (didn't receive the declared size or all 4 HMAC chunks)",
    0x02: "CRC32 mismatch (transfer corruption)",
    0x03: "HMAC signature mismatch (not signed with this project's key)",
    0x04: "HardwareID mismatch (image built for different hardware)",
}

# THIS_HARDWARE_ID and HMAC_KEY must match BOOTLOADER.C exactly, or every
# update this tool sends will be rejected (HardwareID mismatch) or fail
# signature verification (HMAC mismatch). If you change the key in
# BOOTLOADER.C, change it here too - the two are not automatically kept in
# sync.
THIS_HARDWARE_ID = 0x0303CC01  # STM32F303CCT6, URTC board revision 1
FIRMWARE_VERSION_MAJOR = 1
FIRMWARE_VERSION_MINOR = 0

# This tool's OWN version (shown in the banner and window title) -
# deliberately separate from FIRMWARE_VERSION_MAJOR/MINOR above, which is
# the *board firmware's* version this tool writes into the end-update
# frame. The two will often move together but aren't the same number by
# definition - this one's about the flasher script itself.
FLASHER_VERSION = "1.0"
FLASHER_AUTHOR = "JuanenRac"
HMAC_KEY = bytes([
    0x55, 0x52, 0x54, 0x43, 0x2D, 0x48, 0x59, 0x44,
    0x52, 0x41, 0x2D, 0x55, 0x4D, 0x43, 0x2D, 0x32,
    0x30, 0x32, 0x36, 0x2D, 0x43, 0x48, 0x41, 0x4E,
    0x47, 0x45, 0x2D, 0x4D, 0x45, 0x2D, 0x21, 0x21
])

APP_MAX_SIZE = 112 * 1024
FLASH_PAGE_SIZE = 2048
BITRATE_500K_SLCAN_CODE = "6"  # SLCAN's "Sx" bitrate codes: 6 = 500 kbit/s
# Full standard SLCAN/Lawicel bitrate code table, for the bitrate selector
# and auto-detect (see FlasherGUI.auto_detect_bitrate). 500k stays the
# default everywhere else in this file - URTC's own bus is fixed at 500k -
# but a mis-set adapter or a non-standard board might need a different one.
SLCAN_BITRATES = [
    ("10 kbit/s", "0"), ("20 kbit/s", "1"), ("50 kbit/s", "2"),
    ("100 kbit/s", "3"), ("125 kbit/s", "4"), ("250 kbit/s", "5"),
    ("500 kbit/s", "6"), ("800 kbit/s", "7"), ("1 Mbit/s", "8"),
]

# Full-chip SWD/JTAG programming (see SWDFlasher below) - same two fixed
# addresses used throughout BOOTLOADER.C (MAIN_APP_ADDR) and the README's
# documented JTAG bring-up procedure.
BOOTLOADER_FLASH_ADDR = 0x08000000
APP_FLASH_ADDR = 0x08008000
BOOTLOADER_MAX_SIZE = 32 * 1024  # matches BOOTLOADER.C's own 32KB region, 0x08000000-0x08008000
# Verify this against `pyocd list --targets --name stm32f303` on the
# machine actually running this - STM32 coverage in pyOCD is broad, but
# this exact string wasn't confirmed against a live pyOCD install while
# writing this. If it's not found, `pyocd pack install stm32f303cc`
# (or the closest match `pyocd pack find` shows) pulls the CMSIS-Pack.
PYOCD_TARGET_NAME = "stm32f303cc"

# firmware/ lives INSIDE tools/, not as a sibling - this keeps the whole
# tools/ folder self-contained. Someone who just wants to flash a board
# doesn't need the rest of the repo (schematics, 3D files, docs) - they can
# copy tools/ on its own (a USB stick, a shared network folder, wherever)
# and it still works, since the firmware it needs travels with it.
#
# sys.frozen / sys.executable handling: PyInstaller's --onefile mode
# extracts the bundled app into a temporary folder at runtime, and __file__
# inside that running code points into THAT temp folder, not to wherever
# the actual .exe sits on disk - so building the path from __file__ finds a
# firmware/ folder that doesn't exist (or worse, a stale one from a
# previous run, since PyInstaller doesn't always clean these up
# immediately). sys.executable, by contrast, is the real path to the
# running .exe itself, which is what "next to the exe" actually means.
if getattr(sys, "frozen", False):
    base_dir = os.path.dirname(sys.executable)
else:
    base_dir = os.path.dirname(os.path.abspath(__file__))
FIRMWARE_FOLDER = os.path.normpath(os.path.join(base_dir, "firmware"))
LOGS_FOLDER = os.path.normpath(os.path.join(base_dir, "logs"))
CONFIG_FILE_PATH = os.path.normpath(os.path.join(base_dir, "urtc_config.json"))


def _load_config_overrides(log=None):
    """Optional urtc_config.json next to firmware/ can override HMAC_KEY,
    THIS_HARDWARE_ID, and the memory-map constants (slot sizes, flash page
    size, base addresses) without needing to edit or rebuild this script -
    useful for a different board revision, a rotated signing key, or (for
    the memory-map values) adapting this tool to a different chip variant
    or partition scheme down the line. Missing file is normal and silent
    (falls back to the compiled-in defaults above); a present-but-invalid
    file is logged and then also falls back to defaults, rather than
    crashing the whole tool over a typo in a config file.

    Expected format (every key optional - only override what's actually
    changing):
        {
          "hardware_id": "0x0303CC01",
          "hmac_key_hex": "555254432D...",
          "app_max_size": 114688,
          "bootloader_max_size": 32768,
          "flash_page_size": 2048,
          "bootloader_flash_addr": "0x08000000",
          "app_flash_addr": "0x08008000"
        }
    """
    log = log or (lambda msg: None)
    hw_id, key = THIS_HARDWARE_ID, HMAC_KEY
    app_max_size, bootloader_max_size = APP_MAX_SIZE, BOOTLOADER_MAX_SIZE
    flash_page_size = FLASH_PAGE_SIZE
    bootloader_addr, app_addr = BOOTLOADER_FLASH_ADDR, APP_FLASH_ADDR
    defaults = (hw_id, key, app_max_size, bootloader_max_size, flash_page_size, bootloader_addr, app_addr, False)
    if not os.path.isfile(CONFIG_FILE_PATH):
        return defaults

    try:
        with open(CONFIG_FILE_PATH, "r") as f:
            cfg = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        # Nothing to salvage here - the file couldn't even be read as JSON
        # at all, so there's no per-field data to fall back on individually.
        log(f"WARNING: couldn't load {CONFIG_FILE_PATH} ({e}) - using compiled-in defaults instead.")
        return defaults

    # Each field is parsed independently below - a mistake in one (a typo
    # in a rarely-touched memory-map value, say) only reverts THAT field to
    # its compiled-in default and logs which one, rather than discarding
    # every other field that parsed correctly, including a HMAC key or
    # HardwareID the user got right.
    overridden = []
    skipped = []

    def _int_field(name, current):
        # Same string-or-number tolerance as hardware_id below - a
        # hand-edited JSON config is just as likely to have either.
        if name not in cfg:
            return current
        try:
            val = cfg[name]
            result = int(val, 0) if isinstance(val, str) else int(val)
            overridden.append(name)
            return result
        except (ValueError, TypeError) as e:
            skipped.append(f"{name} ({e})")
            return current

    if "hardware_id" in cfg:
        try:
            val = cfg["hardware_id"]
            # Accepts either a JSON string ("0x0303CC01" or a plain decimal
            # string) or a plain JSON number (50580689) - both are natural
            # ways to write this in a hand-edited config file, and int()'s
            # own two-argument form only accepts a string for the first
            # argument, raising TypeError on a bare JSON number otherwise.
            hw_id = int(val, 0) if isinstance(val, str) else int(val)
            overridden.append("hardware_id")
        except (ValueError, TypeError) as e:
            skipped.append(f"hardware_id ({e})")
    if "hmac_key_hex" in cfg:
        try:
            candidate_key = bytes.fromhex(cfg["hmac_key_hex"])
            if len(candidate_key) != 32:
                raise ValueError(f"must decode to 32 bytes, got {len(candidate_key)}")
            key = candidate_key
            overridden.append("hmac_key_hex")
        except (ValueError, TypeError) as e:
            skipped.append(f"hmac_key_hex ({e})")
    app_max_size = _int_field("app_max_size", app_max_size)
    bootloader_max_size = _int_field("bootloader_max_size", bootloader_max_size)
    flash_page_size = _int_field("flash_page_size", flash_page_size)
    bootloader_addr = _int_field("bootloader_flash_addr", bootloader_addr)
    app_addr = _int_field("app_flash_addr", app_addr)

    if overridden:
        log(f"Loaded config overrides from {CONFIG_FILE_PATH}: {', '.join(overridden)}")
    if skipped:
        log(f"WARNING: {CONFIG_FILE_PATH} had problems with: {'; '.join(skipped)} "
            f"- those specific fields fell back to their compiled-in defaults, "
            f"everything else above still applied.")
    return (hw_id, key, app_max_size, bootloader_max_size, flash_page_size,
            bootloader_addr, app_addr, bool(overridden))


# Applied at import time with no logger (silent) so every other module-level
# constant below sees the right values immediately; FlasherGUI.__init__ and
# run_cli() both re-run this WITH logging once a log sink actually exists,
# purely so the override (or its absence) is visible in the session log/CLI
# output - the values themselves don't change on the second call.
THIS_HARDWARE_ID, HMAC_KEY, APP_MAX_SIZE, BOOTLOADER_MAX_SIZE, FLASH_PAGE_SIZE, \
    BOOTLOADER_FLASH_ADDR, APP_FLASH_ADDR, _CONFIG_LOADED = _load_config_overrides()

# The banner image is a bundled asset, not a user-supplied file - a
# genuinely different location than FIRMWARE_FOLDER above when frozen.
# PyInstaller's --onefile mode extracts files added via --add-data to a
# temporary directory at sys._MEIPASS, separate from wherever the .exe
# itself sits (that's base_dir, used above for firmware/ specifically
# because it's meant to be user-editable without a rebuild).
if getattr(sys, "frozen", False):
    _assets_base = getattr(sys, "_MEIPASS", base_dir)
else:
    _assets_base = os.path.dirname(os.path.abspath(__file__))
BANNER_IMAGE_PATH = os.path.normpath(os.path.join(_assets_base, "assets", "urtc_banner.png"))
ICON_IMAGE_PATH = os.path.normpath(os.path.join(_assets_base, "assets", "urtc_icon.png"))

# =============================================================================
# SLCAN transport - talks to the adapter over its virtual COM port using the
# standard SLCAN ASCII protocol (the same one used by Lawicel adapters and
# widely supported by open-source USB-CAN firmware, including common
# alternative firmware for CANable-family boards).
#
# SLCAN FIRMWARE NOTE: a CANable Pro v2 ships by default with "candleLight"
# firmware, which presents itself over USB using the gs_usb protocol (what
# Linux's SocketCAN gs_usb driver expects) - NOT a serial port, and NOT what
# this tool speaks. For this tool to see the adapter as a COM port at all,
# it needs to be running SLCAN-compatible firmware instead. See this
# project's README for links to the firmware and the flashing steps: this
# is a one-time setup on the adapter itself, unrelated to and separate from
# flashing the URTC board.
# =============================================================================
class SLCANError(Exception):
    pass


class SLCAN:
    # Fixed OS-level read timeout, set once at connection and never
    # changed again - every logical "wait up to N seconds" from callers is
    # managed purely in software (see read_frame's deadline loop below),
    # rather than by repeatedly reassigning self.ser.timeout. Reassigning a
    # pyserial Serial's timeout reaches down to the OS (SetCommTimeouts on
    # Windows, tcsetattr on POSIX) - harmless occasionally, but doing it
    # from inside a hot read loop has been reported to cause byte loss on
    # some older/loaded USB-serial drivers (CH340, FTDI under load) if
    # different callers ask for different timeouts back to back. A short,
    # fixed value here means read_until below returns quickly and often,
    # letting the software deadline decide when to actually give up.
    _OS_READ_TIMEOUT = 0.02

    def __init__(self, port, baud=115200, log=None):
        self.log = log or (lambda msg: None)
        self.ser = serial.Serial(port, baudrate=baud, timeout=self._OS_READ_TIMEOUT)
        self._rx_buf = b""  # persists across read_frame() calls - see read_frame's own comment for why
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def _send_raw(self, cmd):
        self.ser.write((cmd + "\r").encode("ascii"))

    def open_channel(self, bitrate_code=BITRATE_500K_SLCAN_CODE):
        # Close first in case it was already open from a previous session
        # that didn't shut down cleanly - SLCAN adapters reject "O" (open)
        # if already open, which would otherwise abort every connection
        # attempt after the first.
        self._send_raw("C")
        time.sleep(0.05)
        self.ser.reset_input_buffer()
        self._send_raw("S" + bitrate_code)
        time.sleep(0.05)
        self._send_raw("O")
        time.sleep(0.1)
        self.ser.reset_input_buffer()

    def close_channel(self):
        try:
            self._send_raw("C")
        except Exception:
            pass

    def close(self):
        """Uniform teardown method, matching SocketCAN's close() below, so
        the GUI can disconnect without caring which transport it's holding."""
        self.close_channel()
        try:
            self.ser.close()
        except Exception:
            pass

    def send_frame(self, can_id, data):
        # SLCAN standard-frame transmit: "t" + 3 hex ID digits + 1 hex DLC
        # digit + DLC*2 hex data digits, e.g. t7F108 + 8 bytes = t7F1081122334455667788
        if can_id > 0x7FF:
            raise SLCANError(f"ID 0x{can_id:X} exceeds standard 11-bit range")
        if len(data) > 8:
            raise SLCANError("CAN data payload cannot exceed 8 bytes")
        frame = f"t{can_id:03X}{len(data):01X}" + "".join(f"{b:02X}" for b in data)
        self._send_raw(frame)

    def read_frame(self, timeout=0.05):
        # Returns (can_id, data_bytes) or None if nothing arrived within
        # timeout. SLCAN receive frames look the same as transmit ones
        # ("tIIILDD...") terminated by \r. Real adapters also emit
        # non-frame status characters on this same line - 'z'/'Z' after
        # confirming a transmitted frame went out, '\a' (BELL) on a bus
        # error - which get consumed and skipped here rather than treated
        # as "nothing arrived", so a burst of transmit-acks from the pages
        # just sent doesn't make this return early before the real
        # response frame shows up.
        #
        # self.ser.timeout is never touched here - it's fixed once, in
        # __init__, at _OS_READ_TIMEOUT. Bytes are read one at a time and
        # held in self._rx_buf, a buffer that PERSISTS across separate
        # calls to this method - a real USB-serial link can have enough
        # latency that a single line's bytes arrive split across more than
        # one of these short OS-level read windows; pulling a complete
        # line only when self._rx_buf actually contains one (rather than
        # expecting any single underlying read to capture a whole line by
        # itself) means a slow-arriving line still gets read correctly
        # instead of being split across two reads and silently lost as two
        # separate unparseable fragments.
        deadline = time.time() + timeout
        while time.time() < deadline:
            if b"\r" not in self._rx_buf:
                chunk = self.ser.read(1)
                if not chunk:
                    continue  # OS-level read timed out with nothing yet - keep going until our own deadline
                self._rx_buf += chunk
                continue
            line_bytes, self._rx_buf = self._rx_buf.split(b"\r", 1)
            line = line_bytes.decode("ascii", errors="ignore").strip()
            if not line or line[0] not in ("t", "T"):
                continue  # status/ack character (z, Z, BELL, ...) - keep listening
            try:
                if line[0] == "t":
                    can_id = int(line[1:4], 16)
                    dlc = int(line[4:5], 16)
                    data_start = 5
                else:  # 'T' = extended 29-bit ID, not used by this protocol but handled for completeness
                    can_id = int(line[1:9], 16)
                    dlc = int(line[9:10], 16)
                    data_start = 10
                expected_len = data_start + dlc * 2
                if len(line) != expected_len:
                    continue  # length doesn't match what this line's own DLC implies - malformed, keep listening
                data = bytes(int(line[data_start + i*2:data_start + i*2 + 2], 16) for i in range(dlc))
                return (can_id, data)
            except (ValueError, IndexError):
                continue  # malformed line - keep listening rather than aborting the whole wait
        return None


# =============================================================================
# SocketCAN transport - Linux only. Talks directly to a kernel can0/slcan0
# network interface via a raw AF_CAN socket, matching the exact same
# send_frame/read_frame/open_channel/close_channel/close interface as SLCAN
# above, so URTCFlasher (and the GUI) can use either one interchangeably
# without caring which is underneath.
#
# SOCKETCAN NOTE: unlike SLCAN, the bitrate here is NOT something this
# application sets - it's a property of the kernel network interface
# itself, brought up once (usually needing sudo, since it's a network
# device configuration change) with something like:
#     sudo ip link set can0 type can bitrate 500000
#     sudo ip link set can0 up
# ...or for a CANable-style adapter that enumerates as slcan0 through
# slcand instead of a native can0, see the README's Linux section. Either
# way, by the time this class's constructor runs, the interface is expected
# to already exist and be up at 500 kbit/s - this class only binds to it,
# it doesn't configure it.
# =============================================================================
def list_socketcan_interfaces():
    """Scans /sys/class/net for CAN-type interfaces (ARPHRD_CAN = 280 in
    if_arp.h) - no subprocess call to `ip`, no extra dependency, just a
    directory listing and a one-line file read per candidate. Returns an
    empty list on any non-Linux system, or if none are found."""
    interfaces = []
    net_dir = "/sys/class/net"
    if not os.path.isdir(net_dir):
        return interfaces
    for name in sorted(os.listdir(net_dir)):
        type_path = os.path.join(net_dir, name, "type")
        try:
            with open(type_path) as f:
                if f.read().strip() == "280":
                    interfaces.append(name)
        except OSError:
            continue
    return interfaces


class SocketCANError(Exception):
    pass


class SocketCAN:
    # Matches Linux's struct can_frame from <linux/can.h>:
    #   __u32 can_id; __u8 can_dlc; __u8 pad[3]; __u8 data[8];
    _FRAME_FMT = "=IB3x8s"
    _FRAME_SIZE = struct.calcsize(_FRAME_FMT)

    @staticmethod
    def read_interface_stats(interface):
        """Basic interface-level error/drop counters from sysfs - a plain
        file read, no netlink call or extra dependency. This is NOT the
        same as a true CAN bus-load percentage or the controller's own
        REC/TEC error counters (those need a netlink query this project
        deliberately doesn't add a dependency for) - it's the more general
        network-interface statistics every Linux interface exposes,
        which still gives useful signal (rx/tx counts moving at all,
        error/drop counts climbing) without needing anything beyond the
        standard library. Returns a dict, or None if the interface/stats
        aren't readable (wrong name, interface removed, non-Linux, etc.)."""
        base = f"/sys/class/net/{interface}/statistics"
        if not os.path.isdir(base):
            return None
        fields = ["rx_packets", "tx_packets", "rx_errors", "tx_errors",
                  "rx_dropped", "tx_dropped", "collisions"]
        stats = {}
        for field in fields:
            try:
                with open(os.path.join(base, field)) as f:
                    stats[field] = int(f.read().strip())
            except (OSError, ValueError):
                return None
        return stats

    @staticmethod
    def check_carrier(interface):
        """Reads /sys/class/net/<iface>/carrier - a plain 0/1 file every
        Linux network interface exposes. Genuinely relevant here: when a
        CAN controller goes bus-off, the kernel driver calls
        netif_carrier_off() (confirmed kernel behavior, not a guess), so
        "no carrier" is real, if indirect, evidence of a bus-off or
        similarly dead link - not just "the file happened to say 0".
        Doesn't distinguish bus-off from a simply-unplugged/down
        interface (that finer distinction needs a netlink query this
        project doesn't have a dependency-free way to make), but either
        way "no carrier" means nothing will get through right now.
        Returns True (carrier up), False (no carrier), or None (couldn't
        read - interface missing, non-Linux, etc., not itself a sign of
        a problem)."""
        path = f"/sys/class/net/{interface}/carrier"
        try:
            with open(path) as f:
                return f.read().strip() == "1"
        except OSError:
            return None

    def __init__(self, interface, log=None):
        self.log = log or (lambda msg: None)
        self.interface = interface
        carrier = self.check_carrier(interface)
        if carrier is False:
            # Deliberately doesn't attempt an automatic recovery cycle
            # itself: clearing a real bus-off condition needs the
            # interface taken down and back up at the kernel level (`ip
            # link set ... down` / `... up`), which needs root and counts
            # as modifying system network configuration - not something
            # to do without the user explicitly running it themselves.
            self.log(f"WARNING: {interface} reports no carrier - this is what the kernel "
                      f"shows during a CAN bus-off condition (too many transmit errors) or "
                      f"a similarly dead link. If the adapter is powered and wired to a live "
                      f"bus, try: sudo ip link set {interface} down && "
                      f"sudo ip link set {interface} up type can bitrate 500000 restart-ms 100")
        try:
            self.sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
            self.sock.bind((interface,))
        except OSError as e:
            raise SocketCANError(
                f"couldn't open '{interface}': {e}. Check it exists and is up - "
                f"'ip link show {interface}' - and that your user can access it "
                f"(SocketCAN access typically doesn't need special group "
                f"membership the way a serial port does, but bringing the "
                f"interface up in the first place usually needs sudo)."
            )
        self.sock.settimeout(0.05)

    def open_channel(self, bitrate_code=None):
        # Deliberately a no-op - see the SOCKETCAN NOTE above. The bitrate
        # is already fixed by whoever ran `ip link set ... bitrate ...`
        # before this constructor was ever called; there's no per-connection
        # equivalent of SLCAN's "Sx" command in SocketCAN's raw-socket API.
        pass

    def close_channel(self):
        pass

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    def send_frame(self, can_id, data):
        if can_id > 0x7FF:
            raise SocketCANError(f"ID 0x{can_id:X} exceeds standard 11-bit range")
        if len(data) > 8:
            raise SocketCANError("CAN data payload cannot exceed 8 bytes")
        padded = data + bytes(8 - len(data))
        frame = struct.pack(self._FRAME_FMT, can_id, len(data), padded)
        try:
            self.sock.send(frame)
        except OSError as e:
            raise SocketCANError(f"send failed on '{self.interface}': {e}")

    def read_frame(self, timeout=0.05):
        self.sock.settimeout(timeout)
        try:
            frame = self.sock.recv(self._FRAME_SIZE)
        except socket.timeout:
            return None
        except OSError:
            return None
        can_id_raw, dlc, data = struct.unpack(self._FRAME_FMT, frame)
        # Error/extended/RTR frames must be discarded BEFORE masking to 11
        # bits, not after. SocketCAN packs these as flag bits in the same
        # 32-bit can_id field: bit 31 (0x80000000) = extended ID, bit 30
        # (0x40000000) = remote transmission request, bit 29 (0x20000000) =
        # kernel error frame. This protocol never uses any of the three, so
        # any frame with one of these bits set is bus noise or a kernel
        # error notification, not real protocol data - masking straight to
        # 0x7FF without checking this first could make an error frame's low
        # bits accidentally match a real ID like CAN_ID_PAGE_ACK, and this
        # code would wrongly treat bus trouble as a valid response.
        if can_id_raw & (0x80000000 | 0x40000000 | 0x20000000):
            return None
        can_id = can_id_raw & 0x7FF
        # dlc is clamped to 8 explicitly - classic CAN frames never exceed
        # 8 data bytes, and this socket is never put into CAN-FD mode, but
        # a malformed frame from kernel driver noise reporting a larger
        # dlc shouldn't be trusted at face value even though Python's own
        # slicing already caps silently at the buffer's real 8-byte length.
        return (can_id, data[:min(dlc, 8)])


# =============================================================================
# SWD/JTAG full-chip programming - a DIFFERENT kind of operation from the CAN
# OTA path above, deliberately kept as its own separate classes rather than
# folded into URTCFlasher. The CAN update path is self-healing on any
# interruption - the bootloader's own golden-image logic (see BOOTLOADER.C)
# guarantees the board always has SOME working firmware afterward, with no
# physical access needed to recover. A full-chip SWD write isn't
# self-healing the same way: an interrupted erase/write can leave the
# board with no valid firmware running until it's reprogrammed. That's a
# real difference worth keeping separate in the UI - but it's not a
# "brick" in the permanent sense: the SWD/debug port itself doesn't depend
# on flash contents, so reconnecting and flashing again from scratch
# recovers it, the same way a first-ever bring-up does. Nothing in either
# class below touches option bytes, so there's no path here to the one
# STM32 failure mode that actually IS irreversible (RDP level 2 read
# protection permanently disabling the debug port) - that would need a
# deliberate separate action this code never performs.
#
# DESIGN NOTE - CLI over Python API: pyOCD does expose a Python API
# (FileProgrammer, ConnectHelper, etc.) that would integrate more natively
# with this GUI's progress bar and log widget than shelling out to a
# subprocess. It was deliberately NOT used here. pyOCD's own README
# describes its Python API as "beta quality" that "will be changed" before
# a 1.0 release, while the `pyocd` command-line tool's flags (`flash
# --base-address`, `erase --chip`, etc.) are the documented, stable
# surface. Since none of this can be tested against real hardware in the
# environment that wrote it, the more stable and externally-verifiable
# interface is the safer choice, even at the cost of parsing subprocess
# output instead of getting live Python callbacks. If you'd rather have
# the tighter API integration and are willing to pin an exact pyOCD
# version and verify the calls yourself, that's a reasonable thing to
# switch to later - just not a safe default to guess at here.
#
# BOTH classes below share the same shape (find the executable, run it,
# stream output to the log, raise on nonzero exit) so the GUI can treat
# them interchangeably, the same way SLCAN/SocketCAN share an interface
# for the CAN path.
# =============================================================================
class SWDFlashError(Exception):
    pass


def _find_executable(names):
    """Try each name/path in order via shutil.which (which also handles
    absolute paths that already exist), return the first hit or None."""
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def _address_args_bin_needs_it(path, address, flag_before=None):
    """.hex and .elf/.axf embed their own load address and should be
    passed as-is; anything else is treated as a raw binary needing an
    address passed explicitly - not just files literally named ".bin",
    since the file pickers' "All files" option lets someone select a raw
    image under any extension (.img, .rom, none at all), and a name alone
    doesn't change what format the bytes are actually in. Returns the
    extra CLI tokens to append."""
    if not path.lower().endswith((".hex", ".elf", ".axf")):
        addr_str = f"0x{address:08X}"  # zero-padded to the conventional 8 hex digits for a 32-bit ARM address - hex() alone would drop leading zeros (0x8000000, 7 digits) which some strict CLI parsers could misread
        return ([flag_before, addr_str] if flag_before else [addr_str])
    return []


class _SubprocessProgrammerBase:
    """Shared run/log/error-handling plumbing for both CLI wrappers below."""
    NAME = "programmer"

    # Substrings that mean "this definitely failed" wherever they appear in
    # stdout/stderr, checked case-insensitively. This exists because a real
    # test against STM32CubeProgrammer with no probe connected returned exit
    # code 0 and still logged "complete" - the tool's own exit code turned
    # out not to be a reliable success/failure signal by itself, so output
    # content is checked too. This list is necessarily a floor, not a
    # ceiling (it can't cover every possible failure message from a tool
    # this code doesn't control) - see check_connection() on each subclass
    # below for the other half of the defense: requiring POSITIVE evidence
    # of a real connection before ever proceeding, rather than only
    # screening for known-bad text.
    _FAILURE_INDICATORS = [
        "no st-link", "no st link", "no debug probe", "unable to connect",
        "not detected", "no target", "no target connected", "failed to connect",
        "connection failed", "no probes", "cannot connect", "could not connect",
        "communication error", "error:", "no device found", "target is not responding",
        "mismatch", "does not match", "differ at", "verification failed",
    ]

    def __init__(self, log=None):
        self.log = log or (lambda msg: None)
        self.exe = None  # set by subclass __init__

    def _looks_like_failure(self, text):
        lower = text.lower()
        return any(indicator in lower for indicator in self._FAILURE_INDICATORS)

    def _run(self, args, dry_run, timeout=300, check_output=True):
        if not self.exe:
            raise SWDFlashError(f"{self.NAME} executable not found on this system")
        cmd = [self.exe] + args
        self.log("$ " + " ".join(cmd))
        if dry_run:
            self.log("  (dry run - not executed)")
            return ""
        try:
            proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1,
            )
        except OSError as e:
            raise SWDFlashError(f"couldn't run {self.NAME}: {e}")

        timed_out = threading.Event()
        timer = threading.Timer(timeout, lambda: (timed_out.set(), proc.kill()))
        timer.start()
        lines = []
        try:
            for line in proc.stdout:
                line = line.rstrip("\n")
                lines.append(line)
                self.log("  " + line)
            proc.wait()
        finally:
            timer.cancel()
        combined = "\n".join(lines)
        if timed_out.is_set():
            raise SWDFlashError(f"{self.NAME} timed out after {timeout}s")
        if proc.returncode != 0:
            raise SWDFlashError(
                f"{self.NAME} exited with code {proc.returncode} - see the log above for details"
            )
        if check_output and self._looks_like_failure(combined):
            raise SWDFlashError(
                f"{self.NAME} exited with code 0, but its own output contains a "
                f"failure indicator - treating this as a failure rather than "
                f"trusting the exit code alone. See the log above for the exact text."
            )
        return combined


class PyOCDCLI(_SubprocessProgrammerBase):
    NAME = "pyOCD"

    def __init__(self, log=None):
        super().__init__(log)
        self.exe = _find_executable(["pyocd", "pyocd.exe"])

    @staticmethod
    def available():
        return _find_executable(["pyocd", "pyocd.exe"]) is not None

    def list_probes(self):
        """Returns a list of (uid, description) tuples, or [] if none found
        or the list couldn't be parsed. Confirmed real pyOCD output is a
        plain table:
            #   Probe/Board          Unique ID                 Target
            --------------------------------------------------------
            0   STM32 STLink         2900240008000054574E514E   n/a
        so the UID is simply the column after the row number - if a given
        pyOCD version formats this differently, this returns [] rather
        than guessing, and callers should treat that as "couldn't parse,
        ask the user to check `pyocd list --probes` output directly".
        """
        try:
            output = self._run(["list", "--probes"], dry_run=False, check_output=False)
        except SWDFlashError:
            return []
        probes = []
        for line in output.splitlines():
            m = re.match(r"^\s*\d+\s+(\S.*)$", line)
            if not m:
                continue
            rest = m.group(1).strip()
            parts = re.split(r"\s{2,}", rest)  # columns are separated by 2+ spaces in the real table
            if len(parts) < 2:
                continue
            description, uid = parts[0], parts[1]
            probes.append((uid, description))
        return probes

    def check_connection(self, probe_uid=None, dry_run=False):
        # Checks USB-level probe presence via `pyocd list --probes` -
        # necessary but not sufficient (a probe can be plugged into USB but
        # not wired via SWD to the actual target chip), which is why every
        # later step's output is also screened for failure text, not just
        # this one check trusted blindly.
        #
        # Detection is a POSITIVE match for an actual probe list row -
        # confirmed real output looks like a plain table:
        #   #   Probe/Board          Unique ID                 Target
        #   --------------------------------------------------------
        #   0   STM32 STLink         2900240008000054574E514E  n/a
        # so a genuine entry is a line starting with a row number followed
        # by whitespace and more content, which also naturally skips the
        # header (starts with "#") and the separator (starts with "-").
        self.log("Checking for a connected probe...")
        if dry_run:
            self.log(f"$ {self.exe} list --probes")
            self.log("  (dry run - not executed, skipping connection check)")
            return
        output = self._run(["list", "--probes"], dry_run=False, check_output=False)
        if not re.search(r"^\s*\d+\s+\S", output, re.MULTILINE):
            raise SWDFlashError(
                "No SWD/JTAG probe detected (checked with 'pyocd list --probes' "
                "- expected at least one probe row, found none). Check the "
                "ST-Link/probe is connected via USB before trying again."
            )
        # With more than one probe connected, a command with no --probe
        # targets whichever one the OS happens to enumerate first - not
        # necessarily the board actually meant. Two or more entries with no
        # probe_uid given is refused rather than guessing which one to hit
        # with a chip erase.
        probe_count = len(re.findall(r"^\s*\d+\s+\S", output, re.MULTILINE))
        if probe_count > 1 and not probe_uid:
            raise SWDFlashError(
                f"{probe_count} probes are connected and none was selected - "
                f"refusing to guess which one to run a chip erase against. "
                f"Pick one from the probe list."
            )
        self.log("Probe detected via USB - proceeding. (This doesn't yet confirm "
                  "it's properly wired via SWD to the target chip - that's what "
                  "the next step, the actual erase, verifies.)")

    def full_chip_flash(self, bootloader_path, app_path, dry_run=False,
                         target=PYOCD_TARGET_NAME, probe_uid=None):
        self.check_connection(probe_uid, dry_run)
        probe_args = ["--probe", probe_uid] if probe_uid else []
        self.log(f"=== pyOCD full-chip program (target={target}) ===")
        self.log("Erasing entire chip...")
        self._run(["erase", "-t", target, "--chip"] + probe_args, dry_run)

        self.log("Programming bootloader region...")
        args = ["flash", "-t", target, "-e", "auto"] + probe_args
        args += _address_args_bin_needs_it(bootloader_path, BOOTLOADER_FLASH_ADDR, "--base-address")
        args += [bootloader_path]
        self._run(args, dry_run)

        self.log("Programming application region...")
        args = ["flash", "-t", target, "-e", "auto"] + probe_args
        args += _address_args_bin_needs_it(app_path, APP_FLASH_ADDR, "--base-address")
        args += [app_path]
        self._run(args, dry_run)

        # pyOCD's own flash command already does an internal CRC-based
        # check before writing (skips pages that already match, per its
        # own "Trust CRC" optimization), but that's not the same as an
        # explicit post-write pass/fail report the way CubeProgrammer's -v
        # flag gives. `commander -c compare` does a real byte-for-byte
        # read-back against the source file, closing that gap - but only
        # for a raw .bin: it compares flash content against the file's raw
        # bytes, which wouldn't correctly match a .hex/.elf file's own
        # text/structured encoding even after a genuinely successful
        # flash, so those formats skip this step and rely on the flash
        # command's own internal verification instead.
        if bootloader_path.lower().endswith(".bin"):
            self.log("Verifying bootloader region (read-back compare)...")
            self._run(["commander", "-t", target] + probe_args
                      + ["-c", f"compare 0x{BOOTLOADER_FLASH_ADDR:08X} {bootloader_path}"], dry_run)
        else:
            self.log("Bootloader is not a .bin - skipping read-back compare "
                      "(pyOCD's own flash-time verification still applies).")
        if app_path.lower().endswith(".bin"):
            self.log("Verifying application region (read-back compare)...")
            self._run(["commander", "-t", target] + probe_args
                      + ["-c", f"compare 0x{APP_FLASH_ADDR:08X} {app_path}"], dry_run)
        else:
            self.log("Application is not a .bin - skipping read-back compare "
                      "(pyOCD's own flash-time verification still applies).")

        self.log("Resetting target...")
        self._run(["reset", "-t", target] + probe_args, dry_run)
        self.log("=== pyOCD full-chip program complete ===")


class CubeProgrammerCLI(_SubprocessProgrammerBase):
    NAME = "STM32CubeProgrammer"

    # Fallback search locations if the CLI isn't on PATH - the installer
    # doesn't always add it. Adjust these if your install lives elsewhere;
    # this list is a starting point, not exhaustive across every OS/version.
    _FALLBACK_PATHS = [
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        "/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
        os.path.expanduser("~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"),
    ]

    def __init__(self, log=None):
        super().__init__(log)
        self.exe = _find_executable(["STM32_Programmer_CLI", "STM32_Programmer_CLI.exe"])
        if not self.exe:
            for path in self._FALLBACK_PATHS:
                if os.path.isfile(path):
                    self.exe = path
                    break

    @staticmethod
    def available():
        return CubeProgrammerCLI().exe is not None

    def list_probes(self):
        """Returns a list of serial numbers found via `-l` ("Connected
        ST-LINK Probes List", one "ST-LINK SN : <serial>" line per probe -
        confirmed against real ST community-forum output). Returns [] if
        none found, the tool isn't available, or the output didn't match
        the expected format (a different CubeProgrammer version phrasing
        this differently would show up as an empty list here rather than
        a wrong guess)."""
        if not self.exe:
            return []
        try:
            output = self._run(["-l"], dry_run=False, check_output=False)
        except SWDFlashError:
            return []
        # \S+ (any non-whitespace), not a hex-only character class - some
        # ST-Link/V2-1 and STLINK-V3 variants (Discovery/Nucleo boards
        # especially) report serials that aren't purely hex, and those
        # would otherwise just be silently excluded from the probe list.
        serials = re.findall(r"ST-LINK SN\s*:\s*(\S{6,})", output)
        return serials

    def check_connection(self, serial=None, dry_run=False):
        # A connect-only invocation (-c port=SWD with no -e/-w action flag)
        # should print target/device identification on success and exit.
        # This requires POSITIVE confirmation text, not just the absence of
        # a failure phrase - which is exactly the gap a real test exposed:
        # with no ST-Link connected at all, this tool still logged
        # "complete" for a full erase+program sequence, meaning its exit
        # code alone isn't trustworthy. Failing closed (no confirmation =
        # treated as failure) is the safer default for a destructive tool.
        self.log("Checking for a connected probe and target...")
        connect_args = ["-c", "port=SWD"] + ([f"sn={serial}"] if serial else [])
        if dry_run:
            self.log(f"$ {self.exe} " + " ".join(connect_args))
            self.log("  (dry run - not executed, skipping connection check)")
            return
        probes = self.list_probes()
        if len(probes) > 1 and not serial:
            raise SWDFlashError(
                f"{len(probes)} ST-Link probes are connected and none was "
                f"selected - refusing to guess which one to run a chip erase "
                f"against. Pick one from the probe list."
            )
        output = self._run(connect_args, dry_run=False, check_output=False)
        lower = output.lower()
        if self._looks_like_failure(output):
            raise SWDFlashError(
                "STM32CubeProgrammer's connection check reported a failure - "
                "see the log above. Check the ST-Link is connected via USB "
                "and wired to the chip's SWD pins."
            )
        if not any(hint in lower for hint in ("device id", "board", "chip id", "connected via", "st-link sn")):
            raise SWDFlashError(
                "Couldn't confirm STM32CubeProgrammer actually connected to a "
                "target - its output didn't contain any expected connection-"
                "confirmation text (see the log above for exactly what it did "
                "print). Not proceeding without that confirmation. Check the "
                "ST-Link is connected via USB and wired to the chip's SWD pins."
            )
        self.log("Connection confirmed - proceeding.")

    def full_chip_flash(self, bootloader_path, app_path, dry_run=False, serial=None):
        self.check_connection(serial, dry_run)
        connect = ["-c", "port=SWD"] + ([f"sn={serial}"] if serial else [])
        self.log("=== STM32CubeProgrammer full-chip program ===")
        self.log("Erasing entire chip...")
        self._run(connect + ["-e", "all"], dry_run)

        self.log("Programming bootloader region...")
        args = connect + ["-w", bootloader_path]
        args += _address_args_bin_needs_it(bootloader_path, BOOTLOADER_FLASH_ADDR)
        self._run(args, dry_run)

        self.log("Programming application region, verifying, and resetting...")
        args = connect + ["-w", app_path]
        args += _address_args_bin_needs_it(app_path, APP_FLASH_ADDR)
        args += ["-v", "-rst"]
        self._run(args, dry_run)
        self.log("=== STM32CubeProgrammer full-chip program complete ===")

    def read_option_bytes(self, serial=None, dry_run=False):
        """Read-only option byte dump ('-ob displ') - no erase, no write,
        just a connect-and-report. Primarily meant to flag RDP2 before it's
        too late to matter: RDP0/RDP1 are both reversible (RDP1 -> RDP0
        works via -rdu, which does a mass-erase but leaves the debug port
        usable again), but RDP2 is a permanent, intentionally irreversible
        lock-out by ST's own design - this project's SWD section has been
        careful throughout to distinguish "recoverable via SWD" from that
        one true exception, and this check exists to catch it BEFORE a
        full-chip operation, not after.

        Returns (rdp_level_str_or_None, raw_output) - rdp_level_str is
        "0"/"1"/"2" when confidently identified, None if the output didn't
        contain a recognizable RDP indicator (shown to the user rather than
        guessed at).
        """
        self.log("=== Reading option bytes (read-only, no erase/write) ===")
        connect = ["-c", "port=SWD"] + ([f"sn={serial}"] if serial else [])
        if dry_run:
            self.log(f"$ {self.exe} " + " ".join(connect) + " -ob displ")
            self.log("  (dry run - not executed)")
            return None, ""
        output = self._run(connect + ["-ob", "displ"], dry_run=False, check_output=False)
        # RDP is checked BEFORE the generic failure-text check: CubeProgrammer
        # genuinely phrases the RDP1 case as starting with "Error: RDP level
        # is set to 1..." (confirmed against real ST community-forum output)
        # - that's a successful, meaningful answer to "what's the RDP
        # level", not a connection failure, even though it contains the
        # word "Error". Finding a recognizable RDP signal at all is itself
        # evidence the read worked.
        lower = output.lower()
        rdp_level = None
        if "0xcc" in lower and "rdp" in lower:
            rdp_level = "2"
        elif "0xaa" in lower and "rdp" in lower:
            rdp_level = "0"
        elif "0xbb" in lower and "rdp" in lower:
            rdp_level = "1"
        else:
            m = re.search(r"rdp\D{0,15}level\D{0,10}(\d)", lower)
            if m:
                rdp_level = m.group(1)

        if rdp_level is None and self._looks_like_failure(output):
            raise SWDFlashError(
                "Couldn't read option bytes - see the log above. Check the "
                "ST-Link is connected via USB and wired to the chip's SWD pins."
            )
        self.log(f"RDP level detected: {rdp_level if rdp_level else 'could not confidently parse - see raw output above'}")
        return rdp_level, output


# =============================================================================
# Bootloader protocol - implements the exact CAN sequence BOOTLOADER.C
# expects, matching CANBUS.TXT's documented 0x7F0-0x7F7 protocol.
# =============================================================================
# =============================================================================
# Firmware file validation - a lightweight, local sanity check on a .bin
# before it's ever offered to the user as something to flash. This mirrors
# the same plausibility check the bootloader itself applies to a fresh
# image: a real vector table's first word is the initial stack pointer,
# which has to land inside this chip's actual main SRAM (0x20000000-
# 0x2000A000, 40KB) - the STM32F303CC's other 8KB, its CCM RAM, is mapped
# at a completely different base address (0x10000000) on this chip
# family, not contiguous with main SRAM, so 0x2000A000-0x2000C000 was
# never real, usable RAM at all despite being inside the range this used
# to accept. A random/wrong/truncated file essentially never happens to
# satisfy this by chance.
#
# This is NOT a substitute for the bootloader's own CRC32/HMAC check during
# the real transfer - it can't detect a corrupted-but-plausible-looking
# file, or one signed with the wrong key. It exists to catch the obvious,
# common mistakes (wrong file, empty file, a truncated download) locally
# and instantly, before spending time sending anything over CAN.
# =============================================================================
def validate_firmware_file(path):
    """Returns (is_valid, reason_string, size_bytes)."""
    try:
        size = os.path.getsize(path)
    except OSError as e:
        return False, f"can't read file: {e}", 0

    if size == 0:
        return False, "empty file", 0
    if size > APP_MAX_SIZE:
        return False, f"too large ({size} bytes > {APP_MAX_SIZE}-byte main slot)", size
    if size < 8:
        return False, "too small to contain a vector table", size

    try:
        with open(path, "rb") as f:
            header = f.read(8)
    except OSError as e:
        return False, f"can't read file: {e}", size

    initial_sp, reset_handler = struct.unpack("<II", header[0:8])
    if not (0x20000000 <= initial_sp <= 0x2000A000):
        return False, (
            f"first word (0x{initial_sp:08X}) doesn't look like a valid "
            f"initial stack pointer for this chip's RAM - probably not a "
            f"real URTC image, or a corrupted one"
        ), size

    # The stack pointer alone can't tell a bootloader image from an
    # application image - both slots share the same RAM, so both pass the
    # check above equally. This was a real gap: this exact cross-check
    # already existed for the SWD/JTAG path's own file pickers (see
    # validate_swd_image_file), but was never applied here - so
    # URTC_BOOTLOADER.bin could sit in firmware/ and get listed as "looks
    # valid" for a CAN-OTA update, which only ever writes to the
    # application slot. The reset handler is a real, absolute code address
    # baked in at link time, and always points inside the image's OWN
    # slot - verified against this project's actual compiled
    # BOOTLOADER.bin/APP.bin, whose reset handlers land at 0x080030F1 and
    # 0x0800C725 respectively, each correctly inside its own range and
    # outside the other's.
    if not (APP_FLASH_ADDR <= reset_handler < APP_FLASH_ADDR + APP_MAX_SIZE):
        return False, (
            f"reset handler (0x{reset_handler:08X}) doesn't point inside "
            f"the application slot (0x{APP_FLASH_ADDR:08X}-"
            f"0x{APP_FLASH_ADDR + APP_MAX_SIZE:08X}) - this looks like a "
            f"bootloader image, not an application image. A CAN-OTA update "
            f"only ever writes to the application slot, so this file can't "
            f"go through this path regardless of size - use the SWD/JTAG "
            f"section instead if you actually need to update the bootloader."
        ), size

    return True, "looks valid", size


def _validate_intel_hex(path, size, max_size, label, expected_base_addr=None):
    """Lightweight Intel HEX validation for the SWD image pickers below -
    checks basic record-format sanity, and extracts the initial stack
    pointer for the same plausibility check the .bin path uses. Checks at
    expected_base_addr first when given (the deterministically-known
    address this slot is supposed to load at - BOOTLOADER_FLASH_ADDR or
    APP_FLASH_ADDR), since blindly trusting min(data_bytes) as "the vector
    table" breaks if the file has any record at a lower address than the
    real image start (a prepended metadata/config block, for instance).
    Falls back to min(data_bytes) only if the expected address isn't
    present at all. Doesn't handle every possible Intel HEX quirk (records
    out of order, multiple extended-address segments) - if it can't
    confidently find a vector table, it says so rather than guessing."""
    try:
        with open(path, "r", errors="strict") as f:
            lines = [ln.strip() for ln in f if ln.strip()]
    except OSError as e:
        return False, f"can't read file: {e}", size
    except (UnicodeDecodeError, ValueError):
        return False, "doesn't look like a text-format .hex file (failed to decode as text)", size

    if not lines or not lines[0].startswith(":"):
        return False, "doesn't start with ':' - not a valid Intel HEX file", size

    data_bytes = {}
    saw_eof = False
    base_addr = 0
    for ln in lines:
        if not ln.startswith(":") or len(ln) < 11:
            return False, f"malformed Intel HEX line: {ln[:20]}", size
        try:
            byte_count = int(ln[1:3], 16)
            address = int(ln[3:7], 16)
            record_type = int(ln[7:9], 16)
            data = bytes.fromhex(ln[9:9 + byte_count * 2])
        except ValueError:
            return False, f"malformed Intel HEX line (bad hex digits): {ln[:20]}", size

        if record_type == 0x01:
            saw_eof = True
            break
        elif record_type == 0x04 and len(data) == 2:
            base_addr = (data[0] << 8 | data[1]) << 16
        elif record_type == 0x00:
            full_addr = base_addr + address
            for i, b in enumerate(data):
                data_bytes[full_addr + i] = b

    if not saw_eof:
        return False, "no EOF record found - file may be truncated", size
    if not data_bytes:
        return False, "no data records found", size
    # Actual occupied bytes, not the address span from lowest to highest
    # record - a legitimate, small image can still have a second block far
    # away (option bytes, calibration data, EEPROM emulation - STM32
    # toolchains do sometimes bundle these into a single .hex export), and
    # the span between two such widely-separated addresses can look like
    # hundreds of megabytes even though only a few real bytes are present.
    occupied = len(data_bytes)
    if occupied > max_size:
        return False, f"{occupied} bytes of actual data, larger than the {max_size}-byte {label} slot", size

    # Prefer the deterministically-known expected address over guessing
    # from min(data_bytes) - the latter is only a fallback now.
    candidates = []
    if expected_base_addr is not None and all(
        (expected_base_addr + i) in data_bytes for i in range(4)
    ):
        candidates.append(expected_base_addr)
    min_addr = min(data_bytes)
    if all((min_addr + i) in data_bytes for i in range(4)):
        candidates.append(min_addr)

    for vector_addr in candidates:
        sp_bytes = bytes(data_bytes[vector_addr + i] for i in range(4))
        initial_sp = struct.unpack("<I", sp_bytes)[0]
        if 0x20000000 <= initial_sp <= 0x2000A000:
            if expected_base_addr is not None and vector_addr != expected_base_addr:
                # Found A valid-looking vector table, just not where this
                # slot expects one - almost always means the wrong file for
                # this slot (bootloader/application swapped), not a
                # corrupted file, so this is a hard failure rather than a
                # passing note.
                return False, (
                    f"vector table found at 0x{vector_addr:08X}, not the "
                    f"expected 0x{expected_base_addr:08X} for the {label} slot "
                    f"- this looks like the wrong image (bootloader/"
                    f"application swapped?)"
                ), size
            # Stack pointer alone can't catch every case: the vector table
            # can genuinely be at the right address while the reset
            # handler it points to - a real, absolute code address chosen
            # by the linker, the same kind of check the .bin path already
            # does - was linked somewhere else entirely (a misconfigured
            # custom linker script, for instance). Only checked when those
            # 4 bytes are actually present in the file; a sparse file that
            # happens to include the stack pointer but not the immediately
            # following word isn't itself a sign of anything wrong.
            if all((vector_addr + 4 + i) in data_bytes for i in range(4)):
                rh_bytes = bytes(data_bytes[vector_addr + 4 + i] for i in range(4))
                reset_handler = struct.unpack("<I", rh_bytes)[0]
                if expected_base_addr is not None and not (
                    expected_base_addr <= reset_handler < expected_base_addr + max_size
                ):
                    return False, (
                        f"reset handler (0x{reset_handler:08X}) doesn't point inside "
                        f"the {label} slot (0x{expected_base_addr:08X}-"
                        f"0x{expected_base_addr + max_size:08X}) - this looks like "
                        f"the wrong image for this slot"
                    ), size
            return True, f"looks valid (load address 0x{vector_addr:08X})", size
    if candidates:
        # Something was at a checkable address, but its value didn't look
        # like a valid stack pointer - a real problem, not just "couldn't check".
        vector_addr = candidates[0]
        sp_bytes = bytes(data_bytes[vector_addr + i] for i in range(4))
        initial_sp = struct.unpack("<I", sp_bytes)[0]
        return False, (
            f"first word at 0x{vector_addr:08X} (0x{initial_sp:08X}) doesn't "
            f"look like a valid initial stack pointer for this chip's RAM"
        ), size

    return True, (
        "Intel HEX format looks valid, but couldn't confidently locate the "
        "vector table to check the stack pointer - double-check this is the "
        "right file"
    ), size


def _validate_elf(path, size, max_size, label, expected_base_addr=None):
    """Lightweight, dependency-free ELF32 validation for the SWD image
    pickers - parses just the ELF header and program headers (no section
    headers, symbols, or anything else) to find PT_LOAD segments and check
    the same initial-stack-pointer plausibility the .bin/.hex paths use.
    Deliberately not using pyelftools here: this project has stayed at
    zero non-stdlib dependencies throughout (chose subprocess over pyOCD's
    own Python API for the same reason - preferring a smaller, more
    stable surface over a richer one), and full ELF parsing is more than
    this specific check needs. ARM Cortex-M ELFs are always 32-bit
    little-endian, so this doesn't handle 64-bit or big-endian ELF at all."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        return False, f"can't read file: {e}", size

    if len(data) < 52 or data[0:4] != b"\x7fELF":
        return False, "doesn't start with the ELF magic number - not a valid ELF file", size
    if data[4] != 1:
        return False, "not a 32-bit ELF (EI_CLASS != 1) - unexpected for a Cortex-M image", size
    if data[5] != 1:
        return False, "not little-endian (EI_DATA != 1) - unexpected for a Cortex-M image", size

    try:
        e_machine, = struct.unpack_from("<H", data, 18)
        e_phoff, = struct.unpack_from("<I", data, 28)
        e_phentsize, = struct.unpack_from("<H", data, 42)
        e_phnum, = struct.unpack_from("<H", data, 44)
    except struct.error:
        return False, "truncated ELF header", size
    if e_machine != 0x28:  # EM_ARM
        return False, f"e_machine=0x{e_machine:X}, not ARM (0x28) - wrong architecture for this chip", size
    if e_phnum == 0 or e_phoff == 0:
        return False, "no program headers found - nothing to load", size

    segments = []  # (p_paddr, p_offset, p_filesz)
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if off + 32 > len(data):
            break
        try:
            p_type, p_offset, p_vaddr, p_paddr, p_filesz = struct.unpack_from("<IIIII", data, off)
        except struct.error:
            break
        if p_type == 1 and p_filesz > 0:  # PT_LOAD
            if p_offset + p_filesz > len(data):
                return False, (
                    f"truncated ELF - a loadable segment at 0x{p_paddr:08X} claims "
                    f"{p_filesz} bytes, but the file itself isn't big enough to "
                    f"actually contain them (looks cut short, e.g. from an "
                    f"interrupted download)"
                ), size
            segments.append((p_paddr, p_offset, p_filesz))

    if not segments:
        return False, "no loadable (PT_LOAD) segments found", size

    # Actual occupied bytes across all PT_LOAD segments, not the address
    # span from the lowest to the highest - same reasoning as the .hex
    # path: a distant segment (option bytes, calibration data) can make
    # the span look enormous even when barely anything is actually there.
    occupied = sum(p_filesz for _, _, p_filesz in segments)
    if occupied > max_size:
        return False, f"{occupied} bytes of actual data, larger than the {max_size}-byte {label} slot", size

    candidates = []
    if expected_base_addr is not None:
        for p_paddr, p_offset, p_filesz in segments:
            if p_paddr == expected_base_addr and p_filesz >= 4:
                candidates.append((p_paddr, p_offset))
    min_paddr, min_offset, min_filesz = min(segments, key=lambda s: s[0])
    if min_filesz >= 4:
        candidates.append((min_paddr, min_offset))

    for paddr, offset in candidates:
        if offset + 4 > len(data):
            continue
        initial_sp, = struct.unpack_from("<I", data, offset)
        if 0x20000000 <= initial_sp <= 0x2000A000:
            if expected_base_addr is not None and paddr != expected_base_addr:
                return False, (
                    f"loadable segment found at 0x{paddr:08X}, not the expected "
                    f"0x{expected_base_addr:08X} for the {label} slot - this looks "
                    f"like the wrong image (bootloader/application swapped?)"
                ), size
            # Same reasoning as the .hex path: the reset handler is a real,
            # absolute code address the linker chose, checked separately
            # from the stack pointer since the vector table being at the
            # right spot doesn't guarantee the code it points to was
            # linked into the same region.
            if offset + 8 <= len(data):
                reset_handler, = struct.unpack_from("<I", data, offset + 4)
                if expected_base_addr is not None and not (
                    expected_base_addr <= reset_handler < expected_base_addr + max_size
                ):
                    return False, (
                        f"reset handler (0x{reset_handler:08X}) doesn't point inside "
                        f"the {label} slot (0x{expected_base_addr:08X}-"
                        f"0x{expected_base_addr + max_size:08X}) - this looks like "
                        f"the wrong image for this slot"
                    ), size
            return True, f"looks valid (load address 0x{paddr:08X})", size
    if candidates:
        paddr, offset = candidates[0]
        initial_sp, = struct.unpack_from("<I", data, offset)
        return False, (
            f"first word at 0x{paddr:08X} (0x{initial_sp:08X}) doesn't look "
            f"like a valid initial stack pointer for this chip's RAM"
        ), size

    return True, (
        "ELF format looks valid, but couldn't confidently locate the vector "
        "table to check the stack pointer - double-check this is the right file"
    ), size


def validate_swd_image_file(path, max_size, label, expected_base_addr=None):
    """Validates a file selected for the SWD/JTAG section (bootloader or
    application image), which - unlike the CAN path's fixed APP_MAX_SIZE
    .bin-only assumption - needs a caller-specified slot size (32KB
    bootloader vs 112KB app) and has to handle .hex/.elf as well as .bin.
    This plugs the gap where those file pickers previously accepted
    anything with no check at all."""
    try:
        size = os.path.getsize(path)
    except OSError as e:
        return False, f"can't read file: {e}", 0
    if size == 0:
        return False, "empty file", 0

    if path.lower().endswith(".hex"):
        return _validate_intel_hex(path, size, max_size, label, expected_base_addr)
    if path.lower().endswith((".elf", ".axf")):
        return _validate_elf(path, size, max_size, label, expected_base_addr)

    if size > max_size:
        return False, f"too large ({size} bytes > {max_size}-byte {label} slot)", size
    if size < 8:
        return False, "too small to contain a vector table", size
    try:
        with open(path, "rb") as f:
            header = f.read(8)
    except OSError as e:
        return False, f"can't read file: {e}", size
    initial_sp, reset_handler = struct.unpack("<II", header[0:8])
    if not (0x20000000 <= initial_sp <= 0x2000A000):
        return False, (
            f"first word (0x{initial_sp:08X}) doesn't look like a valid "
            f"initial stack pointer for this chip's RAM - probably not a "
            f"real {label} image, or a corrupted one"
        ), size
    # The stack pointer alone can't tell a bootloader image from an
    # application image - both slots share the same RAM, so both pass
    # that check equally. The reset handler address can: it's a real,
    # absolute code address baked in at link time, so it always points
    # somewhere inside the image's OWN slot - verified against this
    # project's actual compiled BOOTLOADER.bin/APP.bin, whose reset
    # handlers land at 0x080030F1 and 0x0800C725 respectively, each
    # correctly inside its own range and outside the other's. A bootloader
    # image selected for the application slot (or vice versa) still has a
    # plausible stack pointer - RAM is RAM either way - but its reset
    # handler won't fall inside the slot being flashed into.
    if expected_base_addr is not None:
        if not (expected_base_addr <= reset_handler < expected_base_addr + max_size):
            return False, (
                f"reset handler (0x{reset_handler:08X}) doesn't point inside "
                f"the {label} slot (0x{expected_base_addr:08X}-"
                f"0x{expected_base_addr + max_size:08X}) - this looks like the "
                f"wrong image for this slot (bootloader/application swapped?)"
            ), size
    return True, "looks valid", size


class FlashError(Exception):
    pass


class URTCFlasher:
    def __init__(self, slcan, log, progress_cb=None, stop_flag=None):
        self.can = slcan
        self.log = log
        self.progress_cb = progress_cb or (lambda pct: None)
        self.stop_flag = stop_flag or (lambda: False)
        self._last_heartbeat_pct = None  # tracked across _wait_for calls for the page-ACK retry's active confirmation check

    def query_version(self, timeout=1.5):
        """Asks whatever's currently running (application or bootloader)
        to identify itself. Returns a dict with keys: responder ('application'
        or 'bootloader'), hardware_id, version_major, version_minor, and
        bootloader_version (a (major, minor, patch) tuple, or None) - or
        None if nothing answered within the timeout (board unresponsive,
        wrong bitrate, not connected, etc.).

        bootloader_version only ever gets filled in when the BOOTLOADER is
        the one answering (responder == 'bootloader') - it comes from a
        separate 0x7FA frame the bootloader sends right alongside 0x7F9,
        which the running application never sends (it has no way to know
        a currently-flashed bootloader's version other than this). A short
        grace window after 0x7F9 arrives catches 0x7FA even though the two
        aren't guaranteed to land within the same read_frame() call.
        """
        self.can.send_frame(CAN_ID_QUERY_VERSION, b"\x00")
        deadline = time.time() + timeout
        result = None
        grace_deadline = None
        while time.time() < deadline:
            frame = self.can.read_frame(timeout=0.1)
            if frame is None:
                if result is not None and grace_deadline is not None and time.time() >= grace_deadline:
                    return result  # got 0x7F9, grace window for 0x7FA expired with nothing more
                continue
            can_id, data = frame
            if can_id == CAN_ID_VERSION_RESPONSE and len(data) == 8 and result is None:
                responder = "application" if data[0] == 0x00 else "bootloader"
                hw_id = struct.unpack(">I", data[1:5])[0]
                ver_major = struct.unpack(">H", data[5:7])[0]
                ver_minor = data[7]
                result = {
                    "responder": responder,
                    "hardware_id": hw_id,
                    "version_major": ver_major,
                    "version_minor": ver_minor,
                    "bootloader_version": None,
                }
                if responder != "bootloader":
                    return result  # application never sends 0x7FA - no point waiting for it
                grace_deadline = time.time() + 0.3
            elif can_id == CAN_ID_BOOTLOADER_VERSION_RESPONSE and len(data) == 3 and result is not None:
                result["bootloader_version"] = (data[0], data[1], data[2])
                return result
        return result

    def _wait_for(self, expected_id, timeout=2.0):
        """Wait for a specific CAN ID, logging heartbeats/status seen along
        the way but not treating them as the answer unless they match."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.stop_flag():
                raise FlashError("Cancelled by user")
            frame = self.can.read_frame(timeout=0.1)
            if frame is None:
                continue
            can_id, data = frame
            if can_id == CAN_ID_HEARTBEAT and len(data) == 2:
                status, pct = data[0], data[1]
                name = STATUS_NAMES.get(status, f"0x{status:02X}")
                pct_str = f"{pct}%" if pct != 0xFF else "--"
                self.log(f"  heartbeat: {name} ({pct_str})")
                if pct != 0xFF:
                    self._last_heartbeat_pct = pct
            elif can_id == CAN_ID_STATUS and len(data) in (1, 2):
                name = STATUS_NAMES.get(data[0], f"0x{data[0]:02X}")
                if len(data) == 2 and data[0] == 0x05:  # STATUS_VERIFY_FAIL + reason byte
                    reason = VERIFY_FAIL_REASONS.get(data[1], f"unknown reason 0x{data[1]:02X}")
                    self.log(f"  status: {name} - {reason}")
                else:
                    self.log(f"  status: {name}")
                if can_id == expected_id:
                    return data
            if can_id == expected_id:
                return data
        raise FlashError(f"Timed out waiting for CAN ID 0x{expected_id:03X}")

    def erase_eeprom(self):
        """Sends 0x192 (magic-payload erase) to a currently-running
        application, wiping the persistence EEPROM's saved state. Only
        the application handles this - the bootloader doesn't - so this
        has to run before trigger_bootloader_entry(), not after. A missing
        confirmation is logged, not raised - this is a secondary, optional
        step alongside the actual firmware update, and losing just the
        confirmation frame shouldn't abort the whole flash the way a
        genuine protocol failure in flash() itself should."""
        self.log("Erasing persistence EEPROM (0x192)...")
        self.can.send_frame(CAN_ID_ERASE_EEPROM, ERASE_EEPROM_MAGIC)
        try:
            self._wait_for(CAN_ID_EEPROM_STATE_RESP, timeout=2.0)
            self.log("EEPROM erase confirmed.")
        except FlashError:
            self.log("EEPROM erase sent, but got no confirmation within 2s - "
                      "continuing with the flash anyway. Check the EEPROM state "
                      "separately (e.g. via URTC Tester) if this matters.")

    def trigger_bootloader_entry(self):
        """Sends 0x7F0 to a currently-running application to make it reset
        into the bootloader. Skip this if the board is already sitting in
        the bootloader (fresh JTAG flash, or no valid application present)."""
        self.log("Sending bootloader-entry trigger (0x7F0)...")
        self.can.send_frame(CAN_ID_ENTER_BOOTLOADER, bytes([0xB0, 0x07, 0x1D, 0x5A]))
        time.sleep(0.8)  # give the app time to shut down actuators and reset

    def flash(self, firmware_path):
        with open(firmware_path, "rb") as f:
            firmware = f.read()

        size = len(firmware)
        if size == 0:
            raise FlashError("Firmware file is empty")
        if size > APP_MAX_SIZE:
            raise FlashError(
                f"Firmware is {size} bytes, exceeds the {APP_MAX_SIZE}-byte "
                f"main slot - refusing to send an update the bootloader "
                f"would reject anyway"
            )

        crc32 = zlib.crc32(firmware) & 0xFFFFFFFF
        signature = hmac.new(HMAC_KEY, firmware, hashlib.sha256).digest()

        self.log(f"Firmware: {firmware_path}")
        self.log(f"  size: {size} bytes ({size/1024:.1f} KB)")
        self.log(f"  CRC32: 0x{crc32:08X}")
        self.log(f"  HMAC-SHA256: {signature.hex()}")
        self.log(f"  HardwareID: 0x{THIS_HARDWARE_ID:08X}")

        # --- 0x7F1: start update (size + HardwareID) ---
        self.log("Sending start-update (0x7F1)...")
        payload = struct.pack(">II", size, THIS_HARDWARE_ID)
        self.can.send_frame(CAN_ID_START_UPDATE, payload)
        self._wait_for(CAN_ID_STATUS, timeout=3.0)  # expect STATUS_ERASING then STATUS_RECEIVING

        # --- 0x7F7 x4: HMAC signature chunks ---
        self.log("Sending HMAC signature (4x 0x7F7)...")
        for i in range(4):
            chunk = signature[i*8:(i+1)*8]
            self.can.send_frame(CAN_ID_HMAC_CHUNK, chunk)
            time.sleep(0.01)

        # --- 0x7F2: firmware data, page by page, waiting for each page ACK ---
        self.log("Sending firmware data...")
        total_pages = (size + FLASH_PAGE_SIZE - 1) // FLASH_PAGE_SIZE
        offset = 0
        page_index = 0
        transfer_start = time.time()
        total_retries = 0
        while offset < size:
            if self.stop_flag():
                raise FlashError("Cancelled by user")
            page_start = time.time()
            page_end = min(offset + FLASH_PAGE_SIZE, size)
            page_data = firmware[offset:page_end]
            # send this page 8 bytes at a time
            for i in range(0, len(page_data), 8):
                chunk = page_data[i:i+8]
                self.can.send_frame(CAN_ID_DATA, chunk)
                time.sleep(0.001)  # small pacing gap - avoids overrunning the bootloader's own receive/flash-write pace
            # wait for this page's ACK before sending the next one - retries
            # the WAIT itself (not a resend of the page data) up to 2 extra
            # times with a short backoff, recovering from an ACK that was
            # delayed or lost on a noisy bus without the data itself being
            # lost. Deliberately doesn't resend the page's data on a
            # timeout: if the original data actually arrived fine and only
            # the ACK got lost, resending would make the bootloader read
            # those bytes as the start of the NEXT page, desyncing the
            # transfer - safely retrying the data itself would need the
            # bootloader to tolerate a duplicate page, which isn't
            # something this flasher-only change can verify or rely on.
            #
            # Each retry is more than a passive wait: the bootloader's own
            # heartbeat (already sent roughly once a second regardless)
            # reports its overall bytes-received as a percentage, which
            # this checks against what receiving this page in full would
            # imply - if they're consistent, that's real evidence the data
            # itself got through and only the ACK was lost on the way
            # back, rather than just waiting blind and hoping.
            expected_pct_after_this_page = int((page_end / size) * 100)
            ack = None
            last_err = None
            for attempt in range(3):
                if attempt > 0:
                    total_retries += 1
                    backoff = 0.3 * (2 ** (attempt - 1))  # 0.3s, then 0.6s
                    self.log(f"  no ACK for page {page_index} yet, waiting a bit longer (attempt {attempt+1}/3)...")
                    time.sleep(backoff)
                try:
                    ack = self._wait_for(CAN_ID_PAGE_ACK, timeout=3.0)
                    last_err = None
                    break
                except FlashError as e:
                    last_err = e
                    if self._last_heartbeat_pct is not None and self._last_heartbeat_pct >= expected_pct_after_this_page:
                        self.log(f"  (the bootloader's own heartbeat already reports "
                                  f"{self._last_heartbeat_pct}% received, consistent with this page "
                                  f"having arrived - likely just the ACK that was lost)")
            if last_err is not None:
                raise last_err
            if len(ack) < 4:
                raise FlashError(
                    f"Page ACK for page {page_index} was only {len(ack)} bytes "
                    f"(expected 4) - likely bus noise corrupting the frame"
                )
            acked_page = struct.unpack(">I", ack)[0]
            if acked_page != page_index:
                raise FlashError(
                    f"Page ACK mismatch: expected page {page_index}, "
                    f"bootloader acked page {acked_page}"
                )
            page_elapsed = time.time() - page_start
            page_kbps = (len(page_data) / 1024) / page_elapsed if page_elapsed > 0 else 0.0
            offset = page_end
            page_index += 1
            # Scaled to 0-70%, not 0-100% - the bootloader's own verify/copy
            # phase after this (see below) reports its own 0-100% via
            # heartbeat, which would otherwise make the bar visibly drop
            # back down right after reaching 100% here. Reserving the last
            # 30% for that phase keeps it monotonically increasing instead.
            pct = int((page_index / total_pages) * 70)
            self.progress_cb(pct)
            self.log(f"  page {page_index}/{total_pages} written and acked "
                      f"({page_elapsed:.2f}s, {page_kbps:.1f} KB/s)")

        transfer_elapsed = time.time() - transfer_start
        overall_kbps = (size / 1024) / transfer_elapsed if transfer_elapsed > 0 else 0.0
        self.log(f"Transfer complete: {size} bytes in {transfer_elapsed:.1f}s "
                  f"({overall_kbps:.1f} KB/s average, {total_retries} page-ACK "
                  f"{'retry' if total_retries == 1 else 'retries'})")

        # --- 0x7F4: end update (CRC32 + version) ---
        self.log("Sending end-update / verify (0x7F4)...")
        payload = struct.pack(">IHH", crc32, FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR)
        self.can.send_frame(CAN_ID_END_UPDATE, payload)

        # The bootloader now verifies, then copies backup->main - this can
        # take several seconds for a large image (erase+write+read-back
        # verify per page). Watch for the final status.
        self.log("Verifying and copying to main slot (this can take a while)...")
        deadline = time.time() + 60.0
        while time.time() < deadline:
            if self.stop_flag():
                raise FlashError("Cancelled by user")
            frame = self.can.read_frame(timeout=0.2)
            if frame is None:
                continue
            can_id, data = frame
            if can_id == CAN_ID_HEARTBEAT and len(data) == 2:
                status, pct = data[0], data[1]
                name = STATUS_NAMES.get(status, f"0x{status:02X}")
                pct_str = f"{pct}%" if pct != 0xFF else "--"
                self.log(f"  heartbeat: {name} ({pct_str})")
                if pct != 0xFF:
                    self.progress_cb(70 + int(pct * 0.3))  # this phase owns the reserved 70-100% of the bar - see the comment on the page-transfer loop above
            elif can_id == CAN_ID_STATUS and len(data) in (1, 2):
                status = data[0]
                name = STATUS_NAMES.get(status, f"0x{status:02X}")
                if len(data) == 2 and status == 0x05:  # STATUS_VERIFY_FAIL + reason byte
                    reason = VERIFY_FAIL_REASONS.get(data[1], f"unknown reason 0x{data[1]:02X}")
                    self.log(f"  status: {name} - {reason}")
                else:
                    self.log(f"  status: {name}")
                if status == 0x04:
                    self.progress_cb(100)
                    self.log("Update verified OK - board is resetting into new firmware.")
                    return True
                elif status == 0x05 and len(data) == 2:
                    reason = VERIFY_FAIL_REASONS.get(data[1], f"unknown reason 0x{data[1]:02X}")
                    raise FlashError(f"Bootloader reported failure: {name} - {reason}")
                elif status in (0x05, 0xFF):
                    raise FlashError(f"Bootloader reported failure: {name}")
        raise FlashError("Timed out waiting for final verification result")


# =============================================================================
# GUI
# =============================================================================
class FlasherGUI:
    def __init__(self, root):
        self.root = root
        root.title(f"URTC Flasher v{FLASHER_VERSION}")
        # Geometry (size + centered position) is set once in main(), before
        # this class is constructed - not here, so it isn't overwritten by
        # a second, uncentered geometry() call.
        root.resizable(True, True)

        # Persistent per-session log file (logs/urtc_flasher_YYYYMMDD_HHMMSS.log)
        # - purely additive to the on-screen log, so a technician can share
        # a session's full trace after the fact without having to copy text
        # out of the scrolling widget by hand. Failure to set this up
        # (read-only filesystem, etc.) is non-fatal - the on-screen log
        # still works exactly as before either way.
        self._file_logger = None
        try:
            os.makedirs(LOGS_FOLDER, exist_ok=True)
            log_path = os.path.join(
                LOGS_FOLDER, f"urtc_flasher_{time.strftime('%Y%m%d_%H%M%S')}.log"
            )
            self._file_logger = logging.getLogger(f"urtc_flasher_{id(self)}")
            self._file_logger.setLevel(logging.INFO)
            handler = logging.FileHandler(log_path, encoding="utf-8")
            handler.setFormatter(logging.Formatter("%(asctime)s %(message)s"))
            self._file_logger.addHandler(handler)
            self._file_logger.info(f"=== URTC Flasher v{FLASHER_VERSION} session started ===")
        except OSError:
            self._file_logger = None

        # App icon - a small, bold design (a bright ring + a single accent
        # dot) deliberately simpler than the full banner artwork, which
        # doesn't hold up at 16-32px the way it does at banner size.
        # iconphoto (not iconbitmap) since it works identically on both
        # Windows and Linux from a plain PNG - iconbitmap's own .ico
        # support is Windows-only. Silently skipped if missing, same
        # reasoning as the banner used to have: cosmetic, never worth a crash.
        try:
            self._icon_img = tk.PhotoImage(file=ICON_IMAGE_PATH)
            root.iconphoto(True, self._icon_img)
        except (tk.TclError, OSError):
            pass

        self.transport = None
        self.firmware_path = None
        self._detected_paths = {}
        self._stop_requested = False
        self._flash_thread = None
        self._swd_flash_thread = None

        pad = {"padx": 8, "pady": 4}

        # Two-column layout: everything CAN-OTA related (connect, select
        # firmware, flash) grouped under one named frame on the left,
        # everything SWD/JTAG related under its own named frame on the
        # right - these are two genuinely different operating modes (an
        # auto-recovering OTA update vs. a destructive full-chip
        # programming session), and grouping them this way makes that
        # split visually obvious rather than just a run of same-looking
        # numbered sections. Side by side rather than stacked also means
        # the row height is whichever column is taller, not the sum of
        # everything - stacked left almost no room for the log below, even
        # maximized.
        root.grid_columnconfigure(0, weight=1)
        root.grid_columnconfigure(1, weight=1)
        root.grid_rowconfigure(2, weight=1)  # log row - the one that should actually grow
        left_col = ttk.LabelFrame(root, text="CAN OTA Programming")
        right_col = ttk.LabelFrame(root, text="SWD/JTAG Programming")
        left_col.grid(row=0, column=0, sticky="nsew", padx=4, pady=4)
        right_col.grid(row=0, column=1, sticky="nsew", padx=4, pady=4)

        # --- Connection frame ---
        conn_frame = ttk.LabelFrame(left_col, text="1. Connect to USB-CAN Adapter")
        conn_frame.pack(fill="x", **pad)

        row = 0
        # Transport choice only appears at all when SocketCAN is actually
        # available (Linux, socket.AF_CAN present) - on Windows/macOS this
        # whole row is skipped and the UI looks exactly like the
        # Serial/SLCAN-only original, unchanged.
        self.transport_var = tk.StringVar(value="serial" if HAVE_SERIAL else "socketcan")
        if HAVE_SOCKETCAN:
            ttk.Label(conn_frame, text="Transport:").grid(row=row, column=0, sticky="w", **pad)
            transport_sub = ttk.Frame(conn_frame)
            transport_sub.grid(row=row, column=1, columnspan=3, sticky="w")
            self.transport_radio_serial = ttk.Radiobutton(
                transport_sub, text="Serial / SLCAN", value="serial",
                variable=self.transport_var, command=self.on_transport_change,
                state="normal" if HAVE_SERIAL else "disabled",
            )
            self.transport_radio_serial.pack(side="left", padx=(4, 12))
            self.transport_radio_socketcan = ttk.Radiobutton(
                transport_sub, text="SocketCAN (native)", value="socketcan",
                variable=self.transport_var, command=self.on_transport_change,
            )
            self.transport_radio_socketcan.pack(side="left")
            row += 1

        self.port_label = ttk.Label(conn_frame, text="COM port:" if self.transport_var.get() == "serial" else "CAN interface:")
        self.port_label.grid(row=row, column=0, sticky="w", **pad)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(conn_frame, textvariable=self.port_var, width=20, state="readonly")
        self.port_combo.grid(row=row, column=1, **pad)
        ttk.Button(conn_frame, text="Refresh", command=self.refresh_ports).grid(row=row, column=2, **pad)
        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.toggle_connect)
        self.connect_btn.grid(row=row, column=3, **pad)
        self.conn_status = ttk.Label(conn_frame, text="Not connected", foreground="red")
        self.conn_status.grid(row=row, column=4, **pad)
        row += 1

        # Bitrate selector (Serial/SLCAN only - SocketCAN's bitrate is set
        # at the OS/interface level, not by this application, so there's
        # nothing for auto-detect to try there). URTC's own bus is fixed at
        # 500k, so that stays the default - this is for a misconfigured
        # adapter, or a non-standard board, not something normal use needs
        # to touch.
        self.bitrate_label = ttk.Label(conn_frame, text="Bitrate:")
        self.bitrate_label.grid(row=row, column=0, sticky="w", **pad)
        self.bitrate_var = tk.StringVar(value="500 kbit/s")
        self.bitrate_combo = ttk.Combobox(
            conn_frame, textvariable=self.bitrate_var, width=20, state="readonly",
            values=[label for label, _ in SLCAN_BITRATES],
        )
        self.bitrate_combo.grid(row=row, column=1, **pad)
        self.autobaud_btn = ttk.Button(conn_frame, text="Auto-detect", command=self.auto_detect_bitrate)
        self.autobaud_btn.grid(row=row, column=2, **pad)
        self.bitrate_status = ttk.Label(conn_frame, text="", foreground="gray")
        self.bitrate_status.grid(row=row, column=3, columnspan=2, sticky="w", **pad)
        if self.transport_var.get() != "serial":
            self.bitrate_combo.config(state="disabled")
            self.autobaud_btn.config(state="disabled")
        row += 1

        ttk.Label(conn_frame, text="Currently installed:").grid(row=row, column=0, sticky="w", **pad)
        self.current_version_label = ttk.Label(conn_frame, text="(connect to check)", foreground="gray")
        self.current_version_label.grid(row=row, column=1, columnspan=3, sticky="w", **pad)
        self.query_btn = ttk.Button(conn_frame, text="Query", command=self.query_current_version)
        self.query_btn.grid(row=row, column=4, **pad)
        self.query_btn_holder = [self.query_btn]
        row += 1

        ttk.Label(conn_frame, text="Bus activity:").grid(row=row, column=0, sticky="w", **pad)
        self.bus_activity_label = ttk.Label(conn_frame, text="(check requires an active connection)", foreground="gray")
        self.bus_activity_label.grid(row=row, column=1, columnspan=3, sticky="w", **pad)
        self.bus_activity_btn = ttk.Button(conn_frame, text="Check (2s)", command=self.check_bus_activity)
        self.bus_activity_btn.grid(row=row, column=4, **pad)
        self.query_btn_holder.append(self.bus_activity_btn)  # locked by _set_ui_busy_state the same way Query is

        # --- Firmware frame ---
        fw_frame = ttk.LabelFrame(left_col, text="2. Select firmware")
        fw_frame.pack(fill="both", expand=True, **pad)

        ttk.Label(fw_frame, text="Detected in firmware/:").grid(row=0, column=0, sticky="w", **pad)
        ttk.Button(fw_frame, text="Refresh", command=self.scan_firmware_folder).grid(row=0, column=1, sticky="w", **pad)

        columns = ("file", "size", "status")
        self.fw_tree = ttk.Treeview(fw_frame, columns=columns, show="headings", height=4, selectmode="browse")
        self.fw_tree.heading("file", text="File")
        self.fw_tree.heading("size", text="Size")
        self.fw_tree.heading("status", text="Status")
        self.fw_tree.column("file", width=260)
        self.fw_tree.column("size", width=80, anchor="e")
        self.fw_tree.column("status", width=220)
        self.fw_tree.grid(row=1, column=0, columnspan=3, sticky="ew", padx=8, pady=2)
        self.fw_tree.bind("<<TreeviewSelect>>", self.select_detected_firmware)
        self.fw_tree.tag_configure("invalid", foreground="red")
        self.fw_tree.tag_configure("valid", foreground="black")

        ttk.Label(fw_frame, text="Or browse anywhere else:").grid(row=2, column=0, sticky="w", **pad)
        ttk.Button(fw_frame, text="Browse .bin...", command=self.browse_firmware).grid(row=2, column=1, sticky="w", **pad)

        self.fw_label = ttk.Label(fw_frame, text="No file selected", foreground="gray")
        self.fw_label.grid(row=3, column=0, columnspan=3, sticky="w", padx=8, pady=(0, 4))

        # --- Action frame ---
        act_frame = ttk.LabelFrame(left_col, text="3. Flash by CAN-OTA")
        act_frame.pack(fill="x", **pad)
        self.trigger_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            act_frame,
            text="Board is currently running the application (send 0x7F0 trigger first)",
            variable=self.trigger_var, width=45,
        ).grid(row=0, column=0, columnspan=3, sticky="w", **pad)
        ttk.Label(
            act_frame,
            text="Uncheck this if the board is already sitting in the bootloader "
                 "(fresh JTAG flash, or no valid application currently present).",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=1, column=0, columnspan=3, sticky="w", padx=8)

        self.erase_eeprom_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            act_frame,
            text="Also erase the persistence EEPROM before flashing",
            variable=self.erase_eeprom_var,
        ).grid(row=2, column=0, columnspan=3, sticky="w", **pad)
        ttk.Label(
            act_frame,
            text="Optional, off by default. Wipes the board's saved tool-parameter "
                 "state (see CANBUS.TXT 0x190-0x192) - not required for a normal "
                 "update, since a version mismatch is already detected and ignored "
                 "automatically, but useful for a genuinely clean slate. Only works "
                 "while the application is running (needs the trigger checkbox above "
                 "checked too) - the bootloader itself doesn't handle this command.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=3, sticky="w", padx=8)

        self.flash_btn = ttk.Button(act_frame, text="Flash Firmware", command=self.start_flash)
        self.flash_btn.grid(row=4, column=0, **pad)
        self.cancel_btn = ttk.Button(act_frame, text="Cancel", command=self.cancel_flash, state="disabled")
        self.cancel_btn.grid(row=4, column=1, **pad)

        # --- SWD/JTAG full-chip programming frame (advanced, separate risk profile) ---
        swd_frame = ttk.LabelFrame(
            right_col, text="4. Program complete chip via SWD/JTAG (advanced)"
        )
        swd_frame.pack(fill="both", expand=True, **pad)

        self._pyocd_ok = PyOCDCLI.available()
        self._cube_ok = CubeProgrammerCLI.available()
        self.swd_tool_var = tk.StringVar(value="pyocd" if self._pyocd_ok else "cube")
        tool_sub = ttk.Frame(swd_frame)
        tool_sub.grid(row=0, column=0, columnspan=3, sticky="w", **pad)
        ttk.Radiobutton(
            tool_sub, text="pyOCD (built-in)" + ("" if self._pyocd_ok else " - not found"),
            value="pyocd", variable=self.swd_tool_var, state="normal" if self._pyocd_ok else "disabled",
            command=self.refresh_swd_probes,
        ).pack(side="top", anchor="w")
        ttk.Radiobutton(
            tool_sub, text="STM32CubeProgrammer" + ("" if self._cube_ok else " - not found"),
            value="cube", variable=self.swd_tool_var, state="normal" if self._cube_ok else "disabled",
            command=self.refresh_swd_probes,
        ).pack(side="top", anchor="w")

        probe_sub = ttk.Frame(tool_sub)
        probe_sub.pack(side="top", anchor="w", pady=(4, 0))
        ttk.Label(probe_sub, text="Probe:").pack(side="left")
        self.swd_probe_var = tk.StringVar(value="")
        self.swd_probe_combo = ttk.Combobox(probe_sub, textvariable=self.swd_probe_var, width=20, state="readonly")
        self.swd_probe_combo.pack(side="left", padx=(4, 4))
        self.swd_probe_refresh_btn = ttk.Button(probe_sub, text="Refresh", command=self.refresh_swd_probes)
        self.swd_probe_refresh_btn.pack(side="left")
        # uid -> description, for turning the combobox's shown text back
        # into the actual --probe/sn value to pass to the subprocess
        self._swd_probe_map = {}

        if not self._pyocd_ok and not self._cube_ok:
            ttk.Label(
                swd_frame,
                text="Neither found. Install pyOCD (pip install pyocd) or "
                     "STM32CubeProgrammer to use this section.",
                foreground="red", wraplength=380, justify="left",
            ).grid(row=1, column=0, columnspan=3, sticky="w", padx=8)

        ttk.Label(swd_frame, text="Bootloader (.bin/.hex/.elf):").grid(row=2, column=0, sticky="w", **pad)
        self.swd_bootloader_var = tk.StringVar()
        ttk.Entry(swd_frame, textvariable=self.swd_bootloader_var, width=20).grid(row=2, column=1, sticky="ew", **pad)
        ttk.Button(swd_frame, text="Browse...", command=self.browse_swd_bootloader).grid(row=2, column=2, **pad)

        ttk.Label(swd_frame, text="Application (.bin/.hex/.elf):").grid(row=3, column=0, sticky="w", **pad)
        self.swd_app_var = tk.StringVar()
        ttk.Entry(swd_frame, textvariable=self.swd_app_var, width=20).grid(row=3, column=1, sticky="ew", **pad)
        ttk.Button(swd_frame, text="Browse...", command=self.browse_swd_app).grid(row=3, column=2, **pad)

        self.swd_dry_run_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            swd_frame,
            text="Dry run - show commands without running them (recommended first)",
            variable=self.swd_dry_run_var,
        ).grid(row=4, column=0, columnspan=3, sticky="w", padx=8)

        self.check_ob_btn = ttk.Button(
            swd_frame, text="Check Option Bytes", command=self.start_check_option_bytes,
            state="normal" if self._cube_ok else "disabled",
        )
        self.check_ob_btn.grid(row=5, column=0, columnspan=3, sticky="w", **pad)
        ttk.Label(
            swd_frame,
            text="Read-only RDP check via STM32CubeProgrammer (pyOCD doesn't expose "
                 "this the same way). RDP0/1 are both reversible; RDP2 is the one "
                 "permanent lock-out this tool has been careful about."
                 + ("" if self._cube_ok else " Needs STM32CubeProgrammer."),
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=6, column=0, columnspan=3, sticky="w", padx=8)

        self.swd_flash_btn = ttk.Button(
            swd_frame, text="Flash Complete Chip", command=self.start_swd_flash,
            state="normal" if (self._pyocd_ok or self._cube_ok) else "disabled",
        )
        self.swd_flash_btn.grid(row=7, column=0, columnspan=3, sticky="w", **pad)
        ttk.Label(
            swd_frame,
            text="Erases the WHOLE chip (bootloader included) and writes both images fresh. "
                 "Unlike \"3. Flash\", an interruption here isn't self-healing - the board won't "
                 "run anything until reprogrammed. Recovery is just doing this again via SWD, "
                 "same as this tool's own worst case - not a permanent brick.",
            foreground="#b35900", wraplength=380, justify="left",
        ).grid(row=8, column=0, columnspan=3, sticky="w", padx=8)

        self.progress = ttk.Progressbar(root, orient="horizontal", mode="determinate", maximum=100)
        self.progress.grid(row=1, column=0, columnspan=2, sticky="ew", padx=8, pady=(4, 8))

        # --- Log frame ---
        log_frame = ttk.LabelFrame(root, text="Log")
        log_frame.grid(row=2, column=0, columnspan=2, sticky="nsew", **pad)
        log_toolbar = ttk.Frame(log_frame)
        log_toolbar.pack(fill="x", side="top")
        ttk.Button(log_toolbar, text="Export Debug Bundle...", command=self.export_debug_bundle).pack(
            side="left", padx=4, pady=2
        )
        self.log_text = tk.Text(log_frame, height=8, state="disabled", wrap="word")
        self.log_text.pack(fill="both", expand=True, side="left")
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        scrollbar.pack(side="right", fill="y")
        self.log_text.config(yscrollcommand=scrollbar.set)

        self.refresh_ports()
        self.scan_firmware_folder()
        self.log(f"URTC Flasher ready. HardwareID: 0x{THIS_HARDWARE_ID:08X}, "
                  f"firmware version {FIRMWARE_VERSION_MAJOR}.{FIRMWARE_VERSION_MINOR}")
        if _CONFIG_LOADED:
            _load_config_overrides(self.log)  # re-run just to emit its own log line - values don't change
        if self.transport_var.get() == "serial":
            self.log("Expecting the adapter in SLCAN mode - see the README if your "
                      "CANable shows up as anything other than a serial port.")
        else:
            self.log("SocketCAN mode - expecting the interface already up at "
                      "500 kbit/s (ip link set ... up type can bitrate 500000). "
                      "See the README's Linux section.")

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        if self._pyocd_ok or self._cube_ok:
            self.refresh_swd_probes()

    def export_debug_bundle(self):
        save_path = filedialog.asksaveasfilename(
            title="Save Debug Bundle",
            defaultextension=".zip",
            initialfile=f"urtc_flasher_debug_{time.strftime('%Y%m%d_%H%M%S')}.zip",
            filetypes=[("ZIP archive", "*.zip")],
        )
        if not save_path:
            return
        try:
            with zipfile.ZipFile(save_path, "w", zipfile.ZIP_DEFLATED) as zf:
                # On-screen log, exactly as currently shown - not just the
                # file logger's copy, in case file logging failed to set up
                zf.writestr("session_log.txt", self.log_text.get("1.0", "end"))

                diag = [
                    f"URTC Flasher version: {FLASHER_VERSION}",
                    f"Python: {sys.version}",
                    f"Platform: {platform.platform()}",
                    f"pyserial available: {HAVE_SERIAL}",
                    f"SocketCAN available: {HAVE_SOCKETCAN}",
                    f"pyOCD found: {PyOCDCLI.available()}",
                    f"STM32CubeProgrammer found: {CubeProgrammerCLI.available()}",
                    f"Current transport: {self.transport_var.get()}",
                    f"Current port/interface: {self.port_var.get()}",
                    f"Current bitrate (Serial/SLCAN): {self.bitrate_var.get()}",
                    f"Connected: {self.transport is not None}",
                    f"Selected CAN firmware file: {self.firmware_path or '(none)'}",
                    f"Selected SWD bootloader file: {self.swd_bootloader_var.get() or '(none)'}",
                    f"Selected SWD application file: {self.swd_app_var.get() or '(none)'}",
                ]
                zf.writestr("system_diagnostics.txt", "\n".join(diag))

                # The firmware file itself, if one's selected and still
                # readable - the single most useful thing to have alongside
                # the log when diagnosing a failed flash after the fact.
                if self.firmware_path and os.path.isfile(self.firmware_path):
                    zf.write(self.firmware_path, "firmware/" + os.path.basename(self.firmware_path))

            messagebox.showinfo("Debug bundle saved", f"Saved to:\n{save_path}")
            self.log(f"Debug bundle exported to {save_path}")
        except OSError as e:
            messagebox.showerror("Export failed", str(e))

    def on_close(self):
        # Closing the window mid-flash used to just kill the process - no
        # warning, no chance to close the transport cleanly. A CAN OTA
        # update left interrupted this way is still safe (golden-image
        # backup slot), but the board is left waiting on a bootloader
        # timeout rather than resuming normal operation right away, and an
        # SWD/JTAG full-chip flash has no such safety net at all - worth a
        # confirmation either way rather than a silent kill.
        flashing = bool(
            (self._flash_thread and self._flash_thread.is_alive())
            or (self._swd_flash_thread and self._swd_flash_thread.is_alive())
        )
        if flashing:
            if not messagebox.askyesno(
                "Flash in progress",
                "A flash is currently running. Closing now will abandon it "
                "mid-operation rather than cancelling cleanly.\n\n"
                "Close anyway?",
            ):
                return
        if self.transport is not None:
            try:
                self.transport.close()
            except Exception:
                pass
        self.root.destroy()

    def log(self, msg):
        if self._file_logger:
            try:
                self._file_logger.info(msg)
            except OSError:
                pass  # disk full, permissions changed mid-session, etc. - on-screen log still works
        def _append():
            self.log_text.config(state="normal")
            self.log_text.insert("end", msg + "\n")
            self.log_text.see("end")
            self.log_text.config(state="disabled")
        self.root.after(0, _append)

    def on_transport_change(self):
        self.port_label.config(text="COM port:" if self.transport_var.get() == "serial" else "CAN interface:")
        self.port_var.set("")
        is_serial = self.transport_var.get() == "serial"
        self.bitrate_combo.config(state="readonly" if is_serial else "disabled")
        self.autobaud_btn.config(state="normal" if is_serial else "disabled")
        self.bitrate_status.config(text="" if is_serial else "SocketCAN's bitrate is set at the OS level (ip link), not here")
        self.refresh_ports()

    def refresh_ports(self):
        if self.transport_var.get() == "serial":
            ports = [p.device for p in serial.tools.list_ports.comports()] if HAVE_SERIAL else []
        else:
            ports = list_socketcan_interfaces()
            if not ports:
                self.log("No SocketCAN interfaces found. Bring one up first, e.g.:\n"
                          "    sudo ip link set can0 type can bitrate 500000\n"
                          "    sudo ip link set can0 up")
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def toggle_connect(self):
        if self.transport is None:
            port = self.port_var.get()
            if not port:
                messagebox.showerror("Nothing selected", "Select a port/interface first.")
                return
            is_serial = self.transport_var.get() == "serial"
            try:
                if is_serial:
                    bitrate_label = self.bitrate_var.get()
                    bitrate_code = next((code for label, code in SLCAN_BITRATES if label == bitrate_label),
                                         BITRATE_500K_SLCAN_CODE)
                    self.transport = SLCAN(port, log=self.log)
                    self.transport.open_channel(bitrate_code)
                else:
                    self.transport = SocketCAN(port, log=self.log)
                    self.transport.open_channel()
                self.conn_status.config(text=f"Connected ({port})", foreground="green")
                self.connect_btn.config(text="Disconnect")
                self.log(f"Connected to {port}, CAN channel open"
                          + (f" at {self.bitrate_var.get()}." if is_serial else "."))
                self.query_current_version()
            except Exception as e:
                self.transport = None
                msg = str(e)
                # A permission-denied serial error is the single most common
                # first-run snag on Linux (unlike Windows, opening a serial
                # device needs group membership there) - worth a specific,
                # actionable message instead of just surfacing pyserial's
                # raw exception text.
                if is_serial and ("Permission denied" in msg or "Errno 13" in msg):
                    messagebox.showerror(
                        "Connection failed - permission denied",
                        f"{msg}\n\n"
                        f"On Linux, serial devices usually need your user in "
                        f"the 'dialout' group:\n\n"
                        f"    sudo usermod -a -G dialout $USER\n\n"
                        f"Then log out and back in (group membership is "
                        f"read at login) and try again.",
                    )
                else:
                    messagebox.showerror("Connection failed", msg)
        else:
            try:
                self.transport.close()
            except Exception:
                pass
            self.transport = None
            self.conn_status.config(text="Not connected", foreground="red")
            self.connect_btn.config(text="Connect")
            self.current_version_label.config(text="(connect to check)", foreground="gray")
            self.log("Disconnected.")

    def scan_firmware_folder(self):
        for item in self.fw_tree.get_children():
            self.fw_tree.delete(item)
        self._detected_paths = {}

        matches = []
        if os.path.isdir(FIRMWARE_FOLDER):
            matches = sorted(glob.glob(os.path.join(FIRMWARE_FOLDER, "*.bin")))

        if not os.path.isdir(FIRMWARE_FOLDER):
            self.log(f"No firmware/ folder found inside tools/ (looked in: {FIRMWARE_FOLDER}). "
                      f"Use Browse instead, or create that folder and put a .bin there.")
            return
        if not matches:
            self.log(f"firmware/ folder found but no .bin files in it ({FIRMWARE_FOLDER}).")
            return

        valid_entries = []
        for path in matches:
            name = os.path.basename(path)
            is_valid, reason, size = validate_firmware_file(path)
            size_str = f"{size/1024:.1f} KB" if size else "-"
            status_str = "\u2713 " + reason if is_valid else "\u2717 " + reason
            tag = "valid" if is_valid else "invalid"
            item_id = self.fw_tree.insert("", "end", values=(name, size_str, status_str), tags=(tag,))
            self._detected_paths[item_id] = (path, is_valid)
            if is_valid:
                valid_entries.append((item_id, path, name))

        self.log(f"Scanned firmware/: {len(matches)} file(s) found, {len(valid_entries)} look valid.")
        for path in matches:
            name = os.path.basename(path)
            is_valid, reason, size = validate_firmware_file(path)
            if not is_valid:
                self.log(f"  \u2717 {name}: {reason}")

        if len(valid_entries) == 1:
            # Exactly one VALID candidate - not just one file period. An
            # invalid file sitting alone in the folder should never get
            # auto-selected just because nothing else is competing with it.
            item_id, path, name = valid_entries[0]
            self.fw_tree.selection_set(item_id)
            self.firmware_path = path
            self.fw_label.config(text=path, foreground="black")
            self.log(f"Auto-selected the only valid firmware found: {name}")
        elif len(valid_entries) > 1:
            self.log(f"Multiple valid firmware files found - select one from the list above.")

    def select_detected_firmware(self, event=None):
        selection = self.fw_tree.selection()
        if not selection:
            return
        path, is_valid = self._detected_paths.get(selection[0], (None, False))
        if path is None:
            return
        if not is_valid:
            if not messagebox.askyesno(
                "File looks invalid",
                "This file doesn't look like a valid URTC firmware image "
                "(see the Status column for why). Selecting it anyway is "
                "possible, but it will almost certainly be rejected by the "
                "bootloader's own verification - or worse, waste a full "
                "transfer attempt before being rejected.\n\n"
                "Select it anyway?",
            ):
                return
        self.firmware_path = path
        self.fw_label.config(text=path, foreground="black" if is_valid else "red")

    def browse_firmware(self):
        initial_dir = FIRMWARE_FOLDER if os.path.isdir(FIRMWARE_FOLDER) else None
        path = filedialog.askopenfilename(
            title="Select URTC firmware",
            initialdir=initial_dir,
            filetypes=[("Firmware binary", "*.bin"), ("All files", "*.*")],
        )
        if not path:
            return
        is_valid, reason, size = validate_firmware_file(path)
        if not is_valid:
            if not messagebox.askyesno(
                "File looks invalid",
                f"This file doesn't look like a valid URTC firmware image:\n\n"
                f"{reason}\n\nSelect it anyway?",
            ):
                return
        self.firmware_path = path
        self.fw_label.config(text=path, foreground="black" if is_valid else "red")
        self.fw_tree.selection_remove(self.fw_tree.selection())  # no detected-list item matches a manual browse

    def check_bus_activity(self):
        if self.transport is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        self.bus_activity_label.config(text="Listening for 2s...", foreground="gray")
        self._set_ui_busy_state(True)
        threading.Thread(target=self._check_bus_activity_worker, daemon=True).start()

    def _check_bus_activity_worker(self):
        # Counts real protocol frames actually seen over a fixed window -
        # this is deliberately NOT the same thing as a true CAN bus-load
        # percentage or the controller's own REC/TEC error counters, which
        # would need a netlink query (SocketCAN) or adapter-specific
        # extensions (SLCAN) this project doesn't have a standard,
        # dependency-free way to get. What this DOES give: a genuine,
        # directly-measured "is anything talking on this bus at all, and
        # about how often" signal, on either transport.
        frame_count = 0
        is_socketcan = isinstance(self.transport, SocketCAN)
        before = SocketCAN.read_interface_stats(self.transport.interface) if is_socketcan else None
        deadline = time.time() + 2.0
        try:
            while time.time() < deadline:
                frame = self.transport.read_frame(timeout=0.1)
                if frame is not None:
                    frame_count += 1
        except Exception as e:
            self.log(f"Bus activity check: {e}")
        after = SocketCAN.read_interface_stats(self.transport.interface) if is_socketcan else None
        self.root.after(0, lambda: self._show_bus_activity_result(frame_count, before, after))

    def _show_bus_activity_result(self, frame_count, before, after):
        self._set_ui_busy_state(False)
        rate = frame_count / 2.0
        text = f"{frame_count} frames in 2s (~{rate:.1f}/s)"
        self.bus_activity_label.config(text=text, foreground="green" if frame_count > 0 else "orange")
        self.log(f"Bus activity: {text}")
        if before is not None and after is not None:
            deltas = {k: after[k] - before[k] for k in after}
            err_total = deltas["rx_errors"] + deltas["tx_errors"] + deltas["rx_dropped"] + deltas["tx_dropped"]
            self.log(f"  SocketCAN interface counters (2s delta): {deltas}"
                      + ("  -- errors/drops seen, worth investigating" if err_total > 0 else ""))

    def query_current_version(self):
        if self.transport is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        self.current_version_label.config(text="Querying...", foreground="gray")
        self._set_ui_busy_state(True)

        def _worker():
            try:
                flasher = URTCFlasher(self.transport, log=self.log)
                result = flasher.query_version()
                self.root.after(0, lambda: self._show_version_result(result))
            finally:
                self.root.after(0, lambda: self._set_ui_busy_state(False))

        threading.Thread(target=_worker, daemon=True).start()

    def auto_detect_bitrate(self):
        if self.transport is not None:
            messagebox.showerror("Already connected", "Disconnect first, then auto-detect before reconnecting.")
            return
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Nothing selected", "Select a COM port first.")
            return

        self._set_ui_busy_state(True)
        self.bitrate_status.config(text="Trying each bitrate...", foreground="gray")

        def _worker():
            found_label = None
            try:
                # Most-likely-first order, not just the table's natural
                # order: URTC's bus is fixed at 500k, so that's tried
                # first, then its two most common neighbors.
                try_order = ["500 kbit/s", "250 kbit/s", "125 kbit/s", "1 Mbit/s",
                             "100 kbit/s", "50 kbit/s", "20 kbit/s", "10 kbit/s", "800 kbit/s"]
                for label in try_order:
                    code = next(c for l, c in SLCAN_BITRATES if l == label)
                    self.log(f"Auto-detect: trying {label}...")
                    trial = None
                    try:
                        trial = SLCAN(port, log=lambda m: None)  # quiet - avoid spamming the log with 9 failed attempts
                        trial.open_channel(code)
                        flasher = URTCFlasher(trial, log=lambda m: None)
                        result = flasher.query_version(timeout=0.8)
                        if result is not None:
                            found_label = label
                            break
                    except (SLCANError, OSError):
                        pass
                    finally:
                        if trial is not None:
                            try:
                                trial.close()
                            except Exception:
                                pass
            finally:
                self.root.after(0, lambda: self._auto_detect_done(found_label))

        threading.Thread(target=_worker, daemon=True).start()

    def _auto_detect_done(self, found_label):
        self._set_ui_busy_state(False)
        if found_label:
            self.bitrate_var.set(found_label)
            self.bitrate_status.config(text=f"Found: {found_label}", foreground="green")
            self.log(f"Auto-detect: board responded at {found_label}.")
        else:
            self.bitrate_status.config(text="No response at any standard bitrate", foreground="red")
            self.log("Auto-detect: no response at any standard bitrate - check wiring/power, "
                      "or the board may be using a non-standard bitrate this can't try.")

    def _show_version_result(self, result):
        if result is None:
            self.current_version_label.config(
                text="No response - board unresponsive, wrong bitrate, or not connected",
                foreground="red",
            )
            return
        hw_match = "" if result["hardware_id"] == THIS_HARDWARE_ID else "  \u26a0 HardwareID mismatch!"
        boot_ver = result.get("bootloader_version")
        boot_ver_text = f", bootloader v{boot_ver[0]}.{boot_ver[1]}.{boot_ver[2]}" if boot_ver else ""
        if result["responder"] == "bootloader" and result["hardware_id"] == 0:
            text = f"Bootloader running, no valid firmware currently installed{boot_ver_text}"
            color = "orange"
        else:
            text = (
                f"v{result['version_major']}.{result['version_minor']} "
                f"({result['responder']}, HardwareID 0x{result['hardware_id']:08X}){hw_match}{boot_ver_text}"
            )
            color = "green" if not hw_match else "red"
        self.current_version_label.config(text=text, foreground=color)
        self.log(f"Current version query: {text}")

    def _set_ui_busy_state(self, busy):
        # Centralizes exactly which controls need to be locked while either
        # flash path is running - both touch self.transport (CAN path) or
        # spawn their own subprocess (SWD path), and letting Connect/
        # Disconnect/Query/the port selector fire concurrently from the
        # main thread was a real race condition: e.g. clicking Disconnect
        # mid-transfer closes the same serial port/socket the flash thread
        # is still reading from.
        state = "disabled" if busy else "normal"
        self.connect_btn.config(state=state)
        self.port_combo.config(state="disabled" if busy else "readonly")
        for child in self.query_btn_holder:
            child.config(state=state)
        if hasattr(self, "transport_radio_serial"):
            self.transport_radio_serial.config(state="disabled" if busy else ("normal" if HAVE_SERIAL else "disabled"))
        if hasattr(self, "transport_radio_socketcan"):
            self.transport_radio_socketcan.config(state="disabled" if busy else "normal")
        self.flash_btn.config(state="disabled" if busy else "normal")
        self.swd_flash_btn.config(
            state="disabled" if (busy or not (self._pyocd_ok or self._cube_ok)) else "normal"
        )

    def start_flash(self):
        if self.transport is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        if not self.firmware_path:
            messagebox.showerror("No firmware", "Select a .bin file first.")
            return
        if self._flash_thread and self._flash_thread.is_alive():
            messagebox.showerror("Busy", "A flash is already in progress.")
            return
        if not messagebox.askyesno(
            "Confirm flash",
            "This will erase the backup slot and, once verified, replace the "
            "main application slot with this firmware.\n\n"
            "The currently running firmware stays intact until the new image "
            "is fully verified, so this is safe to retry if it fails - but "
            "double check you selected the right file.\n\nProceed?",
        ):
            return

        self._stop_requested = False
        self._set_ui_busy_state(True)
        self.cancel_btn.config(state="normal")
        self.progress["value"] = 0

        self._flash_thread = threading.Thread(target=self._flash_worker, daemon=True)
        self._flash_thread.start()

    def cancel_flash(self):
        self._stop_requested = True
        self.log("Cancel requested - stopping after the current step...")

    def _flash_worker(self):
        try:
            flasher = URTCFlasher(
                self.transport,
                log=self.log,
                progress_cb=lambda pct: self.root.after(0, lambda: self.progress.configure(value=pct)),
                stop_flag=lambda: self._stop_requested,
            )
            if self.erase_eeprom_var.get():
                if not self.trigger_var.get():
                    self.log("Skipping EEPROM erase: needs the application running "
                              "(the trigger checkbox above is unchecked, so the board "
                              "is assumed to already be in the bootloader, which doesn't "
                              "handle this command).")
                else:
                    flasher.erase_eeprom()
            if self.trigger_var.get():
                flasher.trigger_bootloader_entry()
            flasher.flash(self.firmware_path)
            self.root.after(0, lambda: messagebox.showinfo("Success", "Firmware update complete."))
        except FlashError as e:
            self.log(f"FAILED: {e}")
            self.root.after(0, lambda: messagebox.showerror("Flash failed", str(e)))
        except Exception as e:
            self.log(f"UNEXPECTED ERROR: {e}")
            self.root.after(0, lambda: messagebox.showerror("Unexpected error", str(e)))
        finally:
            self.root.after(0, lambda: self._set_ui_busy_state(False))
            self.root.after(0, lambda: self.cancel_btn.config(state="disabled"))

    def browse_swd_bootloader(self):
        path = filedialog.askopenfilename(
            title="Select bootloader image",
            filetypes=[("Bootloader image", "*.bin *.hex *.elf *.axf"), ("All files", "*.*")],
        )
        if not path:
            return
        is_valid, reason, size = validate_swd_image_file(path, BOOTLOADER_MAX_SIZE, "bootloader", BOOTLOADER_FLASH_ADDR)
        if not is_valid:
            if not messagebox.askyesno(
                "File looks invalid",
                f"This doesn't look like a valid bootloader image:\n\n{reason}\n\n"
                f"Select it anyway?",
            ):
                return
        self.swd_bootloader_var.set(path)

    def browse_swd_app(self):
        path = filedialog.askopenfilename(
            title="Select application image",
            filetypes=[("Application image", "*.bin *.hex *.elf *.axf"), ("All files", "*.*")],
        )
        if not path:
            return
        is_valid, reason, size = validate_swd_image_file(path, APP_MAX_SIZE, "application", APP_FLASH_ADDR)
        if not is_valid:
            if not messagebox.askyesno(
                "File looks invalid",
                f"This doesn't look like a valid application image:\n\n{reason}\n\n"
                f"Select it anyway?",
            ):
                return
        self.swd_app_var.set(path)

    def refresh_swd_probes(self):
        if not (self._pyocd_ok or self._cube_ok):
            return
        self.swd_probe_combo.set("(checking...)")
        self.swd_probe_refresh_btn.config(state="disabled")
        threading.Thread(target=self._refresh_swd_probes_worker, daemon=True).start()

    def _refresh_swd_probes_worker(self):
        tool = self.swd_tool_var.get()
        try:
            if tool == "pyocd" and self._pyocd_ok:
                prog = PyOCDCLI(log=lambda m: None)
            elif tool == "cube" and self._cube_ok:
                prog = CubeProgrammerCLI(log=lambda m: None)
            else:
                self.root.after(0, lambda: self._show_swd_probes([]))
                return
            probes = prog.list_probes()
        except Exception:
            probes = []
        self.root.after(0, lambda: self._show_swd_probes(probes))

    def _show_swd_probes(self, probes):
        self.swd_probe_refresh_btn.config(state="normal")
        # pyOCD gives (uid, description) pairs; CubeProgrammer gives bare
        # serial strings - normalize both into the same uid->label map so
        # the rest of the UI doesn't need to care which tool is active.
        if probes and isinstance(probes[0], tuple):
            self._swd_probe_map = {uid: desc for uid, desc in probes if uid}
        else:
            self._swd_probe_map = {serial: serial for serial in probes}
        labels = list(self._swd_probe_map.values())
        self.swd_probe_combo["values"] = labels
        if len(labels) == 1:
            self.swd_probe_combo.set(labels[0])
        elif len(labels) == 0:
            self.swd_probe_combo.set("(none found)")
        else:
            self.swd_probe_combo.set("")  # force an explicit choice when there's more than one

    def _selected_swd_probe_uid(self):
        """Turns the combobox's shown label back into the actual uid/serial
        to pass to the subprocess - returns None if nothing usable is
        selected (no probes, or multiple with nothing explicitly chosen)."""
        label = self.swd_probe_var.get()
        for uid, desc in self._swd_probe_map.items():
            if desc == label:
                return uid
        return None

    def start_check_option_bytes(self):
        if not self._cube_ok:
            messagebox.showerror("Not available", "This check needs STM32CubeProgrammer specifically.")
            return
        if len(self._swd_probe_map) > 1 and not self._selected_swd_probe_uid():
            messagebox.showerror(
                "Multiple probes connected",
                "More than one ST-Link is connected - pick one from the Probe dropdown first.",
            )
            return
        self._set_ui_busy_state(True)
        threading.Thread(target=self._check_option_bytes_worker, daemon=True).start()

    def _check_option_bytes_worker(self):
        try:
            prog = CubeProgrammerCLI(log=self.log)
            level, output = prog.read_option_bytes(serial=self._selected_swd_probe_uid(), dry_run=False)
            self.root.after(0, lambda: self._show_option_bytes_result(level))
        except SWDFlashError as e:
            self.log(f"FAILED: {e}")
            self.root.after(0, lambda: messagebox.showerror("Option byte check failed", str(e)))
        except Exception as e:
            self.log(f"UNEXPECTED ERROR: {e}")
            self.root.after(0, lambda: messagebox.showerror("Unexpected error", str(e)))
        finally:
            self.root.after(0, lambda: self._set_ui_busy_state(False))

    def _show_option_bytes_result(self, level):
        if level == "2":
            messagebox.showerror(
                "RDP Level 2 detected - PERMANENT",
                "This chip's read-out protection is set to Level 2.\n\n"
                "Unlike every other risk this tool warns about, RDP2 is "
                "permanent and irreversible by ST's own design - the debug "
                "port stays locked out forever, with no recovery path via "
                "SWD/JTAG or anything else. If you didn't intend this, stop "
                "here rather than proceeding with any full-chip operation.",
            )
        elif level == "1":
            messagebox.showwarning(
                "RDP Level 1 detected",
                "This chip's read-out protection is set to Level 1.\n\n"
                "This is reversible (STM32CubeProgrammer's -rdu / Read "
                "Unprotect), but doing so mass-erases the chip as part of "
                "removing the protection - not something this tool does "
                "automatically. If a full-chip flash then fails with an RDP-"
                "related error, that's why.",
            )
        elif level == "0":
            messagebox.showinfo("RDP Level 0", "No read-out protection - normal for a development board.")
        else:
            messagebox.showinfo(
                "Couldn't confidently parse RDP level",
                "The option byte dump didn't match a recognized pattern - "
                "see the log for the raw output STM32CubeProgrammer printed.",
            )

    def start_swd_flash(self):
        if self._flash_thread and self._flash_thread.is_alive():
            messagebox.showerror("Busy", "A CAN update is already in progress.")
            return
        if self._swd_flash_thread and self._swd_flash_thread.is_alive():
            messagebox.showerror("Busy", "An SWD/JTAG flash is already in progress.")
            return
        bootloader_path = self.swd_bootloader_var.get().strip()
        app_path = self.swd_app_var.get().strip()
        if not bootloader_path or not os.path.isfile(bootloader_path):
            messagebox.showerror("Missing bootloader image", "Select a valid bootloader .bin/.hex/.elf file first.")
            return
        if not app_path or not os.path.isfile(app_path):
            messagebox.showerror("Missing application image", "Select a valid application .bin/.hex/.elf file first.")
            return

        # Re-validated here too, not just in the Browse dialog above - the
        # path fields are plain text entries, so a manually typed or
        # pasted path would otherwise skip the check entirely.
        ok, reason, _ = validate_swd_image_file(bootloader_path, BOOTLOADER_MAX_SIZE, "bootloader", BOOTLOADER_FLASH_ADDR)
        if not ok and not messagebox.askyesno(
            "Bootloader file looks invalid",
            f"{reason}\n\nProceed anyway?",
        ):
            return
        ok, reason, _ = validate_swd_image_file(app_path, APP_MAX_SIZE, "application", APP_FLASH_ADDR)
        if not ok and not messagebox.askyesno(
            "Application file looks invalid",
            f"{reason}\n\nProceed anyway?",
        ):
            return

        if len(self._swd_probe_map) > 1 and not self._selected_swd_probe_uid():
            messagebox.showerror(
                "Multiple probes connected",
                "More than one ST-Link/probe is connected - pick one from the "
                "Probe dropdown before flashing the complete chip.",
            )
            return

        dry_run = self.swd_dry_run_var.get()
        tool_name = "pyOCD" if self.swd_tool_var.get() == "pyocd" else "STM32CubeProgrammer"
        probe_uid = self._selected_swd_probe_uid()
        probe_line = f"Probe: {self.swd_probe_var.get()}\n" if probe_uid else ""

        if dry_run:
            proceed = messagebox.askyesno(
                "Dry run",
                f"This will print the exact {tool_name} commands to the log WITHOUT running "
                f"them. Nothing on the board changes. Proceed?",
            )
        else:
            proceed = messagebox.askyesno(
                "Confirm FULL CHIP programming",
                "This erases the ENTIRE chip, including the bootloader, then writes both "
                "images fresh.\n\n"
                "Unlike \"3. Flash\" above, this isn't self-healing: if the connection drops "
                "mid-erase or mid-write, the board won't run anything until it's reprogrammed. "
                "That just means doing this again via SWD - the debug port itself doesn't "
                "depend on flash contents, so it's always still there to reconnect to. This "
                "tool never touches option bytes, so it has no path to the one STM32 failure "
                "mode that IS permanent (RDP2 read protection).\n\n"
                f"Tool: {tool_name}\n"
                f"{probe_line}"
                f"Bootloader: {bootloader_path}\n"
                f"Application: {app_path}\n\n"
                "Double-check both files (and the probe, if more than one board is connected) "
                "are correct before continuing. Proceed?",
            )
        if not proceed:
            return

        self._set_ui_busy_state(True)
        self._swd_flash_thread = threading.Thread(
            target=self._swd_flash_worker,
            args=(bootloader_path, app_path, self.swd_tool_var.get(), dry_run, probe_uid),
            daemon=True,
        )
        self._swd_flash_thread.start()

    def _swd_flash_worker(self, bootloader_path, app_path, tool, dry_run, probe_uid=None):
        try:
            if tool == "pyocd":
                programmer = PyOCDCLI(log=self.log)
                programmer.full_chip_flash(bootloader_path, app_path, dry_run=dry_run, probe_uid=probe_uid)
            else:
                programmer = CubeProgrammerCLI(log=self.log)
                programmer.full_chip_flash(bootloader_path, app_path, dry_run=dry_run, serial=probe_uid)
            if dry_run:
                self.root.after(0, lambda: messagebox.showinfo(
                    "Dry run complete", "Commands printed to the log - nothing was executed."))
            else:
                self.root.after(0, lambda: messagebox.showinfo(
                    "Success", "Full-chip programming complete."))
        except SWDFlashError as e:
            self.log(f"FAILED: {e}")
            self.root.after(0, lambda: messagebox.showerror("SWD/JTAG flash failed", str(e)))
        except Exception as e:
            self.log(f"UNEXPECTED ERROR: {e}")
            self.root.after(0, lambda: messagebox.showerror("Unexpected error", str(e)))
        finally:
            self.root.after(0, lambda: self._set_ui_busy_state(False))


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
    parser.add_argument("--transport", choices=["serial", "socketcan"], default="serial",
                         help="Serial/SLCAN (default, all platforms) or SocketCAN (Linux only)")
    parser.add_argument("--port", required=True,
                         help="Serial port (e.g. COM3 or /dev/ttyACM0) for --transport serial, "
                              "or interface name (e.g. can0) for --transport socketcan")
    parser.add_argument("--file", required=True, help="Firmware .bin file to flash")
    parser.add_argument("--no-trigger", action="store_true",
                         help="Skip the 0x7F0 bootloader-entry trigger - use this if the board "
                              "is already sitting in the bootloader (fresh JTAG flash, or no "
                              "valid application currently present)")
    parser.add_argument("--force", action="store_true",
                         help="Flash even if the file fails the plausibility check")
    args = parser.parse_args(argv)

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
        else:
            transport = SocketCAN(args.port, log=_cli_log)
        transport.open_channel()
        _cli_log(f"Connected via {args.transport} on {args.port}.")

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


def _center_geometry(win, width, height):
    """Returns a "WxH+X+Y" geometry string centered on the screen -
    shared by both the splash and the main window below. Uses the
    caller's own known target width/height rather than winfo_width()/
    height(), which depend on the window having already been fully
    realized/mapped by the window manager - a real source of "it isn't
    actually centered" bugs if queried too early, since those can still
    report a stale placeholder size at that point. On a multi-monitor
    system, winfo_screenwidth()/height() report the full virtual desktop
    Tkinter sees, not necessarily one physical monitor - which is a
    Tkinter/Tk limitation this can't fully work around without a
    platform-specific dependency, but it's still correctly centered on
    that combined canvas, matching what plain Tkinter apps generally do.
    """
    win.update_idletasks()
    ws = win.winfo_screenwidth()
    hs = win.winfo_screenheight()
    x = max(0, (ws - width) // 2)
    y = max(0, (hs - height) // 2)
    return f"{width}x{height}+{x}+{y}"


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
    root = tk.Tk()
    root.withdraw()  # hidden until the splash finishes, rather than flashing empty then populated
    root.geometry(_center_geometry(root, 1286, 1020))
    app = FlasherGUI(root)

    def _reveal_main():
        root.deiconify()

    _show_splash_then(root, _reveal_main)
    root.mainloop()


if __name__ == "__main__":
    main()