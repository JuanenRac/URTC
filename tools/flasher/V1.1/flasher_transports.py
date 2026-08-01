# =============================================================================
# URTC Flasher - CAN transports: SLCAN (serial), SocketCAN (Linux), and
# MockCAN (in-memory simulator for testing this tool's own logic)
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import os
import socket
import struct
import time

try:
    import serial
except ImportError:
    pass  # SLCAN's constructor is what actually needs this - SocketCAN/MockCAN work fine without it

from flasher_config import (
    _, CAN_ID_QUERY_VERSION, CAN_ID_VERSION_RESPONSE, CAN_ID_BOOTLOADER_VERSION_RESPONSE,
    CAN_ID_START_UPDATE, CAN_ID_HMAC_CHUNK, CAN_ID_DATA, CAN_ID_PAGE_ACK,
    CAN_ID_END_UPDATE, CAN_ID_STATUS, THIS_HARDWARE_ID,
    BITRATE_500K_SLCAN_CODE, FLASH_PAGE_SIZE,
)

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
        if not (0 <= can_id <= 0x7FF):
            raise SLCANError(f"ID 0x{can_id:X} is outside the standard 11-bit range (0-0x7FF)")
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
                # Read everything already sitting in the OS buffer in one
                # syscall rather than one byte at a time - during a burst
                # of CAN traffic (page ACKs, heartbeats) this cuts syscall
                # count dramatically. Falls back to a single blocking
                # read(1) when nothing is immediately available, so this
                # still waits correctly rather than busy-spinning.
                waiting = self.ser.in_waiting
                chunk = self.ser.read(waiting if waiting > 0 else 1)
                if not chunk:
                    continue  # OS-level read timed out with nothing yet - keep going until our own deadline
                self._rx_buf += chunk
                if len(self._rx_buf) > 4096:
                    # No real SLCAN line comes anywhere close to this length
                    # (a full 8-byte-data standard frame is ~22 characters) -
                    # this only grows this large if noise, a wrong baudrate,
                    # or a disconnected adapter is feeding bytes with no \r
                    # ever showing up. Discarding and starting fresh turns an
                    # unbounded memory leak into a one-time logged desync,
                    # and the next real \r (if the link recovers) resyncs
                    # normally from there.
                    self._rx_buf = b""
                continue
            line_bytes, self._rx_buf = self._rx_buf.split(b"\r", 1)
            line = line_bytes.decode("ascii", errors="ignore").strip()
            if not line:
                continue
            # Status characters (z/Z after a transmit ack, BELL on a bus
            # error) don't necessarily get their own \r-terminated line -
            # if one arrives with no \r of its own, it concatenates with
            # whatever real frame follows within the same buffered line,
            # putting a non-t/T character at position 0. Scanning for the
            # actual t/T start rather than assuming it's always first
            # keeps a real frame from being silently discarded just
            # because a status character happened to precede it.
            frame_start = -1
            for idx, ch in enumerate(line):
                if ch in ("t", "T"):
                    frame_start = idx
                    break
            if frame_start == -1:
                continue  # no frame in this line at all - pure status noise
            line = line[frame_start:]
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
            self.log(_("WARN_NO_CARRIER", interface=interface))
        try:
            self.sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
            self.sock.bind((interface,))
        except OSError as e:
            # Close the socket if it was created but bind() is what
            # actually failed - otherwise this leaks a file descriptor on
            # every failed attempt, which can add up across repeated
            # retries from the GUI.
            if hasattr(self, "sock"):
                try:
                    self.sock.close()
                except Exception:
                    pass
            raise SocketCANError(
                f"couldn't open '{interface}': {e}. Check it exists and is up - "
                f"'ip link show {interface}' - and that your user can access it "
                f"(SocketCAN access typically doesn't need special group "
                f"membership the way a serial port does, but bringing the "
                f"interface up in the first place usually needs sudo)."
            )
        self.sock.settimeout(0.05)
        self._last_timeout = 0.05

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
        if not (0 <= can_id <= 0x7FF):
            raise SocketCANError(f"ID 0x{can_id:X} is outside the standard 11-bit range (0-0x7FF)")
        if len(data) > 8:
            raise SocketCANError("CAN data payload cannot exceed 8 bytes")
        data = bytes(data)  # normalizes bytearray/memoryview too - a memoryview specifically has no __add__ at all, so the concatenation below would otherwise raise TypeError for that one input type
        padded = data + bytes(8 - len(data))
        frame = struct.pack(self._FRAME_FMT, can_id, len(data), padded)
        try:
            self.sock.send(frame)
        except OSError as e:
            raise SocketCANError(f"send failed on '{self.interface}': {e}")

    def read_frame(self, timeout=0.05):
        # settimeout() is itself a syscall - skipped when the caller asks
        # for the same value already in effect (the common case: this is
        # called in a tight polling loop, almost always with the same
        # fixed timeout each time), rather than re-set on every single call.
        if timeout != self._last_timeout:
            self.sock.settimeout(timeout)
            self._last_timeout = timeout
        try:
            frame = self.sock.recv(self._FRAME_SIZE)
        except socket.timeout:
            return None
        except OSError:
            return None
        try:
            can_id_raw, dlc, data = struct.unpack(self._FRAME_FMT, frame)
        except struct.error:
            # recv() returning fewer bytes than expected (interface torn
            # down mid-call, or similar) - treat the same as any other
            # "nothing valid arrived" case rather than crashing.
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


class MockCAN:
    """A simulated transport, implementing the same interface as SLCAN and
    SocketCAN (open_channel/close_channel/close/send_frame/read_frame),
    that plays back a plausible bootloader protocol sequence entirely in
    memory - no real adapter or board needed. Meant for testing this
    tool's own logic (retry behavior, timeout handling, UI state updates)
    against a predictable, repeatable simulated peer, not for anything
    that ships to run against real hardware.

    simulate_failure, when given, makes the simulated update fail
    verification with that specific VERIFY_FAIL_REASON_* value (see
    CANBUS.TXT) instead of completing successfully - useful for testing
    the failure-handling path without needing a board that's actually
    misconfigured to trigger a real one.
    """

    def __init__(self, simulate_failure=None, log=None):
        self.log = log or (lambda msg: None)
        self.simulate_failure = simulate_failure
        self._response_queue = []  # list of (can_id, data) tuples awaiting read_frame()
        self._update_total_size = 0
        self._update_bytes_received = 0
        self._next_page_index = 0

    def open_channel(self, bitrate_code=None):
        pass  # nothing to actually open

    def close_channel(self):
        pass

    def close(self):
        pass

    def send_frame(self, can_id, data):
        if not (0 <= can_id <= 0x7FF):
            raise SLCANError(f"ID 0x{can_id:X} is outside the standard 11-bit range (0-0x7FF)")
        if can_id == CAN_ID_QUERY_VERSION:
            # responder=0x01 (bootloader, matching this simulator's own
            # role), a plausible fake HardwareID/version, then the
            # bootloader-specific 0x7FA right alongside it.
            self._response_queue.append((
                CAN_ID_VERSION_RESPONSE,
                bytes([0x01]) + struct.pack(">I", THIS_HARDWARE_ID) + struct.pack(">H", 1) + bytes([0]),
            ))
            self._response_queue.append((CAN_ID_BOOTLOADER_VERSION_RESPONSE, bytes([1, 0, 1])))
        elif can_id == CAN_ID_START_UPDATE:
            self._update_total_size = struct.unpack(">I", data[0:4])[0]
            self._update_bytes_received = 0
            self._next_page_index = 0
            self._response_queue.append((CAN_ID_STATUS, bytes([0x02])))  # STATUS_ERASING
            self._response_queue.append((CAN_ID_STATUS, bytes([0x03])))  # STATUS_RECEIVING
        elif can_id == CAN_ID_HMAC_CHUNK:
            pass  # no response, matching the real bootloader protocol
        elif can_id == CAN_ID_DATA:
            self._update_bytes_received += len(data)
            page_size = 2048  # matches FLASH_PAGE_SIZE
            while self._update_bytes_received >= (self._next_page_index + 1) * page_size or (
                self._update_bytes_received >= self._update_total_size
                and self._update_bytes_received > self._next_page_index * page_size
            ):
                self._response_queue.append((CAN_ID_PAGE_ACK, struct.pack(">I", self._next_page_index)))
                self._next_page_index += 1
                if self._next_page_index * page_size >= self._update_total_size:
                    break
        elif can_id == CAN_ID_END_UPDATE:
            self._response_queue.append((CAN_ID_STATUS, bytes([0x06])))  # STATUS_VERIFYING
            if self.simulate_failure is not None:
                self._response_queue.append((CAN_ID_STATUS, bytes([0x05, self.simulate_failure])))
            else:
                self._response_queue.append((CAN_ID_STATUS, bytes([0x04])))  # verify OK

    def read_frame(self, timeout=0.05):
        if self._response_queue:
            time.sleep(0.005)  # a small, fixed delay - realistic enough without slowing tests down
            return self._response_queue.pop(0)
        return None


