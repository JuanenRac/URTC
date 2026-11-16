# =============================================================================
# URTC - tests/test_safe_state_source.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The safe state of a stopped tool head (hardware-only helper, so it is checked in the source): it must not stop the hotend fan, which the error path of the timer interrupt
leaves running on purpose, and it must set to zero the setpoints of the output it stops, so that the state in RAM agrees with the PWM."""
from __future__ import annotations

import re
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "src" / "F303-master" / "firmware_multinode_platform.c"


def safe_state_body() -> str:
    text = SOURCE.read_text(encoding="utf-8").replace("\r\n", "\n")
    start = text.index("void MultiNode_PlatformSafeState(void)")
    body = text[start:text.index("\n}\n", start)]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return re.sub(r"//[^\n]*", "", body)


def test_the_safe_state_leaves_the_hotend_fan_running():
    assert "htim2" not in safe_state_body()


def test_the_safe_state_zeroes_the_setpoints_of_the_stopped_output():
    body = safe_state_body()
    for name in ("drill_speed", "layer_fan_duty", "uv_curing_duty", "hotair_blower_duty"):
        assert re.search(rf"\b{name}\s*=\s*0\s*;", body), name
    assert "hotend_fan_duty" not in body
