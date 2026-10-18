// =============================================================================
// URTC Firmware - Host logic tests for thermal_stats.c (lifetime counters)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "thermal_stats.h"

void run_thermal_stats_tests(int *failures)
{
    ThermalStats_t s;
    ThermalStatsClock_t clk;

    // Layout: the F-RAM record and the CAN answer depend on it.
    TEST_ASSERT(sizeof(ThermalStats_t) == 13, "ThermalStats_t is 13 packed bytes");

    // A blank record is valid; any corruption is detected.
    ThermalStats_Blank(&s);
    TEST_ASSERT(ThermalStats_IsValid(&s), "blank record is valid");
    s.fault_count = 7;
    TEST_ASSERT(!ThermalStats_IsValid(&s), "changing a field without resealing is detected");
    ThermalStats_Blank(&s);
    ((uint8_t *)&s)[0] ^= 0xFF;
    TEST_ASSERT(!ThermalStats_IsValid(&s), "wrong magic is rejected");

    // Heater time: interval credited to the state held during it.
    ThermalStats_Blank(&s); memset(&clk, 0, sizeof(clk));
    ThermalStats_AccumulateHeater(&s, &clk, 1000, 1);          // starts, heater on
    ThermalStats_AccumulateHeater(&s, &clk, 1600, 0);          // 600 ms on, now off
    ThermalStats_AccumulateHeater(&s, &clk, 9600, 1);          // 8 s off -> not counted
    TEST_ASSERT(ThermalStats_AccumulateHeater(&s, &clk, 10000, 1) == 1, "600+400 ms on = one whole second added");
    TEST_ASSERT(s.heater_on_seconds == 1, "off time is never counted");
    TEST_ASSERT(ThermalStats_IsValid(&s), "record stays sealed after accumulating");

    // 20 ms bang-bang for 10 minutes at 50% duty -> 300 s.
    ThermalStats_Blank(&s); memset(&clk, 0, sizeof(clk));
    for (uint32_t t = 0; t <= 600000u; t += 20u) {
        ThermalStats_AccumulateHeater(&s, &clk, t, (t / 20u) % 2u == 0u);
    }
    TEST_ASSERT(s.heater_on_seconds == 300, "50% duty for 10 min = 300 s heater on");

    // HAL tick wrap.
    ThermalStats_Blank(&s); memset(&clk, 0, sizeof(clk));
    ThermalStats_AccumulateHeater(&s, &clk, 0xFFFFFC18u, 1); // 1000 ms before the wrap
    ThermalStats_AccumulateHeater(&s, &clk, 1000u, 1);       // 2000 ms later
    TEST_ASSERT(s.heater_on_seconds == 2, "heater time is correct across the tick wrap");

    // Saturation.
    ThermalStats_Blank(&s); memset(&clk, 0, sizeof(clk));
    s.heater_on_seconds = 0xFFFFFFFEu; ThermalStats_Seal(&s);
    ThermalStats_AccumulateHeater(&s, &clk, 0, 1);
    ThermalStats_AccumulateHeater(&s, &clk, 5000, 1);
    TEST_ASSERT(s.heater_on_seconds == 0xFFFFFFFFu, "heater seconds saturate instead of wrapping");

    // Faults.
    ThermalStats_Blank(&s);
    ThermalStats_RecordFault(&s, 0);
    TEST_ASSERT(s.fault_count == 0, "code 0 is not a fault");
    ThermalStats_RecordFault(&s, 2);
    ThermalStats_RecordFault(&s, 3);
    TEST_ASSERT(s.fault_count == 2 && s.last_fault_code == 3, "faults counted, last code kept");
    s.fault_count = 0xFFFF; ThermalStats_RecordFault(&s, 1);
    TEST_ASSERT(s.fault_count == 0xFFFF, "fault count saturates");

    // CAN encoding (big-endian, like the rest of the bus).
    ThermalStats_Blank(&s);
    s.fault_count = 0x0102; s.heater_on_seconds = 0x03040506u; s.last_fault_code = 2;
    uint8_t out[8];
    ThermalStats_Encode(&s, 1, out);
    const uint8_t want[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x02, 0x01};
    TEST_ASSERT(memcmp(out, want, 8) == 0, "0x1AB payload is fault_count BE16, seconds BE32, last code, persisted");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - thermal_stats.c\n");
    run_thermal_stats_tests(&failures);
    if (failures == 0) {
        printf("  OK   all thermal stats host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
