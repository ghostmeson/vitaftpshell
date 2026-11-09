/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_INPUT_H
#define VFTP_PLATFORM_INPUT_H

#include <stdint.h>

/*
 * Controller contract. The core receives one pressed-edge set, the held set,
 * and the left stick each frame. Repeat timing is policy and lives in the
 * input reducer, not here.
 */

typedef enum {
    INPUT_UP = 0,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_CROSS,
    INPUT_CIRCLE,
    INPUT_SQUARE,
    INPUT_TRIANGLE,
    INPUT_L1,
    INPUT_R1,
    INPUT_START,
    INPUT_SELECT,
    INPUT_COUNT
} InputButton;

#define INPUT_BIT(button) (1u << (unsigned)(button))

typedef struct {
    uint32_t pressed; /* edges since the previous poll */
    uint32_t held;    /* buttons down now */
    int stick_x;      /* -128..127, 0 centred */
    int stick_y;
} InputState;

typedef struct InputPort InputPort;

struct InputPort {
    void *ctx;
    void (*poll)(void *ctx, InputState *out);
};

#endif
