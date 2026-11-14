# =============================================================================
# URTC - tests/test_isr_waits.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""No code that runs inside an interrupt may wait. SysTick has the lowest priority the project gives it, so HAL_GetTick() does not advance while a higher-priority interrupt runs: a
wait on it (HAL_Delay, or a loop that spins until HAL_GetTick() has moved) inside the receive interrupt, the error interrupt or the timer interrupt never ends and the watchdog
resets the head. The files that hold the code of those interrupts are scanned for such waits; the ones that are known are listed with their reason, so that a new one fails here."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "src" / "F303-master"

# The code of these files runs in the interrupt of the CAN receiver (the dispatcher and every handler it calls), in the error interrupt or in the timer interrupt.
ISR_FILES = sorted(set(MASTER.glob("firmware_can_*.c")) | set(MASTER.glob("firmware_interrupts_*.c")) | {MASTER / "firmware_multinode.c"})

WAIT = re.compile(r"HAL_Delay\s*\(|while\s*\(\s*\(?\s*HAL_GetTick\s*\(\s*\)\s*-")

# file -> number of waits that are known. firmware_can_slavebridge.c: the update of the expansion board polls its status with HAL_Delay(10) between reads; it runs in the interrupt only
# in the build without the receive queue (URTC_CAN_RX_QUEUE=1 moves the whole dispatcher to the main loop, where the wait is harmless). A new wait anywhere else is a fault.
KNOWN = {"firmware_can_slavebridge.c": 1}


def code_without_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def waits_in(path: Path) -> int:
    return len(WAIT.findall(code_without_comments(path.read_text(encoding="utf-8", errors="replace"))))


def test_the_files_that_hold_the_code_of_the_interrupts_are_found():
    names = {path.name for path in ISR_FILES}
    assert {"firmware_can_dispatch.c", "firmware_interrupts_timer.c", "firmware_interrupts_can.c", "firmware_can_slavebridge.c"} <= names
    assert len(ISR_FILES) >= 20


def test_no_code_that_runs_in_an_interrupt_waits_except_what_is_known():
    found = {path.name: waits_in(path) for path in ISR_FILES if waits_in(path)}
    assert found == KNOWN, f"waits inside interrupt code: {found} (known: {KNOWN})"


def test_the_pattern_finds_the_waits_it_is_meant_to_find_and_skips_comments():
    assert WAIT.search("    HAL_Delay(10);")
    assert WAIT.search("while ((HAL_GetTick() - start) < 5) {}")
    assert WAIT.search("while (HAL_GetTick() - start < 5) {}")
    assert not WAIT.search("uint32_t now = HAL_GetTick();")
    assert not WAIT.search("if (HAL_GetTick() - last >= 1000) {")
    assert waits_in.__doc__ is None
    assert code_without_comments("a(); // HAL_Delay(1);\n/* HAL_Delay(2); */ b();").replace(" ", "").replace("\n", "") == "a();b();"
