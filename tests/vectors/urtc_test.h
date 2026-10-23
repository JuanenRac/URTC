// =============================================================================
// URTC-COMMON - firmware_base/urtc_test.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The assertion of the host-side tests of the firmware repositories. No test framework on purpose: the tests
// compile and run with the host's plain C compiler (never the ARM one), exercise pure logic and count failures
// in an `int *failures` that each suite receives.
//
// One definition for the repositories that carry it: they copy this file with
// `python tools/vendor_copy.py sync TARGET --set firmware_test`. Each repository keeps its own test_runner.h, which
// includes this header and declares its own suites. Header only, so no build list has to change.
#ifndef URTC_TEST_H
#define URTC_TEST_H

#include <stdio.h>

// `failures` must be the `int *` parameter of the suite function that uses the macro.
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            (*failures)++; \
        } \
    } while (0)

#endif // URTC_TEST_H
