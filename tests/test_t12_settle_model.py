# =============================================================================
# URTC - tests/test_t12_settle_model.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""A model of the amplifier recovery after the T12 heater is switched off, and what it says about the settling time build option.

The thermocouple is read a short wait after the heater current is cut. The amplifier output recovers like a first order system: the error left in the reading after
``t`` is ``transient * exp(-t / tau)`` counts of the converter. The numbers are in tests/t12_amplifier_model.json and are estimates, not measurements; the point is the
shape: the default wait (the 10 microseconds of the first version) leaves a large error for any plausible time constant, and the maximum wait the build accepts is long
enough for all of them while staying well inside the control cycle. Evidence: simulator; the real time constant is read with an oscilloscope on the amplifier output.
"""
from __future__ import annotations

import json
import math
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODEL = json.loads((ROOT / "tests" / "t12_amplifier_model.json").read_text(encoding="utf-8"))
HEADER = (ROOT / "src" / "F303-master" / "t12_settle.h").read_text(encoding="utf-8")


def header_value(name: str) -> int:
    match = re.search(rf"#define\s+{name}\s+(\d+)u", HEADER)
    assert match, name
    return int(match.group(1))


DEFAULT_US = header_value("URTC_T12_SETTLE_US")
MAX_US = header_value("URTC_T12_SETTLE_MAX_US")


def error_c(wait_us: float, tau_us: float) -> float:
    counts = MODEL["transient_counts"] * math.exp(-wait_us / tau_us)
    return counts * MODEL["temperature_full_scale_c"] / MODEL["adc_full_scale_counts"]


def wait_needed_us(tau_us: float) -> float:
    ratio = MODEL["transient_counts"] * MODEL["temperature_full_scale_c"] / MODEL["adc_full_scale_counts"] / MODEL["accepted_error_c"]
    return tau_us * math.log(ratio)


class T12SettleModelTests(unittest.TestCase):
    def test_the_default_wait_is_the_first_versions_and_the_maximum_is_a_quarter_of_the_cycle(self) -> None:
        self.assertEqual(DEFAULT_US, 10)
        self.assertLessEqual(MAX_US, MODEL["control_cycle_ms"] * 1000 // 4)

    def test_the_default_wait_leaves_a_large_error_for_every_plausible_time_constant(self) -> None:
        for tau in MODEL["tau_us_candidates"]:
            with self.subTest(tau_us=tau):
                self.assertGreater(error_c(DEFAULT_US, tau), MODEL["accepted_error_c"], "the audit's concern: 10 microseconds samples the transient")

    def test_the_maximum_wait_brings_every_plausible_time_constant_under_the_accepted_error(self) -> None:
        for tau in MODEL["tau_us_candidates"]:
            with self.subTest(tau_us=tau):
                self.assertLess(error_c(MAX_US, tau), MODEL["accepted_error_c"])
                self.assertLessEqual(wait_needed_us(tau), MAX_US)

    def test_the_error_only_shrinks_as_the_wait_grows(self) -> None:
        for tau in MODEL["tau_us_candidates"]:
            errors = [error_c(w, tau) for w in range(0, MAX_US + 1, 50)]
            self.assertEqual(errors, sorted(errors, reverse=True))

    def test_a_wait_of_a_few_milliseconds_costs_a_small_part_of_the_cycle(self) -> None:
        cycle_us = MODEL["control_cycle_ms"] * 1000
        self.assertLessEqual(2000 / cycle_us, 0.10)
        self.assertLessEqual(MAX_US / cycle_us, 0.25)


if __name__ == "__main__":
    unittest.main()
