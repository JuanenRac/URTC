// =============================================================================
// URTC Slave Firmware - the thermal camera capture as a state machine, one small step per pass of the main loop
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// A capture used to be one blocking call: start the measurement, wait for the camera to say the frame is ready (the camera measures for up to about half
// a second at its default refresh rate), read the frame, calibrate it. All of that ran inside one pass of the main loop, so the PWM tick and the failsafe
// that stops an output when the link is lost waited for it. Here the same work is cut at its natural joints and every call of mlx_capture_step does at
// most one of them:
//
//   begin    start the measurement                                  (one short I2C2 exchange)
//   wait     ask the camera whether the frame is ready, at most once every MLX_CAPTURE_POLL_MS (the wait itself costs the loop nothing)
//   read     read the frame out of the camera                       (the library call; the longest piece that is left)
//   calibrate turn the frame into temperatures                      (the library call; arithmetic only)
//
// The camera libraries are not touched: the four joints are handed in as functions, which is also what lets the host tests drive the machine with doubles
// and count how much work one step does. Pure logic, no HAL: the caller gives the clock.
// =============================================================================
#ifndef SLAVE_MLX_CAPTURE_H
#define SLAVE_MLX_CAPTURE_H

#include <stdint.h>

// How long the camera may take to have a frame ready, from the moment the measurement was started. The slowest refresh rate of the camera is 0.5 Hz (two
// seconds per frame); a camera that is still silent after this much is reported as an error instead of waiting for ever.
#ifndef MLX_CAPTURE_READY_TIMEOUT_MS
#define MLX_CAPTURE_READY_TIMEOUT_MS 3000u
#endif
// The ready bit is looked at once per this many milliseconds, so the bus is not kept busy asking.
#ifndef MLX_CAPTURE_POLL_MS
#define MLX_CAPTURE_POLL_MS 4u
#endif

#define MLX_CAPTURE_STEP_RUNNING 0
#define MLX_CAPTURE_STEP_DONE 1
#define MLX_CAPTURE_STEP_ERROR (-1)

typedef struct {
    int (*trigger)(void);       // start the measurement; negative = the camera did not answer
    int (*data_ready)(void);    // 1 = the frame is ready, 0 = not yet, negative = the camera did not answer
    int (*read_frame)(void);    // read the frame; negative = it could not be read or did not validate
    void (*calibrate)(void);    // calibrate the frame that was read
} MlxCaptureOps;

typedef enum { MLXCAP_IDLE = 0, MLXCAP_WAIT_READY, MLXCAP_READ, MLXCAP_CALIBRATE } MlxCaptureState;

typedef struct {
    MlxCaptureState state;
    uint32_t started_ms;
    uint32_t last_poll_ms;
} MlxCapture;

static inline int mlx_capture_active(const MlxCapture *capture)
{
    return capture->state != MLXCAP_IDLE;
}

// Drops a capture that is in flight (the master chose another camera, or the camera is initialised again).
static inline void mlx_capture_abort(MlxCapture *capture)
{
    capture->state = MLXCAP_IDLE;
}

// Starts a capture: the measurement is triggered at once. MLX_CAPTURE_STEP_RUNNING when the camera took it, MLX_CAPTURE_STEP_ERROR when it did not.
static inline int mlx_capture_begin(MlxCapture *capture, const MlxCaptureOps *ops, uint32_t now_ms)
{
    capture->state = MLXCAP_IDLE;
    if (ops->trigger() < 0) return MLX_CAPTURE_STEP_ERROR;
    capture->state = MLXCAP_WAIT_READY;
    capture->started_ms = now_ms;
    capture->last_poll_ms = now_ms;
    return MLX_CAPTURE_STEP_RUNNING;
}

// One small step of a capture that is in flight. RUNNING = call again on a later pass; DONE = the calibrated frame is ready; ERROR = the capture failed
// (the machine is idle again either way). With nothing in flight it does nothing and reports ERROR, which the caller never reaches (it checks first).
static inline int mlx_capture_step(MlxCapture *capture, const MlxCaptureOps *ops, uint32_t now_ms)
{
    switch (capture->state) {
    case MLXCAP_WAIT_READY: {
        if ((uint32_t)(now_ms - capture->started_ms) > MLX_CAPTURE_READY_TIMEOUT_MS) {
            capture->state = MLXCAP_IDLE;
            return MLX_CAPTURE_STEP_ERROR;
        }
        if ((uint32_t)(now_ms - capture->last_poll_ms) < MLX_CAPTURE_POLL_MS) return MLX_CAPTURE_STEP_RUNNING;
        capture->last_poll_ms = now_ms;
        int ready = ops->data_ready();
        if (ready < 0) {
            capture->state = MLXCAP_IDLE;
            return MLX_CAPTURE_STEP_ERROR;
        }
        if (ready > 0) capture->state = MLXCAP_READ;
        return MLX_CAPTURE_STEP_RUNNING;
    }
    case MLXCAP_READ:
        if (ops->read_frame() < 0) {
            capture->state = MLXCAP_IDLE;
            return MLX_CAPTURE_STEP_ERROR;
        }
        capture->state = MLXCAP_CALIBRATE;
        return MLX_CAPTURE_STEP_RUNNING;
    case MLXCAP_CALIBRATE:
        ops->calibrate();
        capture->state = MLXCAP_IDLE;
        return MLX_CAPTURE_STEP_DONE;
    case MLXCAP_IDLE:
    default:
        return MLX_CAPTURE_STEP_ERROR;
    }
}

#endif // SLAVE_MLX_CAPTURE_H
