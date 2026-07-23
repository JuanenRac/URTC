#!/usr/bin/env python3
"""
URTC Tester - live CAN bus exerciser for the URTC (Universal Robot Tool
Controller) board.
Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>

Licensed under the GNU General Public License v3.0 (GPL-3.0), matching the
URTC firmware and the flasher tool. See LICENSE in the repository root.

Connects over the same USB-CAN adapter/interface the flasher uses, asks
the board which of its 12 tool profiles it's currently jumpered for, and
shows only that tool's own controls and live telemetry - per CANBUS.TXT -
rather than one window trying to represent all 12 at once.

Shares its transport layer (SLCAN/SocketCAN) with urtc_flasher.py, since
both tools ultimately just need to get CAN frames on and off the same
kind of adapter - but this tool never touches flash: everything it does
is runtime commands and telemetry against the currently-running
application, nothing that can leave the board any less working than it
started. Zero non-stdlib dependencies beyond pyserial, matching the flasher.
"""

import sys
import os
import json
import time
import re
import logging
import struct
import threading
import socket
import zipfile
import platform
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

try:
    import serial
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False

HAVE_SOCKETCAN = hasattr(socket, "AF_CAN")

TESTER_VERSION = "1.0"
TESTER_AUTHOR = "JuanenRac"

# THIS_HARDWARE_ID matches BOOTLOADER.C/STM32F303CC.C's own constant - used
# here only to flag a mismatch after connecting (a different board/rig
# answering than the one this was set up for), never to gate anything.
THIS_HARDWARE_ID = 0x0303CC01

BITRATE_500K_SLCAN_CODE = "6"  # SLCAN's "Sx" bitrate codes: 6 = 500 kbit/s
SLCAN_BITRATES = [
    ("10 kbit/s", "0"), ("20 kbit/s", "1"), ("50 kbit/s", "2"),
    ("100 kbit/s", "3"), ("125 kbit/s", "4"), ("250 kbit/s", "5"),
    ("500 kbit/s", "6"), ("800 kbit/s", "7"), ("1 Mbit/s", "8"),
]

if getattr(sys, "frozen", False):
    base_dir = os.path.dirname(sys.executable)
else:
    base_dir = os.path.dirname(os.path.abspath(__file__))
LOGS_FOLDER = os.path.normpath(os.path.join(base_dir, "logs"))
CUSTOM_IDS_PATH = os.path.normpath(os.path.join(base_dir, "urtc_custom_ids.json"))


def _load_custom_id_names():
    """Optional, not included by default: urtc_custom_ids.json next to
    this script, mapping hex ID strings to friendly names - e.g.
    {"0x199": "My Expansion Sensor"}. Lets someone testing a custom
    expansion board or addition give their own IDs a readable name in the
    Raw Bus Monitor without needing to modify this tool's source at all.
    Missing file is silent (this is opt-in); a present-but-broken file is
    logged and ignored rather than crashing the tool over a typo."""
    if not os.path.isfile(CUSTOM_IDS_PATH):
        return {}
    try:
        with open(CUSTOM_IDS_PATH) as f:
            raw = json.load(f)
        result = {}
        for key, name in raw.items():
            try:
                result[int(key, 0)] = str(name)
            except (ValueError, TypeError):
                continue  # one bad entry doesn't invalidate the rest of the file
        return result
    except (OSError, json.JSONDecodeError):
        return {}


CUSTOM_ID_NAMES = _load_custom_id_names()

if getattr(sys, "frozen", False):
    BANNER_IMAGE_PATH = os.path.normpath(os.path.join(sys._MEIPASS, "assets", "urtc_tester_banner.png"))
    ICON_IMAGE_PATH = os.path.normpath(os.path.join(sys._MEIPASS, "assets", "urtc_icon.png"))
else:
    BANNER_IMAGE_PATH = os.path.normpath(os.path.join(base_dir, "assets", "urtc_tester_banner.png"))
    ICON_IMAGE_PATH = os.path.normpath(os.path.join(base_dir, "assets", "urtc_icon.png"))


def list_serial_ports():
    ports = []
    if HAVE_SERIAL:
        import serial.tools.list_ports
        ports = [p.device for p in serial.tools.list_ports.comports()]
    return ports

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
                if len(self._rx_buf) > 4096:
                    # No real SLCAN line comes anywhere close to this length -
                    # this only grows this large from noise, a wrong
                    # baudrate, or a disconnected adapter feeding bytes with
                    # no \r ever showing up. Discarding turns an unbounded
                    # memory grow into a one-time desync instead.
                    self._rx_buf = b""
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
        try:
            can_id_raw, dlc, data = struct.unpack(self._FRAME_FMT, frame)
        except struct.error:
            # A partial or corrupted read (syscall interruption, the
            # interface being torn down mid-read) that doesn't match the
            # expected frame size - treated the same as "nothing usable
            # arrived this time" rather than letting this propagate and
            # kill CANBusMonitor's background thread, which would leave
            # the whole tool silently blind with no further indication
            # anything went wrong.
            return None
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
# CAN protocol constants - every ID this tool can send or receive, matching
# CANBUS.TXT exactly (kept as one flat block here rather than split per
# tool, since several - like 0x100 and 0x110/0x111 - are global and used
# regardless of which tool profile is active).
# =============================================================================
CAN_ID_IMPACT_EVENT      = 0x095  # Scan probe - max priority, event-driven
CAN_ID_GLOBAL_STATUS     = 0x100  # Status LED + ring LED + OLED night mode - any tool
CAN_ID_QUERY_ACTIVE_TOOL = 0x110  # Query which tool profile is active + quick state
CAN_ID_ACTIVE_TOOL_RESP  = 0x111  # Answers CAN_ID_QUERY_ACTIVE_TOOL
CAN_ID_MOTION_CMD        = 0x120  # Stepper dir+steps - dispensers/screwdriver/grippers
CAN_ID_SOLDER_SETPOINT   = 0x130  # T12 setpoint temperature
CAN_ID_SOLDER_TELEMETRY  = 0x135  # T12 actual temperature + endstop
CAN_ID_DRILL_CMD         = 0x140  # BL4260 speed + direction
CAN_ID_VACUUM_TELEMETRY  = 0x145  # ADC reading + digital detect
CAN_ID_DRILL_TELEMETRY   = 0x147  # Actual RPM + endstop
CAN_ID_AOI_CMD           = 0x150  # Ring mode + strobe period
CAN_ID_AOI_TELEMETRY     = 0x155  # Endstop only
CAN_ID_LASER_CMD         = 0x160  # Power + interlock
CAN_ID_LASER_TELEMETRY   = 0x165  # Endstop only
CAN_ID_3DP_THERMAL_MOTION = 0x170  # Nozzle setpoint + extruder direction/steps
CAN_ID_3DP_LAYER_FAN_CMD  = 0x173  # Layer fan PWM
CAN_ID_3DP_HOTEND_TELEM   = 0x175  # Hotend actual temperature
CAN_ID_3DP_LAYER_FAN_RPM  = 0x177  # Layer fan actual RPM
CAN_ID_3DP_HOTEND_FAN_CMD = 0x178  # Hotend fan PWM
CAN_ID_3DP_HOTEND_FAN_RPM = 0x179  # Hotend fan actual RPM
CAN_ID_EXP_SPI_CMD        = 0x180  # Generic SPI passthrough request, for CONN_EXPANSION
CAN_ID_EXP_SPI_RESP       = 0x181  # Answers CAN_ID_EXP_SPI_CMD
CAN_ID_QUERY_DIAG0        = 0x182  # Query EXP_TMC_DIAG0's current level
CAN_ID_DIAG0_RESP         = 0x183  # Answers CAN_ID_QUERY_DIAG0
CAN_ID_QUERY_FRAM_STATE = 0x190  # Query the FM24CL64B's recovered state
CAN_ID_FRAM_STATE_RESP  = 0x191  # Answers CAN_ID_QUERY_FRAM_STATE, also sent after an erase
CAN_ID_ERASE_FRAM       = 0x192  # Magic-payload erase - see ERASE_FRAM_MAGIC below
ERASE_FRAM_MAGIC = bytes([0xE3, 0xA5, 0xE0, 0xFF])
CAN_ID_QUERY_VERSION     = 0x7F8  # Answered by app or bootloader, whichever's running
CAN_ID_VERSION_RESPONSE  = 0x7F9

TOOL_NAMES = {
    0: "Soldering Iron", 1: "Paste Dispenser", 2: "Liquid Dispenser",
    3: "Screwdriver", 4: "Vacuum Pickup", 5: "Drill",
    6: "Gripper (Gimbal)", 7: "Gripper (NEMA)", 8: "AOI Inspection",
    9: "Laser Engraver", 10: "3D Printer", 11: "Scan Probe",
}
# 5 tool IDs all share the exact same motion command (0x120) - a plain
# stepper with direction + step count, differing only in what's physically
# attached, not in the protocol.
MOTION_TOOL_IDS = {1, 2, 3, 6, 7}


class CANBusMonitor:
    """Owns the one and only background thread reading frames off the
    transport, dispatching each received frame to whichever callbacks are
    currently registered for its CAN ID. This exists because a live
    tester - unlike the flasher's one-request-at-a-time protocol - needs
    to watch several different telemetry IDs at once (temperature, RPM,
    endstops...) without them stepping on each other, and a serial port or
    raw socket can't safely be read from two places at once. Callbacks run
    on this background thread - they must only touch Tkinter state via
    root.after(), never touch widgets directly.
    """
    def __init__(self, transport, log):
        self.transport = transport
        self.log = log
        self._handlers = {}  # can_id -> list of callback(data) functions
        self._sniffers = []  # callback(can_id, data) functions - called for EVERY frame, regardless of ID
        self._running = False
        self._thread = None
        self._lock = threading.Lock()

    def register(self, can_id, callback):
        with self._lock:
            self._handlers.setdefault(can_id, []).append(callback)

    def unregister(self, can_id, callback):
        with self._lock:
            if can_id in self._handlers and callback in self._handlers[can_id]:
                self._handlers[can_id].remove(callback)
                if not self._handlers[can_id]:
                    del self._handlers[can_id]

    def register_sniffer(self, callback):
        """Registers a callback(can_id, data) that fires for every single
        frame this monitor sees, regardless of ID - for the raw bus
        monitor panel. Separate from the per-ID handlers above; neither
        mechanism affects the other."""
        with self._lock:
            self._sniffers.append(callback)

    def unregister_sniffer(self, callback):
        with self._lock:
            if callback in self._sniffers:
                self._sniffers.remove(callback)

    def clear_all(self):
        with self._lock:
            self._handlers = {}

    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=1.0)

    def _loop(self):
        while self._running:
            try:
                frame = self.transport.read_frame(timeout=0.1)
            except Exception as e:
                self.log(f"bus read error: {e}")
                time.sleep(0.2)
                continue
            if frame is None:
                continue
            can_id, data = frame
            with self._lock:
                callbacks = list(self._handlers.get(can_id, []))
                sniffers = list(self._sniffers)
            for callback in callbacks:
                try:
                    callback(data)
                except Exception as e:
                    self.log(f"handler error for 0x{can_id:03X}: {e}")
            for sniffer in sniffers:
                try:
                    sniffer(can_id, data)
                except Exception as e:
                    self.log(f"sniffer callback error: {e}")

    def send(self, can_id, data):
        self.transport.send_frame(can_id, data)

    def wait_for_one(self, can_id, timeout=1.5):
        """One-off synchronous wait for a single frame on can_id, layered
        on top of the same registration mechanism everything else uses -
        never calls read_frame() directly, so this never races against
        the background thread's own reading."""
        result = {"data": None}
        event = threading.Event()

        def _capture(data):
            result["data"] = data
            event.set()

        self.register(can_id, _capture)
        try:
            event.wait(timeout=timeout)
        finally:
            self.unregister(can_id, _capture)
        return result["data"]



class TesterGUI:
    def __init__(self, root):
        self.root = root
        root.title(f"URTC Tester v{TESTER_VERSION}")
        # Geometry (size + centered position) is set once in main(), before
        # this class is constructed - not here, so it isn't overwritten by
        # a second, uncentered geometry() call.
        root.resizable(True, True)

        self._file_logger = None
        try:
            os.makedirs(LOGS_FOLDER, exist_ok=True)
            log_path = os.path.join(LOGS_FOLDER, f"urtc_tester_{time.strftime('%Y%m%d_%H%M%S')}.log")
            self._file_logger = logging.getLogger(f"urtc_tester_{id(self)}")
            self._file_logger.setLevel(logging.INFO)
            handler = logging.FileHandler(log_path, encoding="utf-8")
            handler.setFormatter(logging.Formatter("%(asctime)s %(message)s"))
            self._file_logger.addHandler(handler)
            self._file_logger.info(f"=== URTC Tester v{TESTER_VERSION} session started ===")
        except OSError:
            self._file_logger = None

        try:
            self._icon_img = tk.PhotoImage(file=ICON_IMAGE_PATH)
            root.iconphoto(True, self._icon_img)
        except (tk.TclError, OSError):
            pass

        self.transport = None
        self.bus = None  # CANBusMonitor, created on connect
        self.active_tool_id = None
        self._keepalive_jobs = {}  # name -> root.after() id, for watchdog-guarded commands currently repeating
        self._current_tool_id = None  # which tool's panel is currently showing, if any - see _send_tool_off_command

        pad = {"padx": 8, "pady": 4}
        root.grid_columnconfigure(0, weight=1)
        root.grid_columnconfigure(1, weight=1)
        root.grid_rowconfigure(2, weight=1)  # log row

        left_col = ttk.Frame(root)
        right_col = ttk.Frame(root)
        left_col.grid(row=0, column=0, sticky="nsew", padx=4, pady=4)
        right_col.grid(row=0, column=1, sticky="nsew", padx=4, pady=4)

        # --- Section 1: Connect (unchanged from the flasher) ---
        conn_frame = ttk.LabelFrame(left_col, text="1. Connect to USB-CAN Adapter")
        conn_frame.pack(fill="x", **pad)

        row = 0
        if HAVE_SOCKETCAN:
            ttk.Label(conn_frame, text="Transport:").grid(row=row, column=0, sticky="w", **pad)
            self.transport_var = tk.StringVar(value="serial" if HAVE_SERIAL else "socketcan")
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
        else:
            self.transport_var = tk.StringVar(value="serial")

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

        # --- Detected tool status (replaces the flasher's firmware-version row) ---
        ttk.Label(conn_frame, text="Active tool:").grid(row=row, column=0, sticky="w", **pad)
        self.active_tool_label = ttk.Label(conn_frame, text="(connect to detect)", foreground="gray")
        self.active_tool_label.grid(row=row, column=1, columnspan=3, sticky="w", **pad)
        self.detect_btn = ttk.Button(conn_frame, text="Detect", command=self.detect_active_tool)
        self.detect_btn.grid(row=row, column=4, **pad)
        self.selftest_btn = ttk.Button(conn_frame, text="Run Self-Test...", command=self._run_self_test)
        self.selftest_btn.grid(row=row, column=5, **pad)
        row += 1

        # ID jumper pins (ID4 down to ID0, matching ECOVIA.TXT's documented
        # MSB-to-LSB order) plus the resulting tool number - decoded
        # straight from the same active_tool byte the 0x111 response
        # already carries (it IS the raw 5-bit jumper reading - confirmed
        # against STM32F303CC.C's own Read_ToolID, which builds it bit by
        # bit from ID0..ID4 the same way), so no separate query or
        # firmware change is needed for this display.
        ttk.Label(conn_frame, text="ID pins:").grid(row=row, column=0, sticky="w", **pad)
        id_pins_frame = ttk.Frame(conn_frame)
        id_pins_frame.grid(row=row, column=1, columnspan=3, sticky="w", **pad)
        self._id_pin_squares = []
        for bit_label in ("ID4", "ID3", "ID2", "ID1", "ID0"):
            cell = ttk.Frame(id_pins_frame)
            cell.pack(side="left", padx=(0, 10))
            ttk.Label(cell, text=bit_label, font=("", 8)).pack()
            sq = tk.Label(cell, text="?", width=2, height=1, relief="solid", borderwidth=1,
                          font=("", 11, "bold"), bg="#DDDDDD", fg="#666666")
            sq.pack()
            self._id_pin_squares.append(sq)
        ttk.Label(conn_frame, text="Tool #:").grid(row=row, column=4, sticky="w", padx=(0, 2))
        self.tool_number_label = ttk.Label(conn_frame, text="--", font=("", 13, "bold"))
        self.tool_number_label.grid(row=row, column=5, sticky="w", padx=(0, 8))
        row += 1

        ttk.Label(conn_frame, text="Board state:").grid(row=row, column=0, sticky="w", **pad)
        self.board_state_label = ttk.Label(conn_frame, text="(connect to check)", foreground="gray")
        self.board_state_label.grid(row=row, column=1, columnspan=4, sticky="w", **pad)

        # --- Global controls (0x100) - status LED, ring LED, OLED mode.
        # Applies regardless of which tool is active, so this frame is
        # built once and never torn down/rebuilt the way the per-tool
        # frame below is. ---
        global_frame = ttk.LabelFrame(left_col, text="2. Global Controls (status LED, ring, OLED)")
        global_frame.pack(fill="x", **pad)
        self._build_global_panel(global_frame)

        # --- Expansion board (CONN_EXPANSION) - also global, not tied to
        # any specific active_tool, so built once here the same as the
        # panel above. ---
        exp_frame = ttk.LabelFrame(left_col, text="3. Expansion Board (SPI)")
        exp_frame.pack(fill="x", **pad)
        self._build_expansion_panel(exp_frame)

        # --- Persistence F-RAM - a core board component (shares I2C1 with
        # the OLED), not part of the expansion connector at all, so this
        # gets its own section rather than living inside "Expansion Board"
        # the way an earlier version of this tool had it. ---
        fram_frame = ttk.LabelFrame(left_col, text="4. Persistence F-RAM")
        fram_frame.pack(fill="x", **pad)
        self._build_fram_panel(fram_frame)

        # --- Custom/arbitrary CAN frame injector - also global. Useful for
        # exercising a command that doesn't have its own dedicated panel
        # control yet, testing something not (or not yet) documented in
        # CANBUS.TXT, or just sending a raw frame to see what happens. ---
        custom_frame = ttk.LabelFrame(left_col, text="6. Custom CAN Frame")
        custom_frame.pack(fill="x", **pad)
        self._build_custom_frame_panel(custom_frame)

        # --- Dynamic per-tool frame - populated by rebuild_tool_panel()
        # once the active tool is known. Its own inner frame is destroyed
        # and rebuilt from scratch on every detect, rather than trying to
        # selectively show/hide 12 pre-built panels at once - simpler, and
        # matches the "only show what's actually relevant right now" goal
        # directly instead of hiding the other 11 behind the scenes. ---
        self.tool_frame = ttk.LabelFrame(right_col, text="5. Tool Controls")
        self.tool_frame.pack(fill="both", expand=True, **pad)
        self.tool_panel_inner = ttk.Frame(self.tool_frame)
        self.tool_panel_inner.pack(fill="both", expand=True)
        ttk.Label(
            self.tool_panel_inner,
            text="Connect and detect the active tool to see its controls here.",
            foreground="gray", wraplength=380, justify="left",
        ).pack(padx=8, pady=8, anchor="w")

        self.progress = ttk.Progressbar(root, orient="horizontal", mode="determinate", maximum=100)
        self.progress.grid(row=1, column=0, columnspan=2, sticky="ew", padx=8, pady=(4, 8))

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
        self.log(f"URTC Tester ready. Expected HardwareID: 0x{THIS_HARDWARE_ID:08X}")
        if self.transport_var.get() == "serial":
            self.log("Expecting the adapter in SLCAN mode - see the README if your "
                      "CANable shows up as anything other than a serial port.")
        else:
            self.log("SocketCAN mode - expecting the interface already up at "
                      "500 kbit/s (ip link set ... up type can bitrate 500000). "
                      "See the README's Linux section.")
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def log(self, msg):
        if self._file_logger:
            try:
                self._file_logger.info(msg)
            except OSError:
                pass
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
            ports = list_serial_ports()
        else:
            ports = list_socketcan_interfaces()
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
                self.bus = CANBusMonitor(self.transport, self.log)
                self.bus.start()
                self.conn_status.config(text=f"Connected ({port})", foreground="green")
                self.connect_btn.config(text="Disconnect")
                self.log(f"Connected to {port}, CAN channel open"
                          + (f" at {self.bitrate_var.get()}." if is_serial else "."))
                self.detect_active_tool()
            except Exception as e:
                self.transport = None
                msg = str(e)
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
            self._clear_tool_panel()
            if self.bus is not None:
                self.bus.stop()
                self.bus = None
            try:
                self.transport.close()
            except Exception:
                pass
            self.transport = None
            self.active_tool_id = None
            self.conn_status.config(text="Not connected", foreground="red")
            self.connect_btn.config(text="Connect")
            self.active_tool_label.config(text="(connect to detect)", foreground="gray")
            self.board_state_label.config(text="(connect to check)", foreground="gray")
            self.log("Disconnected.")

    def auto_detect_bitrate(self):
        if self.transport is not None:
            messagebox.showerror("Already connected", "Disconnect first, then auto-detect before reconnecting.")
            return
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Nothing selected", "Select a COM port first.")
            return
        self.bitrate_status.config(text="Trying each bitrate...", foreground="gray")
        self.connect_btn.config(state="disabled")
        self.port_combo.config(state="disabled")
        self.autobaud_btn.config(state="disabled")
        threading.Thread(target=self._auto_detect_worker, args=(port,), daemon=True).start()

    def _auto_detect_worker(self, port):
        found_label = None
        try:
            try_order = ["500 kbit/s", "250 kbit/s", "125 kbit/s", "1 Mbit/s",
                         "100 kbit/s", "50 kbit/s", "20 kbit/s", "10 kbit/s", "800 kbit/s"]
            for label in try_order:
                code = next(c for l, c in SLCAN_BITRATES if l == label)
                self.log(f"Auto-detect: trying {label}...")
                trial = None
                try:
                    trial = SLCAN(port, log=lambda m: None)
                    trial.open_channel(code)
                    trial.send_frame(CAN_ID_QUERY_ACTIVE_TOOL, b"")
                    deadline = time.time() + 0.8
                    while time.time() < deadline:
                        frame = trial.read_frame(timeout=0.2)
                        if frame is not None and frame[0] == CAN_ID_ACTIVE_TOOL_RESP:
                            found_label = label
                            break
                    if found_label:
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

    def _auto_detect_done(self, found_label):
        self.connect_btn.config(state="normal")
        self.port_combo.config(state="readonly")
        self.autobaud_btn.config(state="normal")
        if found_label:
            self.bitrate_var.set(found_label)
            self.bitrate_status.config(text=f"Found: {found_label}", foreground="green")
            self.log(f"Auto-detect: board responded at {found_label}.")
        else:
            self.bitrate_status.config(text="No response at any standard bitrate", foreground="red")
            self.log("Auto-detect: no response at any standard bitrate - check wiring/power, "
                      "or the board may be using a non-standard bitrate this can't try.")

    def detect_active_tool(self):
        if self.transport is None or self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        self.active_tool_label.config(text="Detecting...", foreground="gray")
        self.board_state_label.config(text="Detecting...", foreground="gray")
        self.progress["value"] = 0
        threading.Thread(target=self._detect_active_tool_worker, daemon=True).start()

    def _detect_active_tool_worker(self):
        bus = self.bus  # local reference - self.bus can be reassigned to
        # None by toggle_connect() running on the main thread while this
        # worker is still blocked inside wait_for_one below; using this
        # local copy for the rest of the function means that reassignment
        # can't turn a subsequent bus.send/wait_for_one call here into an
        # AttributeError on a None.
        if bus is None:
            return
        self.log("Querying active tool (0x110)...")
        bus.send(CAN_ID_QUERY_ACTIVE_TOOL, b"")
        self.root.after(0, lambda: self.progress.configure(value=35))
        tool_data = bus.wait_for_one(CAN_ID_ACTIVE_TOOL_RESP, timeout=1.5)

        self.log("Querying version (0x7F8)...")
        bus.send(CAN_ID_QUERY_VERSION, b"\x00")
        self.root.after(0, lambda: self.progress.configure(value=70))
        version_data = bus.wait_for_one(CAN_ID_VERSION_RESPONSE, timeout=1.5)
        self.root.after(0, lambda: self.progress.configure(value=100))

        if self.bus is not bus:
            # Disconnected (or disconnected and reconnected to something
            # new) while this was waiting - this result is stale, and the
            # widgets it would update may now belong to a different
            # connection, or no connection at all. Silently dropped rather
            # than risk showing a detection result for a board that isn't
            # even the one currently connected.
            return
        self.root.after(0, lambda: self._show_detect_result(tool_data, version_data))

    def _update_id_pin_display(self, tool_id):
        # bit0=ID0 .. bit4=ID4, matching ECOVIA.TXT's table and
        # STM32F303CC.C's Read_ToolID exactly - the squares are drawn
        # ID4..ID0 (left to right, matching the documented MSB-to-LSB
        # convention), so index 0 in self._id_pin_squares is ID4, needing
        # bit 4, down to index 4 being ID0, needing bit 0.
        for i, square in enumerate(self._id_pin_squares):
            bit_index = 4 - i
            bit_value = (tool_id >> bit_index) & 1
            if bit_value:
                square.config(text="1", bg="#2E7D32", fg="white")  # jumper installed
            else:
                square.config(text="0", bg="#EEEEEE", fg="#666666")  # no jumper
        self.tool_number_label.config(text=str(tool_id))

    def _show_detect_result(self, tool_data, version_data):
        if tool_data is None and version_data is None:
            self.active_tool_label.config(text="No response - check wiring/power/bitrate", foreground="red")
            self.board_state_label.config(text="(no response)", foreground="red")
            self.log("Detection failed: no response to either query.")
            return

        if tool_data is not None and len(tool_data) >= 4:
            tool_id = tool_data[0]
            err = tool_data[1]
            can_err = tool_data[2]
            booting = tool_data[3]
            name = TOOL_NAMES.get(tool_id, f"none assigned (raw ID {tool_id})")
            self.active_tool_label.config(text=f"{name} (ID {tool_id})",
                                           foreground="green" if tool_id in TOOL_NAMES else "orange")
            self._update_id_pin_display(tool_id)
            state_bits = []
            if err:
                state_bits.append("CRITICAL ERROR declared")
            if can_err:
                state_bits.append("CAN bus error seen")
            if booting:
                state_bits.append("still in boot splash")
            state_text = ", ".join(state_bits) if state_bits else "normal"
            self.board_state_label.config(
                text=state_text, foreground="red" if (err or can_err) else "green"
            )
            self.log(f"Active tool: {name} (ID {tool_id}). State: {state_text}.")
            if tool_id != self.active_tool_id:
                self.active_tool_id = tool_id
                self.rebuild_tool_panel(tool_id)
        else:
            self.active_tool_label.config(text="No response to 0x110 - is the firmware "
                                                "on this board recent enough to support it?",
                                           foreground="orange")
            self.log("No response to the active-tool query (0x110/0x111) - this needs "
                      "STM32F303CC.C built with that support; older firmware won't answer it.")

        if version_data is not None and len(version_data) >= 8:
            role = "bootloader" if version_data[0] == 0x01 else "application"
            hw_id = struct.unpack(">I", version_data[1:5])[0]
            ver_major = struct.unpack(">H", version_data[5:7])[0]
            ver_minor = version_data[7]
            hw_note = "" if hw_id == THIS_HARDWARE_ID else f" (expected 0x{THIS_HARDWARE_ID:08X} - different board/rig?)"
            self.log(f"Version: {role}, HardwareID 0x{hw_id:08X}{hw_note}, firmware v{ver_major}.{ver_minor}")

    def _send_tool_off_command(self):
        """Sends an explicit, immediate safe/off command for whichever
        tool's panel is currently showing, if any - called before tearing
        down its keepalive timers. The firmware's own communication
        watchdog (250ms for the soldering iron/laser/3D nozzle, 1000ms for
        the layer fan) would shut the same actuator off on its own once
        this tool stops resending anyway, so this isn't covering a real
        safety gap - it just means the actuator stops within one CAN frame
        of switching away, instead of coasting for however long that
        watchdog takes to notice the silence."""
        if self.bus is None or self._current_tool_id is None:
            return
        off_commands = {
            0: lambda: self.bus.send(CAN_ID_SOLDER_SETPOINT, struct.pack(">H", 0)),
            5: lambda: self.bus.send(CAN_ID_DRILL_CMD, bytes([0, 0])),
            9: lambda: self.bus.send(CAN_ID_LASER_CMD, bytes([0, 0])),
            10: lambda: (
                self.bus.send(CAN_ID_3DP_THERMAL_MOTION, struct.pack(">H", 0) + bytes([0, 0, 0, 0])),
                self.bus.send(CAN_ID_3DP_LAYER_FAN_CMD, bytes([0])),
                self.bus.send(CAN_ID_3DP_HOTEND_FAN_CMD, bytes([0])),
            ),
        }.get(self._current_tool_id)
        if off_commands is not None:
            try:
                off_commands()
            except Exception as e:
                self.log(f"Couldn't send off-command while switching away from tool "
                         f"{self._current_tool_id}: {e}")

    def _clear_tool_panel(self):
        self._send_tool_off_command()
        if self.bus is not None:
            self.bus.clear_all()  # drops every per-tool telemetry handler, not the connection itself
        for child in self.tool_panel_inner.winfo_children():
            child.destroy()
        for job_id in self._keepalive_jobs.values():
            try:
                self.root.after_cancel(job_id)
            except Exception:
                pass
        self._keepalive_jobs = {}

    def rebuild_tool_panel(self, tool_id):
        self._clear_tool_panel()
        self._current_tool_id = tool_id
        name = TOOL_NAMES.get(tool_id, "No tool assigned")
        self.tool_frame.config(text=f"5. Tool Controls - {name}")
        builder = {
            0: self._build_soldering_iron_panel,
            1: self._build_motion_panel, 2: self._build_motion_panel,
            3: self._build_motion_panel, 6: self._build_motion_panel,
            7: self._build_motion_panel,
            4: self._build_vacuum_panel,
            5: self._build_drill_panel,
            8: self._build_aoi_panel,
            9: self._build_laser_panel,
            10: self._build_3dprinter_panel,
            11: self._build_scan_probe_panel,
        }.get(tool_id)
        if builder is None:
            ttk.Label(
                self.tool_panel_inner,
                text=f"No tool is assigned to this jumper setting (raw ID {tool_id}). "
                     f"Nothing to test here - set the board's ID jumpers to a real tool profile.",
                foreground="gray", wraplength=380, justify="left",
            ).pack(padx=8, pady=8, anchor="w")
            return
        if tool_id in MOTION_TOOL_IDS:
            builder(self.tool_panel_inner, tool_id, name)
        else:
            builder(self.tool_panel_inner)

    def _build_global_panel(self, parent):
        # 0x100 - Status LED (override) + ring LED + OLED mode, all sent
        # together in one 8-byte frame regardless of which tool is active.
        # The override holds 10s from the last frame received before
        # falling back to automatic - low-stakes if it lapses (nothing
        # unsafe happens, it just reverts to the automatic color scheme),
        # so unlike the watchdog-guarded tool commands below, this is a
        # plain "Send" button rather than an auto-repeating keepalive.
        self.status_r = tk.IntVar(value=0)
        self.status_g = tk.IntVar(value=255)
        self.status_b = tk.IntVar(value=0)
        self.ring_r = tk.IntVar(value=0)
        self.ring_g = tk.IntVar(value=0)
        self.ring_b = tk.IntVar(value=255)
        self.ring_on = tk.BooleanVar(value=False)
        self.night_mode_var = tk.StringVar(value="Standard")

        ttk.Label(parent, text="Status LED override (R/G/B):").grid(row=0, column=0, sticky="w", padx=4, pady=2)
        rgb_row = ttk.Frame(parent)
        rgb_row.grid(row=0, column=1, columnspan=3, sticky="w")
        for var in (self.status_r, self.status_g, self.status_b):
            ttk.Spinbox(rgb_row, from_=0, to=255, textvariable=var, width=5).pack(side="left", padx=2)

        ttk.Label(parent, text="Ring LED (R/G/B):").grid(row=1, column=0, sticky="w", padx=4, pady=2)
        ring_row = ttk.Frame(parent)
        ring_row.grid(row=1, column=1, columnspan=3, sticky="w")
        for var in (self.ring_r, self.ring_g, self.ring_b):
            ttk.Spinbox(ring_row, from_=0, to=255, textvariable=var, width=5).pack(side="left", padx=2)
        ttk.Checkbutton(ring_row, text="Ring on", variable=self.ring_on).pack(side="left", padx=(12, 2))

        ttk.Label(parent, text="OLED mode:").grid(row=2, column=0, sticky="w", padx=4, pady=2)
        ttk.Combobox(
            parent, textvariable=self.night_mode_var, width=12, state="readonly",
            values=["Standard", "Night", "Standby"],
        ).grid(row=2, column=1, sticky="w", padx=4, pady=2)
        ttk.Button(parent, text="Send", command=self._send_global_status).grid(row=2, column=2, padx=4, pady=2)
        ttk.Label(
            parent,
            text="In AOI Inspection mode, ring on/off here is ignored - use that tool's own "
                 "ring mode control instead (0x150 covers on/off/strobe there). Color still applies.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=4, sticky="w", padx=4, pady=(0, 4))

    def _send_global_status(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        night_code = {"Standard": 0x00, "Night": 0x01, "Standby": 0x0F}[self.night_mode_var.get()]
        data = bytes([
            max(0, min(255, self.status_r.get())),
            max(0, min(255, self.status_g.get())),
            max(0, min(255, self.status_b.get())),
            night_code,
            max(0, min(255, self.ring_r.get())),
            max(0, min(255, self.ring_g.get())),
            max(0, min(255, self.ring_b.get())),
            0x01 if self.ring_on.get() else 0x00,
        ])
        self.bus.send(CAN_ID_GLOBAL_STATUS, data)
        self.log(f"Sent 0x100: status RGB=({data[0]},{data[1]},{data[2]}), night={self.night_mode_var.get()}, "
                  f"ring RGB=({data[4]},{data[5]},{data[6]}), ring_on={self.ring_on.get()}")

    def _build_expansion_panel(self, parent):
        # CONN_EXPANSION's bit-banged SPI bus (0x180/0x181) - a generic
        # byte passthrough, not TMC5160-register-aware, matching the
        # firmware's own approach (see CANBUS.TXT): this tool doesn't
        # need to know that chip's specific protocol either, just send
        # and show raw bytes.
        self.spi_send_var = tk.StringVar(value="01 02 03 04")
        ttk.Label(parent, text="SPI bytes to send (hex, space-separated):").grid(
            row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 0))
        ttk.Entry(parent, textvariable=self.spi_send_var, width=30).grid(
            row=1, column=0, sticky="w", padx=4, pady=2)
        ttk.Button(parent, text="Send", command=self._send_expansion_spi).grid(row=1, column=1, padx=4)
        self.spi_response_var = tk.StringVar(value="(nothing sent yet)")
        ttk.Label(parent, text="Response:").grid(row=2, column=0, sticky="w", padx=4, pady=(4, 0))
        ttk.Label(parent, textvariable=self.spi_response_var, font=("Courier", 9)).grid(
            row=3, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Label(
            parent,
            text="Up to 7 bytes per transfer (CS held low for the whole thing) - matches "
                 "what a single 0x180 frame can carry (1 length byte + up to 7 data bytes).",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=4, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=5, column=0, columnspan=2, sticky="ew", pady=4)

        # EXP_TMC_DIAG0 level (0x182/0x183) - a TMC5160's stall/fault
        # diagnostic line. Simple polled read, not a live/pushed value.
        diag_row = ttk.Frame(parent)
        diag_row.grid(row=6, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 2))
        ttk.Button(diag_row, text="Query DIAG0", command=self._query_diag0).pack(side="left")
        self.diag0_var = tk.StringVar(value="(not queried yet)")
        ttk.Label(diag_row, textvariable=self.diag0_var, font=("Courier", 9)).pack(side="left", padx=(8, 0))

    def _build_fram_panel(self, parent):
        # F-RAM recovered-state query/erase (0x190/0x191/0x192). Deliberately
        # NOT part of the expansion panel above, even though an earlier
        # version of this tool bundled them together: the FM24CL64B shares
        # I2C1 with the OLED (a core board component), it has nothing to do
        # with CONN_EXPANSION's own I2C2/SPI bus at all - the expansion
        # connector itself has no F-RAM, no EEPROM, nothing non-volatile on
        # it, and grouping them together implied a connection that isn't
        # real.
        ttk.Label(parent, text="Persistence F-RAM (FM24CL64B, shares I2C1 with the OLED):").grid(
            row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 2))
        btn_row = ttk.Frame(parent)
        btn_row.grid(row=1, column=0, columnspan=2, sticky="w", padx=4)
        ttk.Button(btn_row, text="Query State", command=self._query_fram_state).pack(side="left")
        ttk.Button(btn_row, text="Erase F-RAM...", command=self._erase_fram).pack(side="left", padx=(8, 0))
        self.fram_state_var = tk.StringVar(value="(not queried yet)")
        ttk.Label(parent, textvariable=self.fram_state_var, wraplength=380, justify="left").grid(
            row=2, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 4))

    # Tool-specific self-test steps: (description, cmd_id_or_None, cmd_data,
    # expect_id_or_None). Every command here is a safe, at-rest value
    # (0 setpoint, 0 speed, 0 power) - this verifies the communication
    # round-trip works, not that an actuator physically responds, since
    # confirming the latter needs a human watching anyway. Tools with no
    # telemetry (plain motion) or that are purely event-driven (scan
    # probe) get an info-only entry rather than a real pass/fail, since
    # there's nothing to safely verify without a physical trigger.
    _SELF_TEST_STEPS = {
        0: [("Soldering iron: safe setpoint (0°C) elicits telemetry",
             CAN_ID_SOLDER_SETPOINT, struct.pack(">H", 0), CAN_ID_SOLDER_TELEMETRY)],
        5: [("Drill: safe speed (0) elicits telemetry",
             CAN_ID_DRILL_CMD, bytes([0, 0]), CAN_ID_DRILL_TELEMETRY)],
        9: [("Laser: safe power/interlock (0/0) elicits endstop telemetry",
             CAN_ID_LASER_CMD, bytes([0, 0]), CAN_ID_LASER_TELEMETRY)],
        10: [("3D printer: safe nozzle setpoint (0°C) elicits hotend telemetry",
              CAN_ID_3DP_THERMAL_MOTION, struct.pack(">H", 0) + bytes([0, 0, 0, 0]), CAN_ID_3DP_HOTEND_TELEM)],
        8: [("AOI: endstop telemetry arrives (no command needed - continuous)", None, None, CAN_ID_AOI_TELEMETRY)],
        4: [("Vacuum: ADC telemetry arrives (no command needed - continuous)", None, None, CAN_ID_VACUUM_TELEMETRY)],
        11: [("Scan probe is event-driven (only sends on physical impact) - "
              "cannot verify without triggering it by hand", None, None, None)],
    }
    _MOTION_TOOL_IDS = (1, 2, 3, 6, 7)

    def _run_self_test(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        if self.active_tool_id is None:
            messagebox.showerror("Not detected", "Run Detect first, so this knows which tool to test.")
            return
        steps = self._SELF_TEST_STEPS.get(self.active_tool_id)
        is_motion = self.active_tool_id in self._MOTION_TOOL_IDS
        if not messagebox.askyesno(
            "Run self-test?",
            "This sends a small number of safe, at-rest commands (setpoint/speed/power "
            "all 0) to verify the board responds as expected for its detected tool - "
            "nothing here heats, fires, or spins anything at meaningful power. Continue?",
        ):
            return

        win = tk.Toplevel(self.root)
        win.title(f"Self-Test - {TOOL_NAMES.get(self.active_tool_id, '?')}")
        win.geometry("480x260")
        text = tk.Text(win, state="disabled", wrap="word")
        text.pack(fill="both", expand=True, padx=8, pady=8)

        def _append(line):
            def _do():
                if not text.winfo_exists():
                    return
                text.config(state="normal")
                text.insert("end", line + "\n")
                text.config(state="disabled")
                text.see("end")
            self.root.after(0, _do)

        def _worker():
            _append(f"Testing: {TOOL_NAMES.get(self.active_tool_id, '?')}\n")
            # Generic checks first - meaningful for every tool.
            self.bus.send(CAN_ID_QUERY_ACTIVE_TOOL, b"")
            resp = self.bus.wait_for_one(CAN_ID_ACTIVE_TOOL_RESP, timeout=1.5)
            ok = resp is not None and len(resp) >= 1 and resp[0] == self.active_tool_id
            _append(f"[{'PASS' if ok else 'FAIL'}] Active-tool query (0x110) confirms detected tool")

            self.bus.send(CAN_ID_QUERY_VERSION, b"\x00")
            resp = self.bus.wait_for_one(CAN_ID_VERSION_RESPONSE, timeout=1.5)
            _append(f"[{'PASS' if resp is not None else 'FAIL'}] Version query (0x7F8) gets a response")

            if is_motion:
                _append("[INFO] This tool has no telemetry to verify - STEP/DIR/EN "
                        "commands are one-shot with no response by design.")
            elif steps:
                for description, cmd_id, cmd_data, expect_id in steps:
                    if expect_id is None:
                        _append(f"[INFO] {description}")
                        continue
                    if cmd_id is not None:
                        self.bus.send(cmd_id, cmd_data)
                    resp = self.bus.wait_for_one(expect_id, timeout=1.5)
                    _append(f"[{'PASS' if resp is not None else 'FAIL'}] {description}")
            else:
                _append("[INFO] No self-test steps defined for this tool yet.")
            _append("\nDone.")

        threading.Thread(target=_worker, daemon=True).start()

    def _open_bus_monitor(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        if getattr(self, "_bus_monitor_win", None) is not None and self._bus_monitor_win.winfo_exists():
            self._bus_monitor_win.lift()  # already open - just bring it to the front rather than opening a second one
            return

        win = tk.Toplevel(self.root)
        win.title("Raw Bus Monitor")
        win.geometry("640x460")
        self._bus_monitor_win = win
        self._bus_monitor_paused = False
        self._bus_monitor_last_tick = None
        self._bus_monitor_row_count = 0
        self._bus_monitor_window_frames = 0
        self._bus_monitor_window_bits = 0

        top_row = ttk.Frame(win)
        top_row.pack(fill="x", padx=6, pady=6)
        pause_var = tk.BooleanVar(value=False)

        def _toggle_pause():
            self._bus_monitor_paused = pause_var.get()

        ttk.Checkbutton(top_row, text="Pause", variable=pause_var, command=_toggle_pause).pack(side="left")

        def _clear():
            tree.delete(*tree.get_children())
            self._bus_monitor_row_count = 0
            self._bus_monitor_last_tick = None

        ttk.Button(top_row, text="Clear", command=_clear).pack(side="left", padx=(8, 0))

        def _export(fmt):
            rows = [tree.item(item)["values"] for item in tree.get_children()]
            if not rows:
                messagebox.showinfo("Nothing to export", "The table is empty.")
                return
            ext = ".trc" if fmt == "trc" else ".asc"
            path = filedialog.asksaveasfilename(defaultextension=ext, filetypes=[(f"{fmt.upper()} files", f"*{ext}")])
            if not path:
                return
            try:
                with open(path, "w") as f:
                    if fmt == "trc":
                        # Simplified PEAK PCAN-View trace format - the key
                        # structural elements (header, one line per frame
                        # with index/offset/type/ID/DLC/data), not
                        # guaranteed byte-identical to what PCAN-View's own
                        # exporter produces for every possible viewer.
                        f.write(";$FILEVERSION=1.1\n;$STARTTIME=0\n;$COLUMNS=N,O,T,I,d,l,D\n;\n")
                        f.write(";   Message Number\n;   Time Offset (ms)\n;   Type\n"
                                ";   ID (hex)\n;   Data Length Code\n;   Data Bytes (hex)\n")
                        f.write(";" + "-" * 80 + "\n")
                        for i, (ts, can_id, dlc, data, dt) in enumerate(rows, start=1):
                            id_hex = str(can_id).replace("0x", "").zfill(4)
                            data_hex = str(data).replace(" ", " ") if data != "(empty)" else ""
                            f.write(f"{i:>6}) {i * 10:>10.1f}  Rx    {id_hex}  {dlc}   {data_hex}\n")
                    else:
                        # Simplified Vector ASCII (.asc) trace format -
                        # same caveat as .trc above: the recognizable shape
                        # of the format, not a byte-perfect match to every
                        # CANalyzer/CANoe version's own output.
                        f.write("date " + time.strftime("%a %b %d %H:%M:%S.000 %Y") + "\n")
                        f.write("base hex  timestamps absolute\n")
                        f.write("internal events logged\n")
                        f.write("Begin Triggerblock " + time.strftime("%a %b %d %H:%M:%S.000 %Y") + "\n")
                        for i, (ts, can_id, dlc, data, dt) in enumerate(rows):
                            id_hex = str(can_id).replace("0x", "")
                            data_hex = str(data) if data != "(empty)" else ""
                            f.write(f"   {i * 0.01:.6f} 1  {id_hex:<15} Rx   d {dlc} {data_hex}\n")
                        f.write("End TriggerBlock\n")
                self.log(f"Exported {len(rows)} frames to {path}")
            except OSError as e:
                messagebox.showerror("Export failed", str(e))

        ttk.Button(top_row, text="Export .trc...", command=lambda: _export("trc")).pack(side="left", padx=(8, 0))
        ttk.Button(top_row, text="Export .asc...", command=lambda: _export("asc")).pack(side="left", padx=(4, 0))
        ttk.Label(
            top_row, text="Shows every frame seen, any ID - independent of the active tool panel.",
            foreground="gray",
        ).pack(side="left", padx=(12, 0))

        columns = ("time", "id", "dlc", "data", "dt")
        tree = ttk.Treeview(win, columns=columns, show="headings", height=18)
        for col, label, width in (
            ("time", "Time", 90), ("id", "ID", 160), ("dlc", "DLC", 40),
            ("data", "Data (hex)", 220), ("dt", "Δt (ms)", 70),
        ):
            tree.heading(col, text=label)
            tree.column(col, width=width, anchor="w")
        scrollbar = ttk.Scrollbar(win, orient="vertical", command=tree.yview)
        tree.configure(yscrollcommand=scrollbar.set)
        tree.pack(side="left", fill="both", expand=True, padx=(6, 0), pady=(0, 6))
        scrollbar.pack(side="right", fill="y", pady=(0, 6))

        def _on_frame(can_id, data):
            if self._bus_monitor_paused:
                return
            now = time.time()
            dt_ms = "" if self._bus_monitor_last_tick is None else f"{(now - self._bus_monitor_last_tick) * 1000:.0f}"
            self._bus_monitor_last_tick = now
            ts = time.strftime("%H:%M:%S", time.localtime(now)) + f".{int(now * 1000) % 1000:03d}"

            # Approximate bit count for this frame - standard 11-bit-ID CAN
            # framing overhead (SOF, ID, control, CRC, ACK, EOF, IFS) is
            # about 47 bits before bit-stuffing, plus 8 bits per data byte.
            # Bit-stuffing itself isn't modeled (it depends on the actual
            # bit pattern, not just the frame length), so this is a
            # deliberately approximate, likely-slight-underestimate - a
            # diagnostic aid for spotting gross bus loading, not a
            # certified measurement.
            self._bus_monitor_window_bits += 47 + 8 * len(data)
            self._bus_monitor_window_frames += 1

            def _append():
                if not tree.winfo_exists():
                    return
                id_display = f"0x{can_id:03X}"
                if can_id in CUSTOM_ID_NAMES:
                    id_display += f" ({CUSTOM_ID_NAMES[can_id]})"
                tree.insert("", "end", values=(ts, id_display, len(data), data.hex(" "), dt_ms))
                self._bus_monitor_row_count += 1
                if self._bus_monitor_row_count > 500:
                    # Bounded, same reasoning as every other unbounded-growth
                    # fix elsewhere in this project - a long session
                    # shouldn't grow this window's memory/widget count
                    # without limit. Oldest rows drop first.
                    oldest = tree.get_children()[0]
                    tree.delete(oldest)
                    self._bus_monitor_row_count -= 1
                tree.yview_moveto(1.0)  # auto-scroll to the newest row

            self.root.after(0, _append)

        self.bus.register_sniffer(_on_frame)

        stats_var = tk.StringVar(value="Frames/s: -- | Bus load: -- % (approx, 500 kbit/s)")
        ttk.Label(win, textvariable=stats_var, foreground="gray").pack(
            side="bottom", fill="x", padx=6, pady=(0, 6))

        def _update_stats():
            if not win.winfo_exists():
                return
            frames = self._bus_monitor_window_frames
            bits = self._bus_monitor_window_bits
            self._bus_monitor_window_frames = 0
            self._bus_monitor_window_bits = 0
            load_pct = min(100.0, bits / 500000 * 100)
            stats_var.set(f"Frames/s: {frames} | Bus load: {load_pct:.1f}% (approx, 500 kbit/s)")
            win.after(1000, _update_stats)

        win.after(1000, _update_stats)

        def _on_close():
            self.bus.unregister_sniffer(_on_frame) if self.bus is not None else None
            self._bus_monitor_win = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", _on_close)

    def _query_diag0(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        self.bus.send(CAN_ID_QUERY_DIAG0, b"")
        response = self.bus.wait_for_one(CAN_ID_DIAG0_RESP, timeout=1.0)
        if response is None or len(response) < 1:
            self.diag0_var.set("no response")
            self.log("Queried 0x182 - no 0x183 response.")
            return
        level = "HIGH (inactive/pulled up)" if response[0] else "LOW (asserted)"
        self.diag0_var.set(level)
        self.log(f"EXP_TMC_DIAG0 level: {level}")

    def _build_custom_frame_panel(self, parent):
        ttk.Label(parent, text="CAN ID (hex):").grid(row=0, column=0, sticky="w", padx=4, pady=(4, 0))
        self.custom_id_var = tk.StringVar(value="100")
        ttk.Entry(parent, textvariable=self.custom_id_var, width=8).grid(row=0, column=1, sticky="w", padx=4)
        ttk.Label(parent, text="Data bytes (hex, space-separated):").grid(
            row=1, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 0))
        self.custom_data_var = tk.StringVar(value="")
        ttk.Entry(parent, textvariable=self.custom_data_var, width=30).grid(
            row=2, column=0, columnspan=2, sticky="w", padx=4)

        btn_row = ttk.Frame(parent)
        btn_row.grid(row=3, column=0, columnspan=2, sticky="w", padx=4, pady=4)
        ttk.Button(btn_row, text="Send Once", command=self._send_custom_frame_once).pack(side="left")

        self.custom_periodic_var = tk.BooleanVar(value=False)
        self.custom_interval_var = tk.IntVar(value=100)
        ttk.Checkbutton(
            btn_row, text="Repeat every", variable=self.custom_periodic_var,
            command=self._toggle_custom_periodic,
        ).pack(side="left", padx=(12, 4))
        ttk.Spinbox(btn_row, from_=10, to=10000, textvariable=self.custom_interval_var, width=6).pack(side="left")
        ttk.Label(btn_row, text="ms").pack(side="left", padx=(2, 0))

        ttk.Label(
            parent,
            text="Sends a raw frame - useful for a command that doesn't have its own "
                 "control here yet, or for testing something not (or not yet) "
                 "documented in CANBUS.TXT. No validation beyond ID range and DLC≤8 - "
                 "whatever this sends is exactly what goes on the bus.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=4, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 4))

        ttk.Separator(parent, orient="horizontal").grid(row=5, column=0, columnspan=2, sticky="ew", pady=4)
        ttk.Button(parent, text="Open Raw Bus Monitor...", command=self._open_bus_monitor).grid(
            row=6, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 4))

    def _parse_custom_frame(self):
        """Returns (can_id, data_bytes) or raises ValueError with a message
        suitable for showing the user directly."""
        try:
            can_id = int(self.custom_id_var.get(), 16)
        except (ValueError, tk.TclError):
            raise ValueError(f"'{self.custom_id_var.get()}' isn't a valid hex CAN ID.")
        if not (0 <= can_id <= 0x7FF):
            raise ValueError(f"CAN ID 0x{can_id:X} is outside the standard 11-bit range (0-0x7FF).")
        text = self.custom_data_var.get().strip()
        try:
            data = bytes(int(b, 16) for b in text.split()) if text else b""
        except ValueError:
            raise ValueError(f"'{text}' isn't valid space-separated hex bytes, e.g. 01 02 03.")
        if len(data) > 8:
            raise ValueError(f"{len(data)} bytes given - a CAN frame carries at most 8.")
        return can_id, data

    def _send_custom_frame_once(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        try:
            can_id, data = self._parse_custom_frame()
        except ValueError as e:
            messagebox.showerror("Bad input", str(e))
            return
        self.bus.send(can_id, data)
        self.log(f"Sent custom frame: ID=0x{can_id:03X}, data={data.hex(' ') if data else '(empty)'}")

    def _toggle_custom_periodic(self):
        if not self.custom_periodic_var.get():
            self._stop_keepalive("custom_frame")
            return
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            self.custom_periodic_var.set(False)
            return
        try:
            can_id, data = self._parse_custom_frame()
        except ValueError as e:
            messagebox.showerror("Bad input", str(e))
            self.custom_periodic_var.set(False)
            return
        interval = max(10, self._safe_int(self.custom_interval_var, 100))
        self.log(f"Repeating custom frame every {interval}ms: ID=0x{can_id:03X}, "
                 f"data={data.hex(' ') if data else '(empty)'}")

        def _send():
            can_id, data = self._parse_custom_frame()  # re-parsed each tick - reflects a live-edited field without needing to stop/restart
            self.bus.send(can_id, data)

        self._start_keepalive(
            "custom_frame", interval, _send,
            on_failure=lambda: self.custom_periodic_var.set(False),
        )

    def _send_expansion_spi(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        try:
            tx_bytes = bytes(int(b, 16) for b in self.spi_send_var.get().split())
        except ValueError:
            messagebox.showerror("Bad input", "Enter space-separated hex bytes, e.g. 01 02 03")
            return
        if not (1 <= len(tx_bytes) <= 7):
            messagebox.showerror("Bad input", "Enter between 1 and 7 bytes.")
            return
        data = bytes([len(tx_bytes)]) + tx_bytes
        # wait_for_one's own registration happens inside the call, so
        # sending first here (rather than registering the wait before
        # sending) is fine - the round trip for a handful of bit-banged
        # SPI bytes is comfortably within the 1s timeout either way.
        self.bus.send(CAN_ID_EXP_SPI_CMD, data)
        response = self.bus.wait_for_one(CAN_ID_EXP_SPI_RESP, timeout=1.0)
        if response is None:
            self.spi_response_var.set("(no response - board not running this firmware version, or not connected)")
            self.log(f"Sent 0x180 ({tx_bytes.hex(' ')}) - no 0x181 response.")
            return
        n = response[0] if len(response) > 0 else 0
        rx_bytes = response[1:1 + n]
        self.spi_response_var.set(rx_bytes.hex(" ") if rx_bytes else "(empty)")
        self.log(f"Sent 0x180 ({tx_bytes.hex(' ')}), received back: {rx_bytes.hex(' ')}")

    def _query_fram_state(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        self.bus.send(CAN_ID_QUERY_FRAM_STATE, b"")
        response = self.bus.wait_for_one(CAN_ID_FRAM_STATE_RESP, timeout=1.0)
        self._show_fram_state(response)

    def _show_fram_state(self, response):
        if response is None or len(response) < 8:
            self.fram_state_var.set("No response - board not running this firmware version, or not connected.")
            self.log("Queried 0x190 - no 0x191 response.")
            return
        valid, tool_id, had_error, temp_hi, temp_lo, speed, dir_or_interlock, fan = response[:8]
        if not valid:
            self.fram_state_var.set("No valid saved state (uninitialized F-RAM, or nothing saved yet).")
            self.log("F-RAM state: nothing valid saved.")
            return
        temp = (temp_hi << 8) | temp_lo
        tool_name = TOOL_NAMES.get(tool_id, f"unknown ({tool_id})")
        text = (f"Last saved under: {tool_name}\n"
                f"Temperature setpoint: {temp}°C  |  Speed/power: {speed}  |  "
                f"Direction/interlock: {'on' if dir_or_interlock else 'off'}  |  Fan: {fan}\n"
                f"Critical error active at last save: {'YES' if had_error else 'no'}")
        self.fram_state_var.set(text)
        self.log(f"F-RAM state: tool={tool_name}, temp={temp}, speed/power={speed}, "
                  f"dir/interlock={dir_or_interlock}, fan={fan}, had_error={had_error}")

    def _erase_fram(self):
        if self.bus is None:
            messagebox.showerror("Not connected", "Connect to the adapter first.")
            return
        if not messagebox.askyesno(
            "Erase F-RAM?",
            "This permanently erases the saved parameter state on the board's "
            "persistence F-RAM. This cannot be undone. Continue?",
            icon="warning",
        ):
            return
        self.bus.send(CAN_ID_ERASE_FRAM, ERASE_FRAM_MAGIC)
        response = self.bus.wait_for_one(CAN_ID_FRAM_STATE_RESP, timeout=1.0)
        if response is None:
            self.log("Sent F-RAM erase (0x192) - no confirmation response received.")
            messagebox.showwarning("No confirmation", "Erase command sent, but no response came back to confirm it.")
            return
        self.log("F-RAM erased and confirmed.")
        self._show_fram_state(response)
        messagebox.showinfo("Erased", "F-RAM erased and confirmed by the board.")

    # -------------------------------------------------------------------
    # Per-tool panels. Each is only ever built for the ONE tool the board
    # is actually jumpered for right now (see rebuild_tool_panel above) -
    # never more than one live at a time, so there's no risk of e.g. the
    # soldering iron's keepalive still firing while the laser panel is
    # showing.
    # -------------------------------------------------------------------

    def _start_keepalive(self, name, interval_ms, send_fn, on_failure=None):
        """Registers a periodic resend under `name`, cancelling whatever
        was previously running under that same name first. Matches what a
        real master controller has to do to satisfy the firmware's
        communication watchdogs (soldering iron/laser/3D-printer nozzle:
        250ms, layer fan: 1000ms) - without this, a setpoint sent once
        would get cut by the firmware itself a fraction of a second later,
        which would look like a bug in THIS tool rather than the expected,
        correct safety behavior it actually is.
        on_failure, when given, runs once if send_fn ever raises (a
        disconnected adapter, or an emptied Spinbox the caller's own
        send_fn reads from) - typically used to uncheck whatever "Active"
        checkbox started this, so the UI doesn't keep showing a keepalive
        as running after it's actually stopped rescheduling itself."""
        self._stop_keepalive(name)
        def _tick():
            try:
                send_fn()
            except Exception as e:
                self.log(f"Keepalive '{name}' stopped: {e}")
                self._keepalive_jobs.pop(name, None)
                if on_failure is not None:
                    try:
                        on_failure()
                    except Exception:
                        pass
                return  # deliberately doesn't reschedule - see on_failure above for reflecting this in the UI
            self._keepalive_jobs[name] = self.root.after(interval_ms, _tick)
        _tick()

    def _stop_keepalive(self, name):
        job_id = self._keepalive_jobs.pop(name, None)
        if job_id is not None:
            try:
                self.root.after_cancel(job_id)
            except Exception:
                pass

    def _safe_int(self, var, default=0):
        """Reads a Tkinter IntVar (typically Spinbox-backed), falling back
        to default if the field is currently empty or holds non-numeric
        text - .get() on an IntVar raises tk.TclError in that case (e.g. a
        user selecting the Spinbox's text and deleting it, or typing a
        stray letter, then clicking Send/Move before re-entering a number),
        which would otherwise propagate straight out of a one-shot button
        callback with no useful feedback."""
        try:
            return var.get()
        except tk.TclError:
            return default

    def _create_live_graph(self, parent, y_max, width=340, height=100, max_points=60):
        """Builds a simple live-scrolling line graph on a plain Canvas -
        deliberately not matplotlib/pyqtgraph, to keep this project at
        zero non-stdlib dependencies beyond pyserial. Y axis is fixed at
        0..y_max (the tool's own known setpoint ceiling, e.g. 450 for the
        soldering iron) rather than auto-scaling to whatever's been seen -
        a fixed, predictable scale is easier to read at a glance than one
        that keeps shifting. Returns add_point(value); each call appends
        one value and redraws, dropping the oldest once max_points is
        reached (a rolling window, not an ever-growing history - this is
        for watching a live trend, not logging one)."""
        canvas = tk.Canvas(parent, width=width, height=height, bg="#1a1a1a", highlightthickness=1,
                           highlightbackground="#444")
        canvas.pack(fill="x", padx=4, pady=(2, 6))
        values = []

        def add_point(value):
            values.append(max(0, min(y_max, value)))
            if len(values) > max_points:
                values.pop(0)
            if not canvas.winfo_exists():
                return
            canvas.delete("all")
            # Faint horizontal gridlines at 0%/50%/100% of y_max, purely
            # for scale reference.
            for frac in (0.0, 0.5, 1.0):
                y = height - frac * height
                canvas.create_line(0, y, width, y, fill="#333")
            if len(values) < 2:
                return
            step_x = width / max(1, max_points - 1)
            points = []
            for i, v in enumerate(values):
                x = i * step_x
                y = height - (v / y_max) * height
                points.extend([x, y])
            canvas.create_line(*points, fill="#00ADB5", width=2, smooth=False)

        return add_point

    def _build_soldering_iron_panel(self, parent):
        setpoint = tk.IntVar(value=0)
        active = tk.BooleanVar(value=False)
        temp_var = tk.StringVar(value="-- °C")
        endstop_var = tk.StringVar(value="--")

        ttk.Label(parent, text="Setpoint temperature (°C, 0-450):").grid(row=0, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=0, to=450, textvariable=setpoint, width=6).grid(row=0, column=1, sticky="w", padx=4)

        def _send_setpoint():
            value = max(0, min(450, setpoint.get()))
            self.bus.send(CAN_ID_SOLDER_SETPOINT, struct.pack(">H", value))

        def _toggle(*_):
            if active.get():
                self.log(f"Soldering iron: setpoint {setpoint.get()}°C, sending every 150ms "
                          f"(firmware watchdog is 250ms).")
                self._start_keepalive("solder", 150, _send_setpoint, on_failure=lambda: active.set(False))
            else:
                self._stop_keepalive("solder")
                self.bus.send(CAN_ID_SOLDER_SETPOINT, struct.pack(">H", 0))
                self.log("Soldering iron: turned off (setpoint 0 sent once).")

        ttk.Checkbutton(parent, text="Active (auto-resends to satisfy the 250ms watchdog)",
                         variable=active, command=_toggle).grid(row=0, column=2, sticky="w", padx=8)

        ttk.Separator(parent, orient="horizontal").grid(row=1, column=0, columnspan=3, sticky="ew", pady=6)
        ttk.Label(parent, text="Live temperature:").grid(row=2, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=temp_var, font=("", 11, "bold")).grid(row=2, column=1, sticky="w")
        ttk.Label(parent, text="Endstop / limit switch:").grid(row=3, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=3, column=1, sticky="w")
        graph_frame = ttk.Frame(parent)
        graph_frame.grid(row=4, column=0, columnspan=3, sticky="w", padx=4)
        add_temp_point = self._create_live_graph(graph_frame, y_max=450)

        def _on_telemetry(data):
            if len(data) < 3:
                return
            temp = struct.unpack(">H", data[0:2])[0]
            endstop = data[2]
            self.root.after(0, lambda: temp_var.set(f"{temp} °C"))
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))
            self.root.after(0, lambda: add_temp_point(temp))

        self.bus.register(CAN_ID_SOLDER_TELEMETRY, _on_telemetry)

    def _build_motion_panel(self, parent, tool_id, tool_name):
        # 5 tools (paste/liquid dispenser, screwdriver, both grippers)
        # share the exact same command (0x120) and have no telemetry of
        # their own - a plain stepper: direction + step count, one-shot,
        # no watchdog to satisfy.
        direction = tk.StringVar(value="Forward")
        steps = tk.IntVar(value=200)

        ttk.Label(parent, text=f"{tool_name} - plain stepper motion (0x120)",
                  font=("", 10, "bold")).grid(row=0, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))
        ttk.Label(parent, text="Direction:").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Combobox(parent, textvariable=direction, width=12, state="readonly",
                     values=["Forward", "Reverse"]).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(parent, text="Steps:").grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=1, to=4294967295, textvariable=steps, width=12).grid(row=2, column=1, sticky="w", padx=4)

        def _send_move():
            dir_byte = 0x01 if direction.get() == "Forward" else 0x00
            n = max(1, self._safe_int(steps, 1))
            data = bytes([dir_byte]) + struct.pack(">I", n)
            self.bus.send(CAN_ID_MOTION_CMD, data)
            self.log(f"{tool_name}: move {direction.get()}, {n} steps.")

        ttk.Button(parent, text="Move", command=_send_move).grid(row=2, column=2, padx=8)
        ttk.Label(
            parent,
            text="One-shot command, no telemetry from this tool - the firmware enables "
                 "the driver on receiving this frame and cuts power once the move finishes.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=3, sticky="w", padx=4, pady=(8, 0))

    def _build_vacuum_panel(self, parent):
        # Telemetry only - no commands for this tool at all.
        adc_var = tk.StringVar(value="--")
        detect_var = tk.StringVar(value="--")

        ttk.Label(parent, text="Vacuum Pickup - telemetry only (0x145)",
                  font=("", 10, "bold")).grid(row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 8))
        ttk.Label(parent, text="Analog reading (12-bit ADC):").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=adc_var, font=("", 11, "bold")).grid(row=1, column=1, sticky="w")
        ttk.Label(parent, text="Part detected (LM393):").grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=detect_var, font=("", 11, "bold")).grid(row=2, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 3:
                return
            adc = struct.unpack(">H", data[0:2])[0]
            detected = data[2]
            self.root.after(0, lambda: adc_var.set(str(adc)))
            self.root.after(0, lambda: detect_var.set("YES - part picked up" if detected else "no"))

        self.bus.register(CAN_ID_VACUUM_TELEMETRY, _on_telemetry)

    def _build_drill_panel(self, parent):
        speed = tk.IntVar(value=0)
        direction = tk.StringVar(value="Clockwise")
        rpm_var = tk.StringVar(value="--")
        endstop_var = tk.StringVar(value="--")

        ttk.Label(parent, text="Speed (0-255):").grid(row=0, column=0, sticky="w", padx=4, pady=4)
        speed_scale = ttk.Scale(parent, from_=0, to=255, variable=speed, orient="horizontal", length=160)
        speed_scale.grid(row=0, column=1, sticky="w", padx=4)
        speed_label = ttk.Label(parent, text="0")
        speed_label.grid(row=0, column=2, sticky="w")
        speed_scale.config(command=lambda v: speed_label.config(text=str(int(float(v)))))

        ttk.Label(parent, text="Direction:").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Combobox(parent, textvariable=direction, width=14, state="readonly",
                     values=["Clockwise", "Counter-clockwise"]).grid(row=1, column=1, sticky="w", padx=4)

        def _send_drill():
            dir_byte = 0x01 if direction.get() == "Counter-clockwise" else 0x00
            self.bus.send(CAN_ID_DRILL_CMD, bytes([max(0, min(255, self._safe_int(speed, 0))), dir_byte]))

        ttk.Button(parent, text="Send", command=_send_drill).grid(row=1, column=2, padx=8)
        ttk.Label(
            parent, text="No watchdog on this command - it holds until a new value is sent, "
                         "0 speed engages the active brake.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=2, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=3, column=0, columnspan=3, sticky="ew", pady=4)
        ttk.Label(parent, text="Actual RPM:").grid(row=4, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=rpm_var, font=("", 11, "bold")).grid(row=4, column=1, sticky="w")
        ttk.Label(parent, text="Endstop / limit switch:").grid(row=5, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=5, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 3:
                return
            rpm = struct.unpack(">H", data[0:2])[0]
            endstop = data[2]
            self.root.after(0, lambda: rpm_var.set(f"{rpm} RPM"))
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))

        self.bus.register(CAN_ID_DRILL_TELEMETRY, _on_telemetry)

    def _build_aoi_panel(self, parent):
        mode = tk.StringVar(value="Off")
        period_us = tk.IntVar(value=1000)
        endstop_var = tk.StringVar(value="--")

        ttk.Label(parent, text="Ring mode:").grid(row=0, column=0, sticky="w", padx=4, pady=4)
        ttk.Combobox(parent, textvariable=mode, width=16, state="readonly",
                     values=["Off", "Synchronous strobe", "Fixed continuous"]).grid(row=0, column=1, sticky="w", padx=4)
        ttk.Label(parent, text="Strobe period:").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=1, to=65535, textvariable=period_us, width=8).grid(row=1, column=1, sticky="w", padx=4)

        def _send_aoi():
            mode_byte = {"Off": 0x00, "Synchronous strobe": 0x01, "Fixed continuous": 0x02}[mode.get()]
            data = bytes([mode_byte]) + struct.pack(">H", max(1, min(65535, period_us.get())))
            self.bus.send(CAN_ID_AOI_CMD, data)
            self.log(f"AOI: mode={mode.get()}, period={period_us.get()}")

        ttk.Button(parent, text="Send", command=_send_aoi).grid(row=1, column=2, padx=8)
        ttk.Label(
            parent, text="Ring color comes from the Global Controls panel above (0x100) - this "
                         "only controls on/off/strobe timing, not the color itself.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=2, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=3, column=0, columnspan=3, sticky="ew", pady=4)
        ttk.Label(parent, text="Endstop / limit switch:").grid(row=4, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=4, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 1:
                return
            endstop = data[0]
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))

        self.bus.register(CAN_ID_AOI_TELEMETRY, _on_telemetry)

    def _build_laser_panel(self, parent):
        power = tk.IntVar(value=0)
        interlock = tk.BooleanVar(value=False)
        active = tk.BooleanVar(value=False)
        endstop_var = tk.StringVar(value="--")

        ttk.Label(
            parent, text="Interlock must be explicitly armed below - this is a real laser.",
            foreground="#b35900", wraplength=380, justify="left",
        ).grid(row=0, column=0, columnspan=3, sticky="w", padx=4, pady=(4, 8))

        ttk.Label(parent, text="Power (0-255):").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        power_scale = ttk.Scale(parent, from_=0, to=255, variable=power, orient="horizontal", length=160)
        power_scale.grid(row=1, column=1, sticky="w", padx=4)
        power_label = ttk.Label(parent, text="0")
        power_label.grid(row=1, column=2, sticky="w")
        power_scale.config(command=lambda v: power_label.config(text=str(int(float(v)))))

        ttk.Checkbutton(parent, text="Interlock armed (required for any power > 0 to matter)",
                         variable=interlock).grid(row=2, column=0, columnspan=3, sticky="w", padx=4, pady=4)

        def _send_laser():
            self.bus.send(CAN_ID_LASER_CMD, bytes([
                max(0, min(255, power.get())),
                0x01 if interlock.get() else 0x00,
            ]))

        def _toggle(*_):
            if active.get():
                self.log(f"Laser: power {power.get()}, interlock {'armed' if interlock.get() else 'safe'}, "
                          f"sending every 150ms (firmware watchdog is 250ms).")
                self._start_keepalive("laser", 150, _send_laser, on_failure=lambda: active.set(False))
            else:
                self._stop_keepalive("laser")
                self.bus.send(CAN_ID_LASER_CMD, bytes([0x00, 0x00]))
                self.log("Laser: stopped (power 0, interlock safe sent once).")

        ttk.Checkbutton(parent, text="Active (auto-resends to satisfy the 250ms watchdog)",
                         variable=active, command=_toggle).grid(row=3, column=0, columnspan=3, sticky="w", padx=4)

        ttk.Separator(parent, orient="horizontal").grid(row=4, column=0, columnspan=3, sticky="ew", pady=6)
        ttk.Label(parent, text="Endstop / limit switch:").grid(row=5, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=endstop_var).grid(row=5, column=1, sticky="w")

        def _on_telemetry(data):
            if len(data) < 1:
                return
            endstop = data[0]
            self.root.after(0, lambda: endstop_var.set("TRIGGERED" if endstop else "open"))

        self.bus.register(CAN_ID_LASER_TELEMETRY, _on_telemetry)

    def _build_scan_probe_panel(self, parent):
        # No commands at all - this tool only monitors PB3 and fires an
        # instant, max-priority event on contact.
        count_var = tk.StringVar(value="0")
        last_var = tk.StringVar(value="(none yet)")
        self._probe_impact_count = 0

        ttk.Label(parent, text="Scan Probe - event telemetry only (0x095)",
                  font=("", 10, "bold")).grid(row=0, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 8))
        ttk.Label(parent, text="Impacts detected this session:").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=count_var, font=("", 11, "bold")).grid(row=1, column=1, sticky="w")
        ttk.Label(parent, text="Last impact:").grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Label(parent, textvariable=last_var).grid(row=2, column=1, sticky="w")
        ttk.Label(
            parent,
            text="Sent straight from the touch interrupt at the lowest (highest-priority) "
                 "CAN ID on the whole bus, bypassing every scheduler - by design, this should "
                 "arrive essentially instantly on contact, every time.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=2, sticky="w", padx=4, pady=(8, 0))

        def _on_impact(data):
            if len(data) >= 1 and data[0] == 0x01:
                self._probe_impact_count += 1
                ts = time.strftime("%H:%M:%S")
                self.root.after(0, lambda: count_var.set(str(self._probe_impact_count)))
                self.root.after(0, lambda: last_var.set(ts))
                self.log(f"Scan probe: impact detected at {ts}")

        self.bus.register(CAN_ID_IMPACT_EVENT, _on_impact)

    def _build_3dprinter_panel(self, parent):
        nozzle_setpoint = tk.IntVar(value=0)
        nozzle_active = tk.BooleanVar(value=False)
        extruder_dir = tk.StringVar(value="Forward")
        extruder_steps = tk.IntVar(value=200)
        layer_fan_power = tk.IntVar(value=0)
        layer_fan_active = tk.BooleanVar(value=False)
        hotend_fan_power = tk.IntVar(value=0)
        hotend_temp_var = tk.StringVar(value="-- °C")
        layer_rpm_var = tk.StringVar(value="--")
        hotend_rpm_var = tk.StringVar(value="--")

        ttk.Label(parent, text="Nozzle setpoint (°C, 0-300):").grid(row=0, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=0, to=300, textvariable=nozzle_setpoint, width=6).grid(row=0, column=1, sticky="w", padx=4)

        def _send_thermal_motion():
            temp = max(0, min(300, self._safe_int(nozzle_setpoint, 0)))
            dir_byte = 0x01 if extruder_dir.get() == "Forward" else 0x00
            steps = max(0, self._safe_int(extruder_steps, 0)) & 0xFFFFFF
            data = struct.pack(">H", temp) + bytes([dir_byte]) + steps.to_bytes(3, "big")
            self.bus.send(CAN_ID_3DP_THERMAL_MOTION, data)

        def _toggle_nozzle(*_):
            if nozzle_active.get():
                self.log(f"3D printer nozzle: setpoint {nozzle_setpoint.get()}°C, sending every "
                          f"150ms (firmware watchdog is 250ms, extruder motion not watchdog-guarded).")
                self._start_keepalive("nozzle", 150, _send_thermal_motion, on_failure=lambda: nozzle_active.set(False))
            else:
                self._stop_keepalive("nozzle")
                extruder_steps_saved = self._safe_int(extruder_steps, 0)
                extruder_steps.set(0)
                _send_thermal_motion()
                extruder_steps.set(extruder_steps_saved)
                self.log("3D printer nozzle: turned off (setpoint 0 sent once).")

        ttk.Checkbutton(parent, text="Heater active (250ms watchdog)", variable=nozzle_active,
                         command=_toggle_nozzle).grid(row=0, column=2, sticky="w", padx=8)

        ttk.Label(parent, text="Extruder direction:").grid(row=1, column=0, sticky="w", padx=4, pady=4)
        ttk.Combobox(parent, textvariable=extruder_dir, width=10, state="readonly",
                     values=["Forward", "Retract"]).grid(row=1, column=1, sticky="w", padx=4)
        ttk.Label(parent, text="Extruder steps:").grid(row=2, column=0, sticky="w", padx=4, pady=4)
        ttk.Spinbox(parent, from_=0, to=16777215, textvariable=extruder_steps, width=10).grid(row=2, column=1, sticky="w", padx=4)
        ttk.Button(parent, text="Move extruder once", command=_send_thermal_motion).grid(row=2, column=2, padx=8)
        ttk.Label(
            parent, text="Extruder motion shares this same frame with the nozzle setpoint (0x170) "
                         "but isn't itself watchdog-guarded - only the temperature is.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=3, column=0, columnspan=3, sticky="w", padx=4, pady=(0, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=4, column=0, columnspan=3, sticky="ew", pady=4)
        ttk.Label(parent, text="Layer fan power (0-255):").grid(row=5, column=0, sticky="w", padx=4, pady=4)
        layer_scale = ttk.Scale(parent, from_=0, to=255, variable=layer_fan_power, orient="horizontal", length=140)
        layer_scale.grid(row=5, column=1, sticky="w", padx=4)
        layer_label = ttk.Label(parent, text="0")
        layer_label.grid(row=5, column=2, sticky="w")
        layer_scale.config(command=lambda v: layer_label.config(text=str(int(float(v)))))

        def _send_layer_fan():
            self.bus.send(CAN_ID_3DP_LAYER_FAN_CMD, bytes([max(0, min(255, layer_fan_power.get()))]))

        def _toggle_layer_fan(*_):
            if layer_fan_active.get():
                self.log(f"Layer fan: power {layer_fan_power.get()}, sending every 400ms "
                          f"(firmware watchdog is 1000ms).")
                self._start_keepalive("layer_fan", 400, _send_layer_fan, on_failure=lambda: layer_fan_active.set(False))
            else:
                self._stop_keepalive("layer_fan")
                self.bus.send(CAN_ID_3DP_LAYER_FAN_CMD, bytes([0x00]))
                self.log("Layer fan: stopped (power 0 sent once).")

        ttk.Checkbutton(parent, text="Active (1000ms watchdog)", variable=layer_fan_active,
                         command=_toggle_layer_fan).grid(row=6, column=0, columnspan=2, sticky="w", padx=4)

        ttk.Label(parent, text="Hotend fan power (0-255):").grid(row=7, column=0, sticky="w", padx=4, pady=4)
        hotend_fan_scale = ttk.Scale(parent, from_=0, to=255, variable=hotend_fan_power, orient="horizontal", length=140)
        hotend_fan_scale.grid(row=7, column=1, sticky="w", padx=4)
        hotend_fan_label = ttk.Label(parent, text="0")
        hotend_fan_label.grid(row=7, column=2, sticky="w")
        hotend_fan_scale.config(command=lambda v: hotend_fan_label.config(text=str(int(float(v)))))

        def _send_hotend_fan():
            self.bus.send(CAN_ID_3DP_HOTEND_FAN_CMD, bytes([max(0, min(255, hotend_fan_power.get()))]))

        ttk.Button(parent, text="Send", command=_send_hotend_fan).grid(row=7, column=3, padx=8)
        ttk.Label(
            parent, text="No communication watchdog on this fan - it's protected by a stall "
                         "detector instead (0 RPM 3s after starting = Critical Error), since "
                         "losing this fan matters even if the master goes briefly quiet.",
            foreground="gray", wraplength=380, justify="left",
        ).grid(row=8, column=0, columnspan=4, sticky="w", padx=4, pady=(0, 8))

        ttk.Separator(parent, orient="horizontal").grid(row=9, column=0, columnspan=4, sticky="ew", pady=4)
        ttk.Label(parent, text="Hotend temperature:").grid(row=10, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=hotend_temp_var, font=("", 11, "bold")).grid(row=10, column=1, sticky="w")
        ttk.Label(parent, text="Layer fan RPM:").grid(row=11, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=layer_rpm_var).grid(row=11, column=1, sticky="w")
        ttk.Label(parent, text="Hotend fan RPM:").grid(row=12, column=0, sticky="w", padx=4, pady=2)
        ttk.Label(parent, textvariable=hotend_rpm_var).grid(row=12, column=1, sticky="w")
        graph_frame = ttk.Frame(parent)
        graph_frame.grid(row=13, column=0, columnspan=4, sticky="w", padx=4)
        add_nozzle_temp_point = self._create_live_graph(graph_frame, y_max=300)

        def _on_hotend_temp(data):
            if len(data) < 2:
                return
            temp = struct.unpack(">H", data[0:2])[0]
            self.root.after(0, lambda: hotend_temp_var.set(f"{temp} °C"))
            self.root.after(0, lambda: add_nozzle_temp_point(temp))

        def _on_layer_rpm(data):
            if len(data) < 2:
                return
            rpm = struct.unpack(">H", data[0:2])[0]
            self.root.after(0, lambda: layer_rpm_var.set(f"{rpm} RPM"))

        def _on_hotend_rpm(data):
            if len(data) < 2:
                return
            rpm = struct.unpack(">H", data[0:2])[0]
            self.root.after(0, lambda: hotend_rpm_var.set(f"{rpm} RPM"))

        self.bus.register(CAN_ID_3DP_HOTEND_TELEM, _on_hotend_temp)
        self.bus.register(CAN_ID_3DP_LAYER_FAN_RPM, _on_layer_rpm)
        self.bus.register(CAN_ID_3DP_HOTEND_FAN_RPM, _on_hotend_rpm)

    def export_debug_bundle(self):
        save_path = filedialog.asksaveasfilename(
            title="Save Debug Bundle",
            defaultextension=".zip",
            initialfile=f"urtc_tester_debug_{time.strftime('%Y%m%d_%H%M%S')}.zip",
            filetypes=[("ZIP archive", "*.zip")],
        )
        if not save_path:
            return
        try:
            with zipfile.ZipFile(save_path, "w", zipfile.ZIP_DEFLATED) as zf:
                zf.writestr("session_log.txt", self.log_text.get("1.0", "end"))
                diag = [
                    f"URTC Tester version: {TESTER_VERSION}",
                    f"Python: {sys.version}",
                    f"Platform: {platform.platform()}",
                    f"pyserial available: {HAVE_SERIAL}",
                    f"SocketCAN available: {HAVE_SOCKETCAN}",
                    f"Current transport: {self.transport_var.get()}",
                    f"Current port/interface: {self.port_var.get()}",
                    f"Current bitrate (Serial/SLCAN): {self.bitrate_var.get()}",
                    f"Connected: {self.transport is not None}",
                    f"Active tool ID: {self.active_tool_id}",
                ]
                zf.writestr("system_diagnostics.txt", "\n".join(diag))
            messagebox.showinfo("Debug bundle saved", f"Saved to:\n{save_path}")
            self.log(f"Debug bundle exported to {save_path}")
        except OSError as e:
            messagebox.showerror("Export failed", str(e))

    def on_close(self):
        for job_id in list(self._keepalive_jobs.values()):
            try:
                self.root.after_cancel(job_id)
            except Exception:
                pass
        if self.bus is not None:
            self.bus.stop()
        if self.transport is not None:
            try:
                self.transport.close()
            except Exception:
                pass
        self.root.destroy()


def _center_geometry(win, width, height):
    """Returns a "WxH+X+Y" geometry string centered on the screen - see
    the flasher's identical helper for the full reasoning (using the
    caller's own known target size rather than winfo_width()/height(),
    which depend on the window already being fully realized)."""
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
    splash.withdraw()  # stays hidden until correctly positioned below - see the flasher's identical fix for the full reasoning
    splash.overrideredirect(True)
    splash.configure(bg="#14171C")
    label = tk.Label(splash, image=banner_img, bg="#14171C", borderwidth=0)
    label.image = banner_img
    label.pack()

    splash.geometry(_center_geometry(splash, banner_img.width(), banner_img.height()))
    splash.attributes("-topmost", True)
    splash.deiconify()  # only shown now that it's already correctly positioned

    def _finish():
        splash.destroy()
        on_done()

    splash.after(5000, _finish)


def main():
    root = tk.Tk()
    root.withdraw()
    root.geometry(_center_geometry(root, 1250, 1260))
    app = TesterGUI(root)

    def _reveal_main():
        root.deiconify()

    _show_splash_then(root, _reveal_main)
    root.mainloop()


if __name__ == "__main__":
    main()
