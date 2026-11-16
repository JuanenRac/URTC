# =============================================================================
# URTC - tests/test_slave_end_refused_when_busy.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The end of an update of the expansion board is refused while a tool is active: the check of the signature record holds the main loop for most of a second. The host test
(tests/test_can_slavebridge.c) covers the behaviour of the bridge; these checks keep the wiring on the board: the application hands the bridge the probe of the actuators
at start-up, and the probe is the one the clear-error command already uses."""
from __future__ import annotations

from pathlib import Path

MASTER = Path(__file__).resolve().parents[1] / "src" / "F303-master"


def read(name: str) -> str:
    return (MASTER / name).read_text(encoding="utf-8")


def test_the_application_hands_the_bridge_the_probe_of_the_actuators() -> None:
    assert "SlaveBridge_SetActuatorProbe(Safety_ActuatorsOff);" in read("STM32F303CC_main.c")


def test_the_probe_is_the_one_of_the_clear_error_command() -> None:
    safety = read("firmware_safety.c")
    assert "uint8_t Safety_ActuatorsOff(void)" in safety
    assert "Safety_ActuatorsOff());" in safety
    assert "uint8_t Safety_ActuatorsOff(void);" in read("firmware_safety.h")


def test_the_refusal_comes_before_the_check_of_the_record() -> None:
    bridge = read("firmware_can_slavebridge.c")
    service = bridge[bridge.index("void SlaveBridge_Service(void)"):]
    assert service.index("actuators_off_probe") < service.index("SlaveAttest_Verify")
    assert "URTC_SLAVE_VERIFY_FAIL_REASON_ACTUATOR_ACTIVE" in bridge
