// =============================================================================
// URTC Firmware - Minimal host-side test harness: test_runner.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
// No external test framework, on purpose: these tests compile and run with
// the *host's* plain gcc/cc, never arm-none-eabi-gcc. They exercise the
// portable logic in src/F303-master/melexis_mlx90640/MLX90640_API.c (the
// pure C sensor API - it includes <math.h> and MLX90640_I2C_Driver.h, but
// never the STM32 HAL), against a fake I2C driver, so a real F303 board is
// not needed to run them. Final on-target timing/accuracy validation still
// needs the physical MLX9064x head - see the repo README's hardware notes.
#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            (*failures)++; \
        } \
    } while (0)

void run_mlx90640_api_tests(int *failures);

#endif // TEST_RUNNER_H
