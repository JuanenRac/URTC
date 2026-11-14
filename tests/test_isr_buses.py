# =============================================================================
# URTC - tests/test_isr_buses.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""No interrupt handler of the head talks to a bus that the main loop also uses. The I2C2 of the screen and the F-RAM, the bit-bang bus of the expansion board and the CAN
transmitter are shared with the main loop: a handler that preempts it in the middle of a transfer corrupts both. The handlers of the interrupts (the files firmware_interrupts_*.c)
therefore only set flags and counters; what talks to the buses runs from the dispatcher, which with URTC_CAN_RX_QUEUE=1 runs in the main loop, and the frames go out through the
transmit queue (URTC_CAN_TX_QUEUE=1). A new call to those buses from an interrupt file fails here."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = ROOT / "src" / "F303-master"
ISR_FILES = sorted(MASTER.glob("firmware_interrupts_*.c"))

BUS_CALL = re.compile(r"\b(FRAM_\w+|OLED_\w+|ExpansionI2C_\w+|ExpansionSPI_\w+|HAL_I2C_\w+|HAL_SPI_\w+)\s*\(")

# file -> the calls that are known and the reason. The impact event of the touch probe is sent straight to the mailboxes, on purpose: it is the one frame that must never wait
# behind the others (see firmware_interrupts_gpio.c); that is the CAN transmitter, not one of the buses above, so nothing is listed.
KNOWN: dict[str, int] = {}


def code_without_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def bus_calls_in(path: Path) -> list[str]:
    return BUS_CALL.findall(code_without_comments(path.read_text(encoding="utf-8", errors="replace")))


def test_the_files_of_the_interrupt_handlers_are_found():
    names = {path.name for path in ISR_FILES}
    assert {"firmware_interrupts_can.c", "firmware_interrupts_gpio.c", "firmware_interrupts_timer.c", "firmware_interrupts_fault.c"} <= names


def test_no_interrupt_handler_talks_to_the_screen_the_fram_or_the_expansion_bus():
    found = {path.name: len(bus_calls_in(path)) for path in ISR_FILES if bus_calls_in(path)}
    assert found == KNOWN, f"bus access inside an interrupt handler: {found}"


def test_the_pattern_finds_the_calls_it_is_meant_to_find_and_skips_comments():
    assert BUS_CALL.search("    FRAM_Write(0, buf, 4);")
    assert BUS_CALL.search("OLED_ClearPage (3);")
    assert BUS_CALL.search("ExpansionI2C_SlaveWriteRegister(1, p, 2);")
    assert BUS_CALL.search("HAL_I2C_Master_Transmit(&hi2c2, a, d, n, 5);")
    assert not BUS_CALL.search("uint32_t oled_ticks = 0;")
    assert not BUS_CALL.search("HAL_GPIO_WritePin(P, N, GPIO_PIN_SET);")
    assert code_without_comments("a(); // FRAM_Write(1);\n/* OLED_Clear(); */ b();").replace(" ", "").replace("\n", "") == "a();b();"


def test_the_two_switches_that_take_the_buses_out_of_the_interrupts_exist_and_are_off_by_default():
    rx = (MASTER / "can_rx_queue.h").read_text(encoding="utf-8")
    tx = (MASTER / "can_tx_queue.h").read_text(encoding="utf-8")
    assert re.search(r"#define URTC_CAN_RX_QUEUE 0", rx)
    assert re.search(r"#define URTC_CAN_TX_QUEUE 0", tx)
