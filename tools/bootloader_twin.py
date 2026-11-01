#!/usr/bin/env python3
# =============================================================================
# URTC - tools/bootloader_twin.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Builds and drives the twin of the bootloader: the real C of the master's bootloader, compiled for the host.

The twin (tests/virtual_bootloader.c) is bootloader_main.c, bootloader_protocol.c, bootloader_flash.c and bootloader_crypto.c unchanged, behind an SLCAN-style
text port: one line per frame on standard input, one line per transmitted frame on standard output. A host tool talks to it as to an adapter, so a test of
the flasher can exercise the code that runs on the chip instead of a Python model of it. The fake flash only lets bits go from 1 to 0 until a page is erased,
the clock is the real time of the wait (plus ``W<ms>`` lines, which the twin alone understands), and a jump into the application ends the run with a ``J`` line.

    python tools/bootloader_twin.py --build-only      # build it, print VIRTUAL_BOARD_COMMAND=<JSON list> (urtc-virtual-board --firmware-repo ... --bootloader uses it)
    python tools/bootloader_twin.py --check           # build it and run a signed update, a refused one and a power cut through it

On Windows the build and the run go through WSL (the fake flash maps memory a 64-bit Linux gives). Exit status: 0 passed, 1 a check failed, 2 skipped because no
host compiler was found.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import fuzz_virtual_board as _fvb  # noqa: E402  (the builder of the application's virtual board: same flags, same WSL path rule)
import firmware_signing  # noqa: E402

BOOT = "src/F303-master/boot"
SOURCES = (
    "tests/virtual_bootloader.c", "tests/host_hal/fake_hal.c", f"{BOOT}/bootloader_oled.c", f"{BOOT}/bootloader_protocol.c",
    f"{BOOT}/bootloader_flash.c", f"{BOOT}/bootloader_crypto.c",
)
INCLUDES = ["tests", "tests/host_hal", BOOT, "src/common"]
FLAGS = [*_fvb.FLAGS, "-Wno-cpp", "-Wno-int-to-pointer-cast", "-Wl,--wrap=JumpToApplication"]
DEFINES: list[str] = []

# the identifiers of the update exchange (docs/CANBUS.TXT, src/F303-master/boot/bootloader_common.h)
START, DATA, PAGE_ACK, END, STATUS, HEARTBEAT, HMAC_CHUNK = 0x7F1, 0x7F2, 0x7F3, 0x7F4, 0x7F5, 0x7F6, 0x7F7
QUERY_VERSION, VERSION_RESPONSE = 0x7F8, 0x7F9
HARDWARE_ID = 0x0303CC01
APP_MAX_SIZE = 112 * 1024
STATUS_LISTENING, STATUS_ERASING, STATUS_RECEIVING, STATUS_VERIFY_OK, STATUS_VERIFY_FAIL, STATUS_VERIFYING, STATUS_COPYING = 1, 2, 3, 4, 5, 6, 7
FAIL_INCOMPLETE, FAIL_CRC32, FAIL_HMAC, FAIL_HARDWARE_ID, FAIL_ROLLBACK = 1, 2, 3, 4, 5
FRAME_LINE = re.compile(r"^t([0-7][0-9A-F]{2})([0-8])((?:[0-9A-F]{2}){0,8})$")

Frame = tuple[int, bytes]


def build(binary: Path) -> list[str] | None:
    """Compile the twin; returns the command prefix that runs it (through WSL on Windows), or None when it cannot be built."""
    inc = [f"-I{i}" for i in INCLUDES]
    if os.name == "nt":
        wsl = shutil.which("wsl")
        if wsl is None:
            return None
        resolved = binary.resolve()
        rel = resolved.relative_to(ROOT).as_posix() if str(resolved).startswith(str(ROOT)) else _fvb._wsl_path(str(binary))
        cmd = [wsl, "--cd", str(ROOT), "--", "gcc", *FLAGS, *DEFINES, *inc, "-o", rel, *SOURCES]
        if subprocess.run(cmd).returncode != 0:
            raise SystemExit("TWIN=FAIL the twin of the bootloader did not compile")
        return [wsl, "--cd", str(ROOT), "--", rel]
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc is None or sys.platform != "linux":
        return None
    cmd = [cc, *FLAGS, *DEFINES, *[f"-I{ROOT / i}" for i in INCLUDES], "-o", str(binary), *[str(ROOT / s) for s in SOURCES]]
    if subprocess.run(cmd, cwd=ROOT).returncode != 0:
        raise SystemExit("TWIN=FAIL the twin of the bootloader did not compile")
    return [str(binary)]


def slcan(can_id: int, data: bytes) -> str:
    return f"t{can_id:03X}{len(data)}{data.hex().upper()}"


def crc32(image: bytes) -> int:
    return zlib.crc32(image) & 0xFFFFFFFF


def sign(image: bytes, key: bytes | None = None) -> bytes:
    """The signature the bootloader expects: HMAC-SHA256 of the image with the key it was built with (the public development key by default)."""
    return hmac.new(key or firmware_signing.dev_key("master"), image, hashlib.sha256).digest()


def test_image(size: int) -> bytes:
    """A reproducible image whose vector table is NOT a valid one, so the twin's jump check (and the real one) refuses to leave the bootloader."""
    return bytes((i * 13 + 5) & 0xFF for i in range(size))


def minor_field(minor: int, patch: int | None) -> int:
    """The 16-bit minor field of 0x7F4: the minor number and, when known, the patch (src/common/urtc_version_wire.h)."""
    return minor if patch is None or patch > 0x7F else ((0x80 | patch) << 8) | minor


class Twin:
    """A running twin: frames in, the frames it transmits out."""

    def __init__(self, prefix: list[str]) -> None:
        self.process = subprocess.Popen(prefix, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.frames: "queue.Queue[Frame]" = queue.Queue()
        self.events: "queue.Queue[str]" = queue.Queue()
        self.log: list[Frame] = []
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _pump(self) -> None:
        assert self.process.stdout is not None
        for raw in iter(self.process.stdout.readline, b""):
            text = raw.decode("ascii", "replace").strip("\r\n")
            match = FRAME_LINE.match(text)
            if match:
                frame = (int(match.group(1), 16), bytes.fromhex(match.group(3)))
                self.log.append(frame)
                self.frames.put(frame)
            elif text in ("X", "J"):
                self.events.put(text)

    def send(self, can_id: int, data: bytes = b"") -> None:
        assert self.process.stdin is not None
        self.process.stdin.write((slcan(can_id, data) + "\n").encode("ascii"))
        self.process.stdin.flush()

    def wait(self, milliseconds: int) -> None:
        """Let that much time pass on the bootloader's clock."""
        assert self.process.stdin is not None
        self.process.stdin.write(f"W{milliseconds}\n".encode("ascii"))
        self.process.stdin.flush()

    def take(self, can_id: int | None = None, timeout: float = 5.0) -> Frame | None:
        """The next frame (of that identifier, when given; the others are skipped) or None after the timeout."""
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return None
            try:
                frame = self.frames.get(timeout=left)
            except queue.Empty:
                return None
            if can_id is None or frame[0] == can_id:
                return frame

    def event(self, timeout: float = 5.0) -> str | None:
        try:
            return self.events.get(timeout=timeout)
        except queue.Empty:
            return None

    def statuses_until(self, wanted: set[int], timeout: float = 10.0) -> list[Frame]:
        """The status frames up to (and including) the first one whose first byte is in ``wanted``."""
        seen: list[Frame] = []
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            frame = self.take(STATUS, max(0.05, end - time.monotonic()))
            if frame is None:
                break
            seen.append(frame)
            if frame[1] and frame[1][0] in wanted:
                break
        return seen

    def close(self) -> None:
        try:
            if self.process.stdin is not None:
                self.process.stdin.close()
        except OSError:
            pass
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
        for stream in (self.process.stdout, self.process.stderr):
            if stream is not None:
                stream.close()

    def __enter__(self) -> "Twin":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # ---- the update exchange, as a flashing tool plays it ------------------------------------------
    def start_update(self, size: int, hardware_id: int = HARDWARE_ID) -> list[Frame]:
        self.send(START, size.to_bytes(4, "big") + hardware_id.to_bytes(4, "big"))
        return self.statuses_until({STATUS_RECEIVING, STATUS_VERIFY_FAIL, 0xFF}, 15.0)

    def send_signature(self, signature: bytes, chunks: int = 4) -> None:
        for i in range(chunks):
            self.send(HMAC_CHUNK, signature[i * 8:(i + 1) * 8])

    def send_data(self, image: bytes) -> list[int]:
        """The image in frames of eight bytes; returns the page indices the bootloader acknowledged (a whole page each; the partial last one is flushed by END)."""
        for offset in range(0, len(image), 8):
            self.send(DATA, image[offset:offset + 8])
        acks = []
        for _ in range(len(image) // 2048):
            frame = self.take(PAGE_ACK, 5.0)
            if frame is None:
                break
            acks.append(int.from_bytes(frame[1][:4], "big"))
        return acks

    def end_update(self, crc: int, major: int, minor: int, patch: int | None = None) -> list[Frame]:
        self.send(END, crc.to_bytes(4, "big") + major.to_bytes(2, "big") + minor_field(minor, patch).to_bytes(2, "big"))
        return self.statuses_until({STATUS_VERIFY_OK, STATUS_VERIFY_FAIL, 0xFF}, 15.0)

    def installed_version(self) -> tuple[int, int, int] | None:
        """The identity the bootloader reports for the installed application: (hardware id, major, minor)."""
        self.send(QUERY_VERSION)
        frame = self.take(VERSION_RESPONSE, 5.0)
        if frame is None or len(frame[1]) != 8:
            return None
        return int.from_bytes(frame[1][1:5], "big"), int.from_bytes(frame[1][5:7], "big"), frame[1][7]


def verify_failure(statuses: list[Frame]) -> int | None:
    """The reason of a VERIFY_FAIL status among ``statuses`` (None when there is none)."""
    for _id, data in statuses:
        if len(data) == 2 and data[0] == STATUS_VERIFY_FAIL:
            return data[1]
    return None


def run_checks(prefix: list[str]) -> list[str]:
    """A signed update, a refused one and a power cut through the real bootloader; returns the problems found."""
    problems: list[str] = []
    image = test_image(3000)

    with Twin(prefix) as twin:
        if twin.take(STATUS, 4.0) is None:
            return ["the twin never said it was listening"]
        statuses = twin.start_update(len(image))
        if not statuses or statuses[-1][1][:1] != bytes([STATUS_RECEIVING]):
            problems.append(f"START did not reach RECEIVING: {statuses}")
        twin.send_signature(sign(image))
        acks = twin.send_data(image)
        if acks != [0]:
            problems.append(f"page acknowledgements {acks}, expected [0] before the end of a 3000-byte image (the partial page is flushed by END)")
        statuses = twin.end_update(crc32(image), 0, 5, 1)
        if [int.from_bytes(d[:4], "big") for i, d in twin.log if i == PAGE_ACK] != [0, 1]:
            problems.append("the partial last page was not acknowledged when the update ended")
        kinds = [f[1][0] for f in statuses if f[1]]
        if kinds[-1:] != [STATUS_VERIFY_OK] or STATUS_VERIFYING not in kinds or STATUS_COPYING not in kinds:
            problems.append(f"a good update did not end in VERIFY_OK through VERIFYING and COPYING: {kinds}")
        if twin.event(5.0) != "X":
            problems.append("a good update did not reset the chip")
        identity = None
        for _ in range(3):
            identity = twin.installed_version()
            if identity:
                break
        if identity is None or identity[0] != HARDWARE_ID or identity[1] != 0 or identity[2] != 5:
            problems.append(f"after the update the bootloader reports {identity}, expected the new application (0.5)")

    with Twin(prefix) as twin:
        twin.take(STATUS, 4.0)
        twin.start_update(len(image))
        signature = bytearray(sign(image))
        signature[31] ^= 1
        twin.send_signature(bytes(signature))
        twin.send_data(image)
        reason = verify_failure(twin.end_update(crc32(image), 0, 5, 1))
        if reason != FAIL_HMAC:
            problems.append(f"a flipped bit in the signature gave reason {reason}, expected {FAIL_HMAC}")
        if twin.installed_version() is not None and twin.installed_version()[1:] != (0, 0):
            problems.append("a refused update changed the installed identity")

    with Twin(prefix) as twin:
        twin.take(STATUS, 4.0)
        statuses = twin.start_update(len(image), HARDWARE_ID ^ 0x100)
        if verify_failure(statuses) != FAIL_HARDWARE_ID:
            problems.append(f"a foreign hardware id was not refused with reason {FAIL_HARDWARE_ID}: {statuses}")

    with Twin(prefix) as twin:
        twin.take(STATUS, 4.0)
        twin.start_update(len(image))
        twin.send_signature(sign(image))
        for offset in range(0, 800, 8):
            twin.send(DATA, image[offset:offset + 8])
        twin.wait(10_500)
        frame = twin.take(STATUS, 5.0)
        while frame is not None and frame[1][:1] != bytes([STATUS_LISTENING]):
            frame = twin.take(STATUS, 2.0)
        if frame is None:
            problems.append("ten seconds of silence in the middle of a transfer did not take the bootloader back to listening")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default=None)
    parser.add_argument("--build-only", action="store_true",
                        help="build the twin, print the command that runs it (VIRTUAL_BOARD_COMMAND=<JSON list>) and stop")
    parser.add_argument("--check", action="store_true", help="build it and run a signed update, refused ones and a power cut through it")
    args = parser.parse_args(argv)
    build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    prefix = build(build_dir / "virtual_bootloader")
    if prefix is None:
        print("TWIN=SKIPPED no host C compiler (install gcc, or WSL on Windows)")
        return 2
    if args.build_only:
        print("VIRTUAL_BOARD_COMMAND=" + json.dumps(prefix))
        return 0
    problems = run_checks(prefix)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print(f"TWIN=FAIL {len(problems)} problem(s)", file=sys.stderr)
        return 1
    print("TWIN=PASS the real bootloader took a signed update, refused a bad signature and a foreign board, and left a stalled transfer")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
