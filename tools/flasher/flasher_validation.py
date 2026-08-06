# =============================================================================
# URTC Flasher - Firmware image validation for the SWD pickers: .bin, Intel
# HEX, and ELF sanity/plausibility checks before anything gets written
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
import os
import struct

from flasher_config import APP_FLASH_ADDR, APP_MAX_SIZE, BOOTLOADER_FLASH_ADDR

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
    # Lower bound is 0x20000100, not the exact start of RAM: Cortex-M's
    # stack grows downward, so a stack pointer sitting right at 0x20000000
    # leaves zero bytes of real headroom before the first PUSH underflows
    # into whatever precedes RAM in the address space. A small margin
    # catches this without rejecting a genuinely small-stack build.
    if not (0x20000100 <= initial_sp <= 0x2000A000):
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

        # Checksum: the two's-complement sum of every byte on the line
        # (byte count, address hi/lo, record type, all data bytes, and the
        # checksum byte itself) must equal 0 mod 256. Catches a corrupted
        # or partially-overwritten file that would otherwise look
        # structurally fine while actually having altered bytes. Computed
        # before the EOF check below so a corrupted EOF line (record_type
        # still reads as 0x01, but something else on the line changed)
        # gets caught here too, rather than being waved through on sight.
        try:
            checksum_byte = int(ln[9 + byte_count * 2:9 + byte_count * 2 + 2], 16)
        except (ValueError, IndexError):
            return False, f"malformed Intel HEX line (missing/bad checksum): {ln[:20]}", size
        line_sum = (byte_count + (address >> 8) + (address & 0xFF) + record_type
                    + sum(data) + checksum_byte) & 0xFF
        if line_sum != 0:
            return False, f"checksum mismatch on line: {ln[:20]}", size

        if record_type == 0x01:
            saw_eof = True
            break

        if record_type == 0x04 and len(data) == 2:
            base_addr = (data[0] << 8 | data[1]) << 16
        elif record_type == 0x02 and len(data) == 2:
            # Extended Segment Address - less common than 0x04 for 32-bit
            # ARM targets (which this project's own toolchain uses), but
            # some older/other toolchains still emit it. Segment value is
            # shifted left 4 bits (per the Intel HEX spec) rather than 16.
            base_addr = (data[0] << 8 | data[1]) << 4
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
    if min_addr not in candidates and all((min_addr + i) in data_bytes for i in range(4)):
        candidates.append(min_addr)

    for vector_addr in candidates:
        sp_bytes = bytes(data_bytes[vector_addr + i] for i in range(4))
        initial_sp = struct.unpack("<I", sp_bytes)[0]
        if 0x20000100 <= initial_sp <= 0x2000A000:
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
        if 0x20000100 <= initial_sp <= 0x2000A000:
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
    bootloader vs 112KB app) and has to handle .hex/.elf as well as .bin."""
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
    if not (0x20000100 <= initial_sp <= 0x2000A000):
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


