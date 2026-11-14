// GENERATED from spec by tools/make_safety_vectors.py - do not edit by hand.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
#ifndef SAFETY_POLICY_VECTORS_H
#define SAFETY_POLICY_VECTORS_H

#include <stdint.h>

typedef struct { uint8_t feature_on, error_latched, reason; uint16_t temperature_c; uint8_t actuators_off, result; } safety_clear_case_t;
typedef struct { uint32_t steps, speed_sps, limit_ms; } safety_limit_case_t;
typedef struct { uint8_t feature_on, moving; uint32_t silence_ms, limit_ms; uint8_t stop; } safety_stop_case_t;
typedef struct { uint8_t watchdog_on, release_on, energised; uint32_t silence_ms; uint8_t release; } safety_magnet_case_t;

static const safety_clear_case_t SAFETY_CLEAR_CASES[] = {
    {0, 1, 5, 20u, 1, 2},
    {0, 0, 0, 20u, 1, 2},
    {1, 0, 0, 20u, 1, 3},
    {1, 1, 4, 20u, 1, 4},
    {1, 1, 5, 49u, 1, 1},
    {1, 1, 5, 50u, 1, 5},
    {1, 1, 5, 51u, 1, 5},
    {1, 1, 2, 0u, 1, 1},
    {1, 1, 2, 300u, 1, 5},
    {1, 1, 3, 65535u, 1, 5},
    {1, 1, 5, 20u, 0, 6},
    {1, 1, 7, 20u, 0, 6},
    {1, 1, 8, 0u, 1, 1},
    {1, 1, 6, 35u, 1, 1},
    {1, 1, 1, 20u, 1, 1},
    {1, 1, 4, 300u, 0, 4},
    {1, 1, 2, 65535u, 0, 5},
    {0, 0, 4, 65535u, 0, 2},
};

static const safety_limit_case_t SAFETY_LIMIT_CASES[] = {
    {0u, 0u, 1000u},
    {1u, 0u, 1000u},
    {100u, 500u, 1000u},
    {500u, 500u, 1000u},
    {1000u, 500u, 1000u},
    {1500u, 500u, 1500u},
    {4000u, 500u, 4000u},
    {4000u, 0u, 4000u},
    {10000u, 500u, 10000u},
    {100000u, 500u, 100000u},
    {100000u, 2000u, 25000u},
    {100000u, 1u, 50000000u},
    {4294967295u, 1u, 4294967295u},
    {4294967295u, 500u, 4294967295u},
    {4294967295u, 65535u, 32768500u},
    {7u, 3u, 1166u},
    {999u, 1000u, 1000u},
    {2000u, 1000u, 1000u},
};

static const safety_stop_case_t SAFETY_STOP_CASES[] = {
    {1, 1, 1001u, 1000u, 1},
    {1, 1, 1000u, 1000u, 0},
    {1, 1, 999u, 1000u, 0},
    {0, 1, 5000u, 1000u, 0},
    {1, 0, 5000u, 1000u, 0},
    {1, 1, 0u, 0u, 0},
    {1, 1, 1u, 0u, 1},
    {1, 1, 4294967295u, 4294967294u, 1},
    {1, 1, 4294967295u, 4294967295u, 0},
    {0, 0, 4294967295u, 0u, 0},
};

static const safety_magnet_case_t SAFETY_MAGNET_CASES[] = {
    {1, 1, 1, 5001u, 1},
    {1, 1, 1, 5000u, 0},
    {1, 1, 1, 4999u, 0},
    {1, 1, 0, 60000u, 0},
    {1, 0, 1, 60000u, 0},
    {0, 1, 1, 60000u, 0},
    {0, 0, 1, 60000u, 0},
    {1, 1, 1, 0u, 0},
    {1, 1, 1, 4294967295u, 1},
};

#define SAFETY_VEC_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

#endif // SAFETY_POLICY_VECTORS_H
