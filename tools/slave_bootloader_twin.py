#!/usr/bin/env python3
# =============================================================================
# URTC - tools/slave_bootloader_twin.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""A host model of the bootloader of the expansion slave (STM32F303CB), the counterpart of tools/bootloader_twin.py for the chip on the I2C link bus.

The master's bootloader has a twin that is its own C compiled for the host. The slave's bootloader cannot be built that way as a whole program (its interrupt
handlers sit on the HAL of the I2C peripheral), so this twin is a Python model written from src/F303-slave/boot/*.c, line by line: the register protocol, the
update state machine, the confirmation by the master, the rollback floor, the append-only metadata log, the resume of an interrupted copy, the ten-second
silence rule and the jump rules. It runs on a flash that only lets bits go from 1 to 0 until a page is erased, and it can lose the power at any flash operation.

It is a model, not the chip: it says nothing about the pins, the clocks, the interrupt timing or the real flash. What it does is make the logic of the
bootloader reproducible without a board, including three properties of the code as it is built today that a bench test has to confirm or fix:

  * direction   the address callback arms a receive on the HAL direction constant that describes the MASTER reading, so over a real HAL a write by the master is
                answered as a read and a read as a write (``direction_fixed=False`` is the code as built, ``True`` what it would do when the test is inverted);
  * short write the design assumes the receive-complete callback fires on a STOP that comes before the 33-byte buffer is full; if the HAL reports it through
                the error path instead (``short_write="hal_error"``) every write except a full-length one is lost, including the register pointer;
  * window      a valid application is entered at once after every reset, so the bootloader is only reachable on a chip that holds no valid application
                (``listen_window_ms`` models the window a fix would add);
  * isr work    the update steps (erase, copy, hashing) and a 200 ms delay run inside the I2C event interrupt; with the SysTick below it in priority (the default of
                the HAL) the delay never ends and the watchdog resets the chip instead of the software reset.

    python tools/slave_bootloader_twin.py --check     # run the scenarios and print SLAVE_TWIN=PASS (exit status 0) or the failures (1)
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zlib
from dataclasses import dataclass, field

# ---- the layout and the protocol, as written in src/common/urtc_flash_layout.h, src/common/urtc_can_ids.h and boot/slaveboot_common.h -------------------------
FLASH_BASE = 0x08000000
FLASH_SIZE = 128 * 1024
PAGE = 2048
BOOTLOADER_SIZE = 18432
METADATA_ADDR = 0x08004800
MAIN_APP_ADDR = 0x08005000
BACKUP_APP_ADDR = 0x08012800
APP_MAX_SIZE = 54 * 1024
HARDWARE_ID = 0x0303CB01
METADATA_MAGIC = 0x55415041
COUNTER_FLOOR_COMPILED = 0
BOOTLOADER_VERSION = (0, 3, 4)     # major, minor, patch
IDLE_ABORT_MS = 10000              # slaveboot_main.c: a transfer silent for longer than this is given up
LOOP_MS = 50                       # the HAL_Delay(50) of the main loop
IWDG_MS = 800                      # prescaler 32, reload 999 on the 40 kHz LSI
RX_BUFFER = 33                     # the register and the largest write, REG_DATA's 32 bytes
SRAM_TOP = 0x20000000 + 0xA000
CCM_BASE = 0x10000000

REG_STATUS, REG_START_UPDATE, REG_DIGEST_EXPECTED, REG_DATA, REG_END_UPDATE, REG_PROGRESS = 0x00, 0x01, 0x02, 0x03, 0x04, 0x05
REG_QUERY_VERSION, REG_VERIFY_FAIL_REASON, REG_QUERY_VERSION_EXT, REG_CHALLENGE, REG_CONFIRM = 0x06, 0x07, 0x08, 0x09, 0x0A
REG_COUNTER_FLOOR, REG_RECORD_COUNTER = 0x0B, 0x0C
VERSION_EXT_MARKER = 0xE1

STATUS_LISTENING, STATUS_ERASING, STATUS_RECEIVING, STATUS_VERIFY_OK, STATUS_VERIFY_FAIL, STATUS_VERIFYING, STATUS_COPYING = 1, 2, 3, 4, 5, 6, 7
STATUS_AWAIT_CONFIRM, STATUS_ERROR = 0x08, 0xFF
FAIL_INCOMPLETE, FAIL_CRC32, FAIL_DIGEST, FAIL_HARDWARE_ID, FAIL_ROLLBACK, FAIL_NOT_CONFIRMED = 1, 2, 3, 4, 5, 6

META_STATE_APP_VALID, META_STATE_COPY_PENDING = 1, 2
META_RECORD_SIZE = 72
META_SLOTS = PAGE // META_RECORD_SIZE
PATCH_FLAG, PATCH_MAX, PATCH_UNKNOWN = 0x80, 0x7F, 0xFF
CONFIRM_DOMAIN = b"URTC-SLV-CONF-V2"

I2C_DIRECTION_TRANSMIT, I2C_DIRECTION_RECEIVE = 0, 1   # the HAL gives the direction of the MASTER: TRANSMIT is a write by the master


class PowerCut(Exception):
    """The supply was lost in the middle of a flash operation."""


# ---- version helpers (src/common/urtc_version_wire.h) ---------------------------------------------------------------------------------------------------
def minor_field_pack(minor: int, patch: int) -> int:
    return minor if patch > PATCH_MAX else ((PATCH_FLAG | patch) << 8) | minor


def minor_field_minor(field_value: int) -> int:
    return field_value & 0xFF


def minor_field_patch(field_value: int) -> int:
    high = (field_value >> 8) & 0xFF
    return (high & PATCH_MAX) if high & PATCH_FLAG else PATCH_UNKNOWN


def version_is_older(new_major: int, new_field: int, cur_major: int, cur_field: int) -> bool:
    if new_major != cur_major:
        return new_major < cur_major
    if minor_field_minor(new_field) != minor_field_minor(cur_field):
        return minor_field_minor(new_field) < minor_field_minor(cur_field)
    new_patch, cur_patch = minor_field_patch(new_field), minor_field_patch(cur_field)
    return new_patch != PATCH_UNKNOWN and cur_patch != PATCH_UNKNOWN and new_patch < cur_patch


def confirm_message(nonce: bytes, digest: bytes, hardware_id: int, major: int, minor_field: int, size: int, counter: int) -> bytes:
    """The 80 bytes both sides hash (src/common/urtc_slave_confirm.h)."""
    return CONFIRM_DOMAIN + nonce + digest + struct.pack(">IHHII", hardware_id, major, minor_field, size, counter)


def confirm_tag(nonce: bytes, digest: bytes, hardware_id: int, major: int, minor_field: int, size: int, counter: int) -> bytes:
    return hashlib.sha256(confirm_message(nonce, digest, hardware_id, major, minor_field, size, counter)).digest()


def application_image(size: int, seed: int = 7, valid_vectors: bool = True) -> bytes:
    """A reproducible application image. With ``valid_vectors`` its first words are a stack pointer in SRAM and an odd reset vector inside the slot, so the jump
    check of the bootloader accepts it; without, the image is a blob the bootloader checks and stores but refuses to enter."""
    body = bytearray(((i * 13 + seed) & 0xFF) for i in range(size))
    if valid_vectors:
        body[0:8] = struct.pack("<II", 0x20002000, MAIN_APP_ADDR + 0x101)
    return bytes(body)


# ---- the flash: bits go 1 -> 0 until the page is erased; power can be lost at any operation ------------------------------------------------------------
class Flash:
    def __init__(self) -> None:
        self.data = bytearray(b"\xff" * FLASH_SIZE)
        self.erases = 0
        self.programs = 0
        self.cut_after: int | None = None    # lose the power when this many operations have started (None: never)
        self.ops = 0

    def _tick(self) -> bool:
        self.ops += 1
        return self.cut_after is not None and self.ops >= self.cut_after

    def erase_page(self, address: int) -> None:
        offset = address - FLASH_BASE
        if offset % PAGE or not 0 <= offset < FLASH_SIZE:
            raise ValueError("erase address")
        if self._tick():
            self.data[offset:offset + PAGE // 2] = b"\xff" * (PAGE // 2)    # a torn erase: the first half is blank, the rest is what it was
            raise PowerCut
        self.data[offset:offset + PAGE] = b"\xff" * PAGE
        self.erases += 1

    def program_halfword(self, address: int, value: int) -> bool:
        """False (a programming error flag in the chip) when the half-word was not blank."""
        offset = address - FLASH_BASE
        if offset % 2 or not 0 <= offset < FLASH_SIZE - 1:
            raise ValueError("program address")
        if self._tick():
            self.data[offset] = value & 0xFF     # a torn program: the low byte is in, the high byte is not
            raise PowerCut
        if self.data[offset] != 0xFF or self.data[offset + 1] != 0xFF:
            return False
        self.data[offset:offset + 2] = struct.pack("<H", value)
        self.programs += 1
        return True

    def read(self, address: int, length: int) -> bytes:
        offset = address - FLASH_BASE
        return bytes(self.data[offset:offset + length])

    def write_blob(self, address: int, blob: bytes) -> None:
        """Writes without the rules and without counting (a programmer: how a chip is flashed over SWD)."""
        offset = address - FLASH_BASE
        self.data[offset:offset + len(blob)] = blob


@dataclass
class Meta:
    magic: int = METADATA_MAGIC
    state: int = META_STATE_APP_VALID
    hardware_id: int = HARDWARE_ID
    version_major: int = 0
    version_minor: int = 0
    size: int = 0
    crc32: int = 0
    counter: int = 0
    sha256: bytes = b"\x00" * 32

    def pack(self) -> bytes:
        return struct.pack("<8I32s", self.magic, self.state, self.hardware_id, self.version_major, self.version_minor, self.size, self.crc32, self.counter, self.sha256)

    @classmethod
    def unpack(cls, blob: bytes) -> "Meta":
        fields = struct.unpack("<8I32s", blob)
        return cls(*fields)


@dataclass
class WorkLog:
    """What one interrupt callback did: the time the master's clock is stretched for."""
    callback: str
    pages_erased: int = 0
    halfwords_programmed: int = 0
    bytes_hashed: int = 0
    delay_ms: int = 0


@dataclass
class SlaveBootloader:
    flash: Flash
    direction_fixed: bool = True                  # True: the address callback arms a receive for a write by the master; False: the callback before the fix (see the module note)
    short_write: str = "callback"                 # "callback" (the design) or "hal_error" (the HAL as the comment in slaveboot_main.c fears)
    tick_preempts_isr: bool = False               # the SysTick is below the I2C event interrupt (the default of the HAL), so HAL_Delay inside it never ends
    listen_window_ms: int = 600                   # SLAVEBOOT_LISTEN_WINDOW_MS; 0: a valid application is entered at once (the code before the window)
    tick: int = 1000
    defer_heavy_work: bool = True                 # END and CONFIRM only queue in the interrupt and run in the main loop; False: the code before, all of it inside the interrupt

    # the state of slaveboot_protocol.c / slaveboot_main.c (RAM: lost at every reset)
    current_status: int = STATUS_LISTENING
    verify_fail_reason: int = 0
    progress: int = 0xFF
    jumped: bool = False
    listening: bool = False
    reset_cause: str | None = None                # "software" or "iwdg" when the chip asked for (or was forced into) a reset
    errors: list[str] = field(default_factory=list)
    log: list[WorkLog] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.pending_read_register = 0xFF
        self.armed: str | None = None
        self.tx = b""
        self.in_progress = False
        self.failed = False
        self.awaiting_confirm = False
        self.total_size = 0
        self.declared_hw = 0
        self.received = 0
        self.running_crc = 0
        self.expected_digest = b"\x00" * 32
        self.digest_received = False
        self.nonce = b"\x00" * 16
        self.nonce_valid = False
        self.session_counter = 0
        self.record_counter = 0
        self.counter_received = False
        self.await_digest = b""
        self.await_major = 0
        self.await_minor = 0
        self.await_crc = 0
        self.page_buffer = bytearray(b"\xff" * PAGE)
        self.page_fill = 0
        self.page_index = 0
        self.last_activity = 0
        self._work = WorkLog("")
        self.hashed = 0
        self.window_end: int | None = None
        self.deferred: tuple[str, bytes] | None = None
        self.reset_due_at: int | None = None

    # ---- flash helpers (slaveboot_flash.c) --------------------------------------------------------------------------------------------------------
    def erase_pages(self, address: int, count: int) -> bool:
        for i in range(count):
            self.flash.erase_page(address + i * PAGE)
            self._work.pages_erased += 1
        return True

    def write_verified(self, address: int, blob: bytes) -> bool:
        padded = blob + (b"\xff" if len(blob) % 2 else b"")
        for i in range(0, len(padded), 2):
            if not self.flash.program_halfword(address + i, struct.unpack_from("<H", padded, i)[0]):
                return False
            self._work.halfwords_programmed += 1
        return self.flash.read(address, len(blob)) == blob

    def copy_region(self, dest: int, src: int, total: int) -> bool:
        pages = (total + PAGE - 1) // PAGE
        for p in range(pages):
            length = PAGE if (p + 1) * PAGE <= total else total - p * PAGE
            self.erases_one(dest + p * PAGE)
            if not self.write_verified(dest + p * PAGE, self.flash.read(src + p * PAGE, length)):
                return False
            self.progress = (p + 1) * 100 // pages
        return True

    def erases_one(self, address: int) -> None:
        self.flash.erase_page(address)
        self._work.pages_erased += 1

    def sha256_region(self, address: int, length: int) -> bytes:
        self.hashed += length
        self._work.bytes_hashed += length
        return hashlib.sha256(self.flash.read(address, length)).digest()

    # ---- the metadata log (slaveboot_flash.c) ------------------------------------------------------------------------------------------------------
    def _slot(self, index: int) -> bytes:
        return self.flash.read(METADATA_ADDR + index * META_RECORD_SIZE, META_RECORD_SIZE)

    def _newest(self) -> tuple[int, int] | None:
        best: tuple[int, int] | None = None
        for i in range(META_SLOTS):
            record = self._slot(i)
            generation, magic = struct.unpack_from("<II", record, 0)[0], struct.unpack_from("<I", record, 4)[0]
            if magic != METADATA_MAGIC or generation == 0xFFFFFFFF:
                continue
            if zlib.crc32(record[:-4]) & 0xFFFFFFFF != struct.unpack_from("<I", record, META_RECORD_SIZE - 4)[0]:
                continue
            if best is None or generation > best[1]:
                best = (i, generation)
        return best

    def metadata_read(self) -> Meta | None:
        newest = self._newest()
        return None if newest is None else Meta.unpack(self._slot(newest[0])[4:68])

    def metadata_write(self, meta: Meta) -> bool:
        generation = 1
        newest = self._newest()
        if newest is not None:
            if self._slot(newest[0])[4:68] == meta.pack():
                return True
            generation = newest[1] + 1
        slot = next((i for i in range(META_SLOTS) if self._slot(i) == b"\xff" * META_RECORD_SIZE), None)
        if slot is None:
            self.erases_one(METADATA_ADDR)
            slot = 0
        body = struct.pack("<I", generation) + meta.pack()
        record = body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
        return self.write_verified(METADATA_ADDR + slot * META_RECORD_SIZE, record)

    def counter_floor(self) -> int:
        meta = self.metadata_read()
        return max(COUNTER_FLOOR_COMPILED, meta.counter if meta else 0)

    # ---- start-up (slaveboot_main.c main, slaveboot_protocol.c ApplicationIsValid / JumpToApplication) ----------------------------------------------------
    @staticmethod
    def _stack_ok(sp: int) -> bool:
        return 0x20000000 + 0x100 <= sp <= SRAM_TOP or CCM_BASE + 0x100 <= sp <= CCM_BASE + 0x2000

    def application_is_valid(self) -> bool:
        meta = self.metadata_read()
        if meta is None:
            if not self._stack_ok(struct.unpack("<I", self.flash.read(MAIN_APP_ADDR, 4))[0]):
                return False
            image = self.flash.read(MAIN_APP_ADDR, APP_MAX_SIZE)
            adopted = Meta(hardware_id=HARDWARE_ID, version_major=0, version_minor=minor_field_pack(3, 0), size=APP_MAX_SIZE,
                           crc32=zlib.crc32(image) & 0xFFFFFFFF, counter=COUNTER_FLOOR_COMPILED, sha256=hashlib.sha256(image).digest())
            return self.metadata_write(adopted)
        if meta.hardware_id != HARDWARE_ID or meta.size == 0 or meta.size > APP_MAX_SIZE:
            return False
        if meta.state == META_STATE_COPY_PENDING:
            if self.sha256_region(BACKUP_APP_ADDR, meta.size) != meta.sha256:
                return False
            self.current_status = STATUS_COPYING
            if not self.copy_region(MAIN_APP_ADDR, BACKUP_APP_ADDR, meta.size):
                return False
            meta = Meta(**{**meta.__dict__, "state": META_STATE_APP_VALID})
            if not self.metadata_write(meta):
                return False
        if zlib.crc32(self.flash.read(MAIN_APP_ADDR, meta.size)) & 0xFFFFFFFF != meta.crc32:
            return False
        return self.sha256_region(MAIN_APP_ADDR, meta.size) == meta.sha256

    def can_jump(self) -> bool:
        stack, vector = struct.unpack("<II", self.flash.read(MAIN_APP_ADDR, 8))
        return self._stack_ok(stack) and not stack & 3 and bool(vector & 1) and MAIN_APP_ADDR <= vector < MAIN_APP_ADDR + APP_MAX_SIZE

    def jump_to_application(self) -> bool:
        self.jumped = self.can_jump()
        return self.jumped

    def boot(self) -> str:
        """What main() does after a reset: ``"jumped"`` into a valid application or ``"listening"`` on the link bus."""
        self._work = WorkLog("boot")
        if self.application_is_valid() and self.can_jump():
            if self.listen_window_ms > 0:
                self.listening = True
                self.window_end = self.tick + self.listen_window_ms
                return "window"
            self.jump_to_application()
            return "jumped"
        self.listening = True
        return "listening"

    def reboot(self) -> "SlaveBootloader":
        """A reset: the RAM is lost, the flash stays. The options of this model carry over."""
        return SlaveBootloader(self.flash, self.direction_fixed, self.short_write, self.tick_preempts_isr, self.listen_window_ms, self.tick, self.defer_heavy_work)

    # ---- time: the main loop (HAL_Delay(50), the ten-second rule) ------------------------------------------------------------------------------------
    def advance(self, milliseconds: int) -> None:
        end = self.tick + milliseconds
        while self.tick < end:
            self.tick = min(end, self.tick + LOOP_MS)
            self.service()
            if self.window_end is not None and (self.in_progress or self.failed or self.busy()):
                self.window_end = None          # an update started inside the window: the main loop takes over and never enters the application behind it
            if self.window_end is not None and self.tick >= self.window_end:
                self.window_end = None
                self.listening = False
                self.jump_to_application()      # the window closed with no update started: the application is entered
                return
            if self.in_progress and not self.failed and self.tick - self.last_activity > IDLE_ABORT_MS:
                if self.awaiting_confirm:
                    self.abort_unconfirmed()
                else:
                    self.in_progress = False
                    self.failed = False
                    self.current_status = STATUS_LISTENING

    # ---- the update (slaveboot_protocol.c) -------------------------------------------------------------------------------------------------------------
    def _fail(self, reason: int) -> None:
        self.current_status = STATUS_VERIFY_FAIL
        self.verify_fail_reason = reason
        self.awaiting_confirm = False
        self.nonce_valid = False
        self.in_progress = False
        self.failed = True

    def _flush_page(self) -> bool:
        self.page_buffer[self.page_fill:] = b"\xff" * (PAGE - self.page_fill)
        if not self.write_verified(BACKUP_APP_ADDR + self.page_index * PAGE, bytes(self.page_buffer)):
            return False
        self.page_index += 1
        self.page_fill = 0
        self.progress = self.received * 100 // self.total_size if self.total_size else 0
        return True

    def _make_challenge(self) -> None:
        self.session_counter += 1
        seed = struct.pack(">IIII", 0xC0FFEE, self.tick, self.session_counter, 0) + self.nonce
        self.nonce = hashlib.sha256(seed).digest()[:16]
        self.nonce_valid = True

    def start_update(self, data: bytes) -> None:
        size, hardware = struct.unpack(">II", data[:8])
        if size == 0 or size > APP_MAX_SIZE:
            self.current_status = STATUS_ERROR
            return
        if hardware != HARDWARE_ID:
            self.current_status = STATUS_VERIFY_FAIL
            self.verify_fail_reason = FAIL_HARDWARE_ID
            return
        self.total_size, self.declared_hw = size, hardware
        self.current_status = STATUS_ERASING
        try:
            self.erase_pages(BACKUP_APP_ADDR, (size + PAGE - 1) // PAGE)
        except PowerCut:
            self.errors.append("power lost while erasing the backup slot")
            raise
        self.page_buffer[:] = b"\xff" * PAGE
        self.received = 0
        self.running_crc = 0
        self.digest_received = False
        self.awaiting_confirm = False
        self.counter_received = False
        self.record_counter = 0
        self._make_challenge()
        self.page_fill = 0
        self.page_index = 0
        self.in_progress = True
        self.failed = False
        self.progress = 0
        self.last_activity = self.tick
        self.current_status = STATUS_RECEIVING

    def _usable(self) -> bool:
        return self.in_progress and not self.failed and not self.awaiting_confirm

    def digest_expected(self, data: bytes) -> None:
        if not self._usable():
            return
        self.expected_digest = bytes(data[:32])
        self.digest_received = True
        self.last_activity = self.tick

    def record_counter_write(self, data: bytes) -> None:
        if not self._usable():
            return
        self.record_counter = struct.unpack(">I", data[:4])[0]
        self.counter_received = True
        self.last_activity = self.tick

    def data(self, chunk: bytes) -> None:
        if not self._usable() or len(chunk) > 32:
            return
        self.last_activity = self.tick
        taken = bytearray()
        for byte in chunk:
            if self.received >= self.total_size:
                break
            self.page_buffer[self.page_fill] = byte
            self.page_fill += 1
            self.received += 1
            taken.append(byte)
            if self.page_fill == PAGE and not self._flush_page():
                self.running_crc = zlib.crc32(bytes(taken), self.running_crc)
                self.current_status, self.in_progress, self.failed = STATUS_ERROR, False, True
                return
        self.running_crc = zlib.crc32(bytes(taken), self.running_crc)

    def end_update(self, data: bytes) -> None:
        if not self._usable():
            return
        self.last_activity = self.tick
        if self.page_fill > 0 and not self._flush_page():
            self.current_status, self.in_progress, self.failed = STATUS_ERROR, False, True
            return
        if self.received != self.total_size or not self.digest_received:
            self._fail_plain(FAIL_INCOMPLETE)
            return
        expected_crc, major, minor_field = struct.unpack(">IHH", data[:8])
        self.current_status = STATUS_VERIFYING
        if (self.running_crc & 0xFFFFFFFF) != expected_crc:
            self._fail_plain(FAIL_CRC32)
            return
        digest = self.sha256_region(BACKUP_APP_ADDR, self.total_size)
        if digest != self.expected_digest:
            self._fail_plain(FAIL_DIGEST)
            return
        if self.counter_received and self.record_counter < self.counter_floor():
            self._fail_plain(FAIL_ROLLBACK)
            return
        current = self.metadata_read()
        if current is not None and current.state == META_STATE_APP_VALID and version_is_older(major, minor_field, current.version_major, current.version_minor):
            self._fail_plain(FAIL_ROLLBACK)
            return
        self.await_digest, self.await_major, self.await_minor, self.await_crc = digest, major, minor_field, expected_crc & 0xFFFFFFFF
        self.awaiting_confirm = True
        self.last_activity = self.tick
        self.current_status = STATUS_AWAIT_CONFIRM

    def _fail_plain(self, reason: int) -> None:
        self.current_status = STATUS_VERIFY_FAIL
        self.verify_fail_reason = reason
        self.in_progress = False
        self.failed = True

    def abort_unconfirmed(self) -> None:
        if self.awaiting_confirm:
            self._fail(FAIL_NOT_CONFIRMED)

    def confirm(self, tag: bytes) -> None:
        if not self.in_progress or self.failed or not self.awaiting_confirm:
            return
        self.awaiting_confirm = False
        self.nonce_valid = False
        self.last_activity = self.tick
        now_digest = self.sha256_region(BACKUP_APP_ADDR, self.total_size)
        if now_digest != self.await_digest:
            self._fail(FAIL_DIGEST)
            return
        if not self.counter_received:
            self._fail(FAIL_NOT_CONFIRMED)
            return
        expected = confirm_tag(self.nonce, now_digest, self.declared_hw, self.await_major, self.await_minor, self.total_size, self.record_counter)
        if expected != bytes(tag[:32]):
            self._fail(FAIL_NOT_CONFIRMED)
            return
        if self.record_counter < self.counter_floor():
            self._fail(FAIL_ROLLBACK)
            return
        pending = Meta(state=META_STATE_COPY_PENDING, hardware_id=self.declared_hw, version_major=self.await_major, version_minor=self.await_minor,
                       size=self.total_size, crc32=self.await_crc, counter=self.record_counter, sha256=self.await_digest)
        if not self.metadata_write(pending):
            self.current_status, self.in_progress, self.failed = STATUS_ERROR, False, True
            return
        self.current_status = STATUS_COPYING
        self.progress = 0
        if not self.copy_region(MAIN_APP_ADDR, BACKUP_APP_ADDR, self.total_size):
            self.current_status, self.in_progress, self.failed = STATUS_ERROR, False, True
            return
        if self.sha256_region(MAIN_APP_ADDR, self.total_size) != self.await_digest:
            self.current_status, self.in_progress, self.failed = STATUS_ERROR, False, True
            return
        if not self.metadata_write(Meta(**{**pending.__dict__, "state": META_STATE_APP_VALID})):
            self.current_status, self.in_progress, self.failed = STATUS_ERROR, False, True
            return
        self.current_status = STATUS_VERIFY_OK
        self.progress = 100
        if self.defer_heavy_work:
            self.reset_due_at = self.tick          # ServiceDeferred() resets the chip 200 ms later, from the main loop
            return
        # HAL_Delay(200) followed by NVIC_SystemReset(), inside the I2C event interrupt (the code before the heavy work left the interrupt)
        self._work.delay_ms = 200
        if self.tick_preempts_isr:
            self.tick += 200
            self.reset_cause = "software"
        else:
            self.tick += IWDG_MS      # the tick cannot advance under the interrupt that waits for it: the watchdog ends the wait
            self.reset_cause = "iwdg"

    # ---- the I2C link (slaveboot_main.c): what the HAL calls, and a master that drives the bus ---------------------------------------------------------
    def _answer(self, register: int) -> bytes:
        if register == REG_PROGRESS:
            return bytes([self.progress])
        if register == REG_QUERY_VERSION:
            meta = self.metadata_read()
            hw, major, minor = (meta.hardware_id, meta.version_major, minor_field_minor(meta.version_minor)) if meta else (0, 0, 0)
            return struct.pack(">BIHHB", 1, hw, major, minor, BOOTLOADER_VERSION[0])
        if register == REG_QUERY_VERSION_EXT:
            meta = self.metadata_read()
            return bytes([VERSION_EXT_MARKER, minor_field_patch(meta.version_minor) if meta else PATCH_UNKNOWN, BOOTLOADER_VERSION[1], BOOTLOADER_VERSION[2]])
        if register == REG_VERIFY_FAIL_REASON:
            return bytes([self.verify_fail_reason])
        if register == REG_CHALLENGE:
            return self.nonce if self.nonce_valid else b"\x00" * 16
        if register == REG_COUNTER_FLOOR:
            return struct.pack(">I", self.counter_floor())
        return bytes([self.current_status])      # REG_STATUS and every unknown register

    def _addr_callback(self, hal_direction: int) -> None:
        """HAL_I2C_AddrCallback. ``hal_direction`` is what the HAL passes: the direction of the master."""
        arm_receive = (hal_direction == I2C_DIRECTION_TRANSMIT) if self.direction_fixed else (hal_direction == I2C_DIRECTION_RECEIVE)
        if arm_receive:
            self.armed = "receive"
        else:
            self.armed = "transmit"
            self.tx = self._answer(self.pending_read_register)

    def _rx_complete(self, received: bytes) -> None:
        """HAL_I2C_SlaveRxCpltCallback with the bytes the master wrote (the register first)."""
        self._work = WorkLog(f"write 0x{received[0]:02X}")
        register = received[0]
        n = len(received)
        if n == 1:
            self.pending_read_register = register
        elif self.defer_heavy_work and self.busy():
            pass                                   # a step runs in the main loop, or the reset is due: writes wait
        elif self.defer_heavy_work and register == REG_END_UPDATE and n == 9:
            self.queue("end", received[1:])
        elif self.defer_heavy_work and register == REG_CONFIRM and n == 33:
            self.queue("confirm", received[1:])
        elif register == REG_START_UPDATE and n == 9:
            self.start_update(received[1:])
        elif register == REG_DIGEST_EXPECTED and n == 33:
            self.digest_expected(received[1:])
        elif register == REG_CONFIRM and n == 33:
            self.confirm(received[1:])
        elif register == REG_RECORD_COUNTER and n == 5:
            self.record_counter_write(received[1:])
        elif register == REG_DATA:
            self.data(received[1:])
        elif register == REG_END_UPDATE and n == 9:
            self.end_update(received[1:])
        self.log.append(self._work)

    # ---- the heavy steps out of the interrupt (QueueEndUpdate / QueueConfirm / ServiceDeferred) -----------------------------------------------------------------
    def busy(self) -> bool:
        return self.deferred is not None or self.reset_due_at is not None

    def queue(self, kind: str, data: bytes) -> None:
        if self.busy() or not self.in_progress or self.failed:
            return
        if kind == "end":
            if self.awaiting_confirm:
                return
            self.last_activity = self.tick
            self.current_status = STATUS_VERIFYING
        else:
            if not self.awaiting_confirm:
                return
            self.last_activity = self.tick
        self.deferred = (kind, bytes(data))

    def service(self) -> None:
        """ServiceDeferred(): one pass of the main loop."""
        if self.deferred is not None:
            kind, data = self.deferred
            self._work = WorkLog("main loop " + kind)
            if kind == "end":
                self.end_update(data)
            else:
                self.confirm(data)
            self.log.append(self._work)
            self.deferred = None
        if self.reset_due_at is not None and self.tick - self.reset_due_at >= 200:
            self.reset_due_at = None
            self.reset_cause = "software"

    def master_write(self, payload: bytes) -> int:
        """A write by the master (start, address with the write bit, bytes, stop). Returns how many bytes the slave accepted."""
        self.service()                     # the main loop runs between two transactions
        self._addr_callback(I2C_DIRECTION_TRANSMIT)
        if self.armed != "receive":
            return 0                       # the slave is armed to transmit: it does not take the bytes
        accepted = payload[:RX_BUFFER]
        if len(accepted) == RX_BUFFER or (accepted and self.short_write == "callback"):
            self._rx_complete(bytes(accepted))
        # short_write == "hal_error": the STOP before the buffer is full is an error of the HAL, the callback is not called and the bytes are lost
        return len(accepted)

    def master_read(self, count: int) -> bytes | None:
        """A read by the master. None when the slave is not armed to transmit (the bus would hang on the stretched clock)."""
        self.service()
        self._addr_callback(I2C_DIRECTION_RECEIVE)
        if self.armed != "transmit":
            return None
        return (self.tx + b"\xff" * count)[:count]

    def read_register(self, register: int, count: int) -> bytes | None:
        """The pointer write and the read the master does for every register read."""
        self.master_write(bytes([register]))
        return self.master_read(count)

    def write_register(self, register: int, payload: bytes = b"") -> int:
        return self.master_write(bytes([register]) + payload)


class Master:
    """The master side of the update, in the order of docs/SLAVE_BOOT_CONFIRMATION.md. Every step is a method so a scenario can leave one out or alter it."""

    def __init__(self, slave: SlaveBootloader, image: bytes, major: int = 0, minor: int = 4, patch: int = 0, counter: int = 1, hardware_id: int = HARDWARE_ID) -> None:
        self.slave, self.image = slave, image
        self.major, self.minor_field, self.counter, self.hardware_id = major, minor_field_pack(minor, patch), counter, hardware_id
        self.digest = hashlib.sha256(image).digest()
        self.crc = zlib.crc32(image) & 0xFFFFFFFF

    def floor(self) -> int | None:
        raw = self.slave.read_register(REG_COUNTER_FLOOR, 4)
        return None if raw is None else struct.unpack(">I", raw)[0]

    def start(self) -> None:
        self.slave.write_register(REG_START_UPDATE, struct.pack(">II", len(self.image), self.hardware_id))

    def send_digest(self, digest: bytes | None = None) -> None:
        self.slave.write_register(REG_DIGEST_EXPECTED, digest or self.digest)

    def send_counter(self, counter: int | None = None) -> None:
        self.slave.write_register(REG_RECORD_COUNTER, struct.pack(">I", self.counter if counter is None else counter))

    def send_data(self, image: bytes | None = None) -> None:
        blob = self.image if image is None else image
        for offset in range(0, len(blob), 32):
            self.slave.write_register(REG_DATA, blob[offset:offset + 32])

    def end(self, crc: int | None = None) -> None:
        self.slave.write_register(REG_END_UPDATE, struct.pack(">IHH", self.crc if crc is None else crc, self.major, self.minor_field))
        self.slave.advance(LOOP_MS)              # the main loop runs the checks that the interrupt queued

    def status(self) -> int:
        raw = self.slave.read_register(REG_STATUS, 1)
        return -1 if raw is None else raw[0]

    def reason(self) -> int:
        raw = self.slave.read_register(REG_VERIFY_FAIL_REASON, 1)
        return -1 if raw is None else raw[0]

    def nonce(self) -> bytes:
        return self.slave.read_register(REG_CHALLENGE, 16) or b""

    def send_confirmation(self, tag: bytes | None = None, nonce: bytes | None = None) -> None:
        if tag is None:
            tag = confirm_tag(self.nonce() if nonce is None else nonce, self.digest, self.hardware_id, self.major, self.minor_field, len(self.image), self.counter)
        self.slave.write_register(REG_CONFIRM, tag)
        self.slave.advance(300)                  # the main loop writes the update and, 200 ms after, resets the chip

    def run(self, *, confirm: bool = True) -> int:
        """The whole exchange; returns the status after it."""
        self.start()
        self.send_digest()
        self.send_counter()
        self.send_data()
        self.end()
        if confirm and self.status() == STATUS_AWAIT_CONFIRM:
            self.send_confirmation()
        return self.status()


def installed_chip(image_size: int = 3000, counter: int = 1, major: int = 0, minor: int = 3, patch: int = 0, **options) -> SlaveBootloader:
    """A bootloader on a chip that already holds a valid application (installed through the real exchange), as it is right after a reset."""
    flash = Flash()
    slave = SlaveBootloader(flash, **{"direction_fixed": True, **options})
    Master(slave, application_image(image_size, seed=1), major, minor, patch, counter).run()
    return slave.reboot()


# ---- the scenarios --------------------------------------------------------------------------------------------------------------------------------------
def boot_through_window(chip: "SlaveBootloader") -> str:
    """boot() and, when the chip listens first, the wait for the window to close: ``"jumped"`` or ``"listening"``."""
    verdict = chip.boot()
    if verdict == "window":
        chip.advance(chip.listen_window_ms + 50)
        return "jumped" if chip.jumped else "listening"
    return verdict


def run_checks() -> tuple[list[str], list[str]]:
    """Returns (problems, known): the behaviours that differ from the design (problems) and the ones that reproduce a property of the code as built (known)."""
    problems: list[str] = []
    known: list[str] = []

    def expect(condition: bool, text: str) -> None:
        if not condition:
            problems.append(text)

    # a good update through the whole exchange; every status on the way
    flash = Flash()
    slave = SlaveBootloader(flash, direction_fixed=True)
    expect(slave.boot() == "listening", "a blank chip must listen")
    expect(slave.read_register(REG_QUERY_VERSION, 10) == struct.pack(">BIHHB", 1, 0, 0, 0, 0), "a blank chip answers the version of nothing installed")
    image = application_image(3000)
    m = Master(slave, image, counter=3)
    expect(m.floor() == 0, "the floor of a blank chip is the compiled one")
    m.start()
    expect(m.status() == STATUS_RECEIVING and slave.progress == 0, "START did not reach RECEIVING")
    expect(len(m.nonce()) == 16 and m.nonce() != b"\x00" * 16, "the challenge was not made at START")
    m.send_digest()
    m.send_counter()
    m.send_data()
    m.end()
    expect(m.status() == STATUS_AWAIT_CONFIRM, f"after END the status is {m.status()}, expected AWAIT_CONFIRM")
    expect(slave.read_register(REG_STATUS, 1) == bytes([STATUS_AWAIT_CONFIRM]), "the status register must agree")
    expect(flash.read(MAIN_APP_ADDR, 8) == b"\xff" * 8, "the main slot was touched before the confirmation")
    expect(slave.metadata_read() is None, "the metadata was written before the confirmation")
    m.send_confirmation()
    expect(slave.reset_cause == "software", "the reset after a confirmed update is the software one, taken by the main loop 200 ms after")
    after = slave.reboot()
    expect(boot_through_window(after) == "jumped", "the new application was not entered after the reset")
    meta = after.metadata_read()
    expect(meta is not None and meta.state == META_STATE_APP_VALID and meta.counter == 3 and meta.size == 3000, f"installed metadata is wrong: {meta}")
    expect(flash.read(MAIN_APP_ADDR, 3000) == image, "the main slot does not hold the image")
    expect(struct.unpack(">I", after.read_register(REG_COUNTER_FLOOR, 4) or b"\0\0\0\0")[0] == 3, "the floor after the update is the counter of the image")

    def fresh(**kw) -> tuple[SlaveBootloader, Master]:
        s = SlaveBootloader(Flash(), direction_fixed=True, **kw)
        s.boot()
        return s, Master(s, application_image(3000), counter=2)

    # refusals, each with its reason and the main slot untouched
    for label, mutate, reason in (
        ("a flipped CRC", lambda mm: (mm.start(), mm.send_digest(), mm.send_counter(), mm.send_data(), mm.end(crc=mm.crc ^ 1)), FAIL_CRC32),
        ("a digest of other bytes", lambda mm: (mm.start(), mm.send_digest(b"\x01" * 32), mm.send_counter(), mm.send_data(), mm.end()), FAIL_DIGEST),
        ("no digest at all", lambda mm: (mm.start(), mm.send_counter(), mm.send_data(), mm.end()), FAIL_INCOMPLETE),
        ("a missing last chunk", lambda mm: (mm.start(), mm.send_digest(), mm.send_counter(), mm.send_data(mm.image[:-32]), mm.end()), FAIL_INCOMPLETE),
    ):
        s, mm = fresh()
        mutate(mm)
        expect(mm.status() == STATUS_VERIFY_FAIL and mm.reason() == reason, f"{label}: status {mm.status()} reason {mm.reason()}, expected VERIFY_FAIL {reason}")
        expect(s.flash.read(MAIN_APP_ADDR, 16) == b"\xff" * 16 and s.metadata_read() is None, f"{label}: the main slot or the metadata changed")
    s, mm = fresh()
    mm.hardware_id ^= 0x100
    mm.start()
    expect(mm.status() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_HARDWARE_ID, "a foreign hardware id must be refused at START")
    expect(s.flash.erases == 0, "a foreign hardware id must be refused before anything is erased")
    s, mm = fresh()
    s.write_register(REG_START_UPDATE, struct.pack(">II", APP_MAX_SIZE + 1, HARDWARE_ID))
    expect(mm.status() == STATUS_ERROR and s.flash.erases == 0, "a size above the slot must be an error before anything is erased")
    s.write_register(REG_START_UPDATE, struct.pack(">II", 0, HARDWARE_ID))
    expect(mm.status() == STATUS_ERROR, "a size of zero must be an error")

    # the confirmation: absent, wrong, replayed, for another session, for another image, without the counter
    s, mm = fresh()
    mm.run(confirm=False)
    expect(mm.status() == STATUS_AWAIT_CONFIRM, "the slave must wait for the confirmation")
    s.advance(IDLE_ABORT_MS)
    expect(mm.status() == STATUS_AWAIT_CONFIRM, "ten seconds of waiting are still allowed (the rule is 'longer than')")
    s.advance(2 * LOOP_MS)
    expect(mm.status() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_NOT_CONFIRMED, "no confirmation in ten seconds must fail closed with NOT_CONFIRMED")
    expect(s.metadata_read() is None and s.flash.read(MAIN_APP_ADDR, 8) == b"\xff" * 8, "failing closed must leave the metadata and the main slot alone")
    mm.send_confirmation()
    expect(mm.status() == STATUS_VERIFY_FAIL, "a confirmation after the failure must be ignored")
    s, mm = fresh()
    mm.run(confirm=False)
    mm.send_confirmation(tag=b"\x00" * 32)
    expect(mm.status() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_NOT_CONFIRMED and s.metadata_read() is None, "a wrong tag must be refused")
    s, mm = fresh()
    mm.run(confirm=False)
    stale = mm.nonce()
    s.write_register(REG_START_UPDATE, struct.pack(">II", 3000, HARDWARE_ID))     # a new session changes the nonce
    mm.send_digest(); mm.send_counter(); mm.send_data(); mm.end()                  # noqa: E702
    expect(mm.nonce() != stale, "a new session must have a new nonce")
    mm.send_confirmation(nonce=stale)
    expect(mm.status() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_NOT_CONFIRMED, "a confirmation of an earlier session must be refused")
    s, mm = fresh()
    mm.start(); mm.send_digest(); mm.send_data(); mm.end()                         # noqa: E702  (the counter was never sent)
    expect(mm.status() == STATUS_AWAIT_CONFIRM, "without the counter the slave can still reach AWAIT_CONFIRM")
    mm.send_confirmation()
    expect(mm.status() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_NOT_CONFIRMED, "a master that never sent the counter cannot have the tag accepted")
    s, mm = fresh()
    mm.run(confirm=False)
    s.flash.data[BACKUP_APP_ADDR - FLASH_BASE + 100] ^= 0x01                       # a bit of the verified image changes before the confirmation
    mm.send_confirmation()
    expect(mm.status() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_DIGEST, "an image altered after the check must be caught when the confirmation arrives")
    s, mm = fresh()
    mm.run(confirm=False)
    mm.send_confirmation()
    tag_used = mm.status()
    s.write_register(REG_CONFIRM, b"\x00" * 32)
    expect(tag_used in (STATUS_VERIFY_OK, STATUS_COPYING) or s.reset_cause is not None, "a good confirmation must install")

    # rollback: the floor and the version rule
    chip = installed_chip(counter=5, major=0, minor=5, patch=1)
    expect(Master(chip, b"", counter=0).floor() == 5, "the floor after an install is its counter")
    mm = Master(chip, application_image(3000, seed=9), major=0, minor=6, patch=0, counter=4)
    expect(mm.run() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_ROLLBACK, "a newer version signed with a lower counter must be refused")
    chip = installed_chip(counter=5, major=0, minor=5, patch=1)
    mm = Master(chip, application_image(3000, seed=9), major=0, minor=4, patch=0, counter=9)
    expect(mm.run() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_ROLLBACK, "an older version signed with a higher counter must be refused by the version rule")
    chip = installed_chip(counter=5, major=0, minor=5, patch=1)
    mm = Master(chip, application_image(3000, seed=9), major=0, minor=5, patch=0, counter=9)
    expect(mm.run() == STATUS_VERIFY_FAIL and mm.reason() == FAIL_ROLLBACK, "the same minor with an older patch must be refused")
    chip = installed_chip(counter=5, major=0, minor=5, patch=1)
    mm = Master(chip, application_image(3000, seed=9), major=0, minor=5, patch=1, counter=5)
    expect(mm.run() != STATUS_VERIFY_FAIL, "the same version with the same counter (a reinstall) must be allowed")

    # the ten seconds of silence in the middle of a transfer
    s, mm = fresh()
    mm.start(); mm.send_digest(); mm.send_data(mm.image[:640])                    # noqa: E702
    s.advance(IDLE_ABORT_MS)
    expect(mm.status() == STATUS_RECEIVING, "ten seconds of silence are still allowed")
    s.advance(2 * LOOP_MS)
    expect(mm.status() == STATUS_LISTENING, "a transfer silent for longer than ten seconds must go back to listening")
    mm.end()
    expect(mm.status() == STATUS_LISTENING, "an END after the abandonment must be ignored")
    mm.run()
    expect(mm.status() in (STATUS_VERIFY_OK, STATUS_COPYING) or s.reset_cause is not None, "a transfer after an abandoned one must work")

    # power lost at every instant of an update: the next boot runs the old or the new application, never a broken one
    old_image, new_image = application_image(3000, seed=1), application_image(3000, seed=2)
    outcomes = {"old": 0, "new": 0, "bootloader": 0}
    reference = Flash()
    ref = SlaveBootloader(reference, direction_fixed=True)
    Master(ref, old_image, counter=1).run()
    base = bytes(reference.data)
    probe = SlaveBootloader(Flash(), direction_fixed=True)
    probe.flash.data[:] = base
    Master(probe, new_image, minor=5, counter=2).run()
    total_ops = probe.flash.ops
    for cut in sorted(set(list(range(1, total_ops + 1, max(1, total_ops // 400))) + [total_ops - 1, total_ops])):
        flash = Flash()
        flash.data[:] = base
        flash.cut_after = cut
        chip = SlaveBootloader(flash, direction_fixed=True)
        chip.boot()
        try:
            Master(chip, new_image, minor=5, counter=2).run()
        except PowerCut:
            pass
        flash.cut_after = None
        after = SlaveBootloader(flash, direction_fixed=True)
        verdict = boot_through_window(after)
        if verdict == "jumped":
            held = flash.read(MAIN_APP_ADDR, 3000)
            if held == old_image:
                outcomes["old"] += 1
            elif held == new_image:
                outcomes["new"] += 1
            else:
                problems.append(f"power lost at flash operation {cut}: the chip entered an application that is neither the old nor the new one")
        else:
            outcomes["bootloader"] += 1
            problems.append(f"power lost at flash operation {cut}: the chip did not enter an application (it stays in the bootloader)")
        floor = after.counter_floor()
        if floor not in (1, 2):
            problems.append(f"power lost at flash operation {cut}: the rollback floor read back is {floor}")
    expect(outcomes["old"] > 0 and outcomes["new"] > 0, f"the sweep of power cuts must see both the old and the new application: {outcomes}")
    known.append(f"power cut: {sum(outcomes.values())} cut points over {total_ops} flash operations, {outcomes['old']} boots into the old application, {outcomes['new']} into the new one, none broken")

    # a cut during the copy leaves COPY_PENDING; the next boot resumes it from the backup slot
    flash = Flash()
    flash.data[:] = base
    chip = SlaveBootloader(flash, direction_fixed=True)
    chip.boot()
    mm = Master(chip, new_image, minor=5, counter=2)
    mm.start(); mm.send_digest(); mm.send_counter(); mm.send_data(); mm.end()      # noqa: E702
    flash.cut_after = flash.ops + 700
    try:
        mm.send_confirmation()
    except PowerCut:
        pass
    flash.cut_after = None
    pending = SlaveBootloader(flash, direction_fixed=True)
    state = pending.metadata_read()
    expect(state is not None and state.state == META_STATE_COPY_PENDING, "a cut in the middle of the copy must leave the pending-copy record")
    expect(boot_through_window(pending) == "jumped" and flash.read(MAIN_APP_ADDR, 3000) == new_image, "the next boot must finish the copy and enter the new application")
    meta = pending.metadata_read()
    expect(meta is not None and meta.state == META_STATE_APP_VALID and meta.counter == 2, "after the resume the record must be valid with the new counter")

    # a pending copy whose backup slot no longer holds the verified image is not resumed
    flash.data[:] = base
    chip = SlaveBootloader(flash, direction_fixed=True)
    chip.boot()
    mm = Master(chip, new_image, minor=5, counter=2)
    mm.start(); mm.send_digest(); mm.send_counter(); mm.send_data(); mm.end()      # noqa: E702
    flash.cut_after = flash.ops + 700
    try:
        mm.send_confirmation()
    except PowerCut:
        pass
    flash.cut_after = None
    flash.data[BACKUP_APP_ADDR - FLASH_BASE + 50] &= 0x0F
    damaged = SlaveBootloader(flash, direction_fixed=True)
    expect(damaged.boot() == "listening", "a pending copy whose backup slot was damaged must not be resumed")

    # the metadata log: it grows by appending and keeps the floor across the erase of a full page
    flash = Flash()
    chip = SlaveBootloader(flash, direction_fixed=True)
    chip.boot()
    for n in range(1, META_SLOTS + 4):
        chip.metadata_write(Meta(version_major=0, version_minor=n & 0xFF, size=100, counter=n, sha256=bytes([n]) * 32))
        newest = chip.metadata_read()
        if newest is None or newest.counter != n:
            problems.append(f"metadata record {n} was not read back as the newest")
            break
    expect(flash.erases == 1, f"the metadata page was erased {flash.erases} times for {META_SLOTS + 3} records, expected once (when the 28 slots were full)")

    # an application flashed with a programmer (no metadata) is adopted; a blob with bad vectors is not entered
    flash = Flash()
    flash.write_blob(MAIN_APP_ADDR, application_image(APP_MAX_SIZE))
    expect(boot_through_window(SlaveBootloader(flash, direction_fixed=True)) == "jumped", "an application flashed with a programmer must be adopted and entered")
    flash = Flash()
    flash.write_blob(MAIN_APP_ADDR, application_image(2000, valid_vectors=False))
    expect(SlaveBootloader(flash, direction_fixed=True).boot() == "listening", "a main slot whose first word is not a stack pointer must not be entered")
    flash = Flash()
    garbage = bytearray(application_image(APP_MAX_SIZE))
    garbage[4:8] = struct.pack("<I", 0x08000000 + 0x101)                          # a reset vector outside the slot
    flash.write_blob(MAIN_APP_ADDR, bytes(garbage))
    expect(SlaveBootloader(flash, direction_fixed=True).boot() == "listening", "a reset vector outside the slot must not be entered")

    # ---- what the source does about the properties that the model reproduced (the code before each fix stays available as an option) ----------------------------
    # 1. the listening window: a valid application is entered only after it, so the bootloader can be reached after a reset
    before = installed_chip(listen_window_ms=0)
    expect(before.boot() == "jumped", "with no window (the code before it) a valid application is entered at once")
    windowed = installed_chip()
    expect(windowed.boot() == "window" and windowed.listening and not windowed.jumped, "with a window the bootloader listens before it enters the application")
    windowed.advance(650)
    expect(windowed.jumped and not windowed.listening, "a window that closes with no update started must enter the application")
    busy = installed_chip()
    busy.boot()
    Master(busy, application_image(3000, seed=4), minor=5, counter=2).start()
    busy.advance(650)
    expect(not busy.jumped and busy.in_progress, "an update started inside the window must not be cut by it")
    failed = installed_chip()
    failed.boot()
    Master(failed, application_image(3000, seed=4), minor=5, counter=2).start()
    failed.write_register(REG_END_UPDATE, struct.pack(">IHH", 0, 0, 0))     # END with nothing received: the update fails inside the window
    failed.advance(700)
    expect(not failed.jumped, "an update that failed inside the window must not be followed by a jump into the application")
    known.append("window: FIXED in the source (SLAVEBOOT_LISTEN_WINDOW_MS, 600 ms): a chip with a valid application listens before it enters it; to confirm on a board: restart the slave and start an update inside the window (test 10)")

    # 2. the direction: the callback arms a receive for a write by the master
    before = SlaveBootloader(Flash(), direction_fixed=False)
    before.boot()
    wrote = before.master_write(bytes([REG_PROGRESS]))
    expect(wrote == 0 and before.pending_read_register == 0xFF, "the callback before the fix: a write by the master must not reach the register pointer (it armed a transmit)")
    expect(before.master_read(1) is None, "the callback before the fix: a read by the master finds the slave armed to receive and gets nothing")
    fixed = SlaveBootloader(Flash())
    fixed.boot()
    expect(fixed.read_register(REG_STATUS, 1) == bytes([STATUS_LISTENING]), "with the direction fixed the status read works")
    known.append("direction: FIXED in the source (a write by the master, direction TRANSMIT in the HAL, arms the receive); to confirm on a board: write 1, 4 and 8 bytes and read them back (test 2)")

    # 3. the short write: only writes that fill the 33-byte buffer arrive if the HAL reports the early STOP as an error
    chip = SlaveBootloader(Flash(), direction_fixed=True, short_write="hal_error")
    chip.boot()
    chip.master_write(bytes([REG_PROGRESS]))
    expect(chip.pending_read_register == 0xFF, "with an error on the early STOP the register pointer is lost")
    chip.write_register(REG_DIGEST_EXPECTED, b"\x07" * 32)
    expect(chip.digest_received is False, "no update is open, so a full-length write is ignored")
    chip.write_register(REG_START_UPDATE, struct.pack(">II", 100, HARDWARE_ID))
    expect(chip.in_progress is False and chip.current_status == STATUS_LISTENING, "START is 9 bytes: with an error on the early STOP it never arrives")
    callback = SlaveBootloader(Flash(), direction_fixed=True, short_write="callback")
    callback.boot()
    callback.write_register(REG_START_UPDATE, struct.pack(">II", 100, HARDWARE_ID))
    expect(callback.in_progress, "with the callback on the early STOP START arrives")
    known.append("short write: if the HAL reports a STOP before 33 bytes through the error path, the pointer write, START, END and the counter are lost and only 33-byte writes (digest, confirmation) arrive")

    # 4. the work done inside the interrupt, and the delay that cannot end
    chip = SlaveBootloader(Flash(), defer_heavy_work=False)
    chip.boot()
    mm = Master(chip, application_image(APP_MAX_SIZE), counter=1)
    mm.run()
    work = {entry.callback: entry for entry in chip.log}
    expect(work["write 0x0A"].delay_ms == 200 and chip.reset_cause == "iwdg", "the code before: the delay inside the interrupt ends in the watchdog when the SysTick is below it")
    chip = SlaveBootloader(Flash())
    chip.boot()
    mm = Master(chip, application_image(APP_MAX_SIZE), counter=1)
    mm.run()
    chip.advance(300)
    work = {entry.callback: entry for entry in chip.log}
    start, confirm_isr, confirm_loop = work["write 0x01"], work["write 0x0A"], work["main loop confirm"]
    expect(start.pages_erased == 27, f"START still erases the 27 pages of the backup slot inside the callback (the master relies on the stretched clock), saw {start.pages_erased}")
    expect(confirm_isr.pages_erased == 0 and confirm_isr.bytes_hashed == 0 and confirm_isr.delay_ms == 0, f"the confirmation does no work inside the callback: {confirm_isr}")
    expect(confirm_loop.pages_erased >= 27 and confirm_loop.bytes_hashed >= 2 * APP_MAX_SIZE and confirm_loop.delay_ms == 0, f"the main loop hashes, erases and copies: {confirm_loop}")
    expect(work["main loop end"].bytes_hashed >= APP_MAX_SIZE and work["write 0x04"].bytes_hashed == 0, "END is hashed in the main loop, not in the callback")
    expect(chip.reset_cause == "software", "the reset comes from the main loop, 200 ms after the update, whatever the priority of the tick")
    chip = SlaveBootloader(Flash())
    chip.boot()
    mm = Master(chip, application_image(3000), counter=1)
    mm.start(); mm.send_digest(); mm.send_counter(); mm.send_data(); mm.end()      # noqa: E702
    before_busy = chip.write_register(REG_DATA, b"\x01" * 8)
    expect(before_busy == 9, "the data write is accepted by the callback")
    chip.advance(LOOP_MS)
    expect(chip.current_status == STATUS_AWAIT_CONFIRM, "the main loop finishes the checks and the chip waits for the confirmation")
    known.append(f"interrupt work: START still erases {start.pages_erased} pages inside the I2C event callback (the master paces its data by the stretched clock, so START stays there); END and the confirmation "
                 "(hashes, copy, 200 ms pause) now run in the main loop and the reset comes from it; to confirm on a board: a full update with the logic analyser on the clock (test 15)")
    return problems, known


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="run the scenarios (no compiler, no board)")
    args = parser.parse_args(argv)
    if not args.check:
        parser.print_help()
        return 0
    problems, known = run_checks()
    for line in known:
        print("KNOWN " + line)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print(f"SLAVE_TWIN=FAIL {len(problems)} problem(s)", file=sys.stderr)
        return 1
    print("SLAVE_TWIN=PASS the model of the slave bootloader took a confirmed update, refused the unconfirmed, the altered, the replayed and the rolled-back ones, "
          "gave up a silent transfer, and survived a power cut at every sampled flash operation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
