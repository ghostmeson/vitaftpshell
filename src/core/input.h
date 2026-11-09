/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_INPUT_H
#define VFTP_CORE_INPUT_H

#include <stdint.h>

#include "platform/input.h"

/*
 * Controller edges to semantic UI events. Repeat is policy and lives here, not
 * in the adapter: the adapter reports one pressed set per frame.
 */

#define INPUT_REPEAT_DELAY_MS 400
#define INPUT_REPEAT_MS 100
#define INPUT_STICK_DEADZONE 32
#define INPUT_STICK_REPEAT_MS 100

typedef enum {
    UI_KEY_NONE = 0,
    UI_KEY_UP,
    UI_KEY_DOWN,
    UI_KEY_LEFT,
    UI_KEY_RIGHT,
    UI_KEY_CROSS,
    UI_KEY_CIRCLE,
    UI_KEY_SQUARE,
    UI_KEY_TRIANGLE,
    UI_KEY_L1,
    UI_KEY_R1,
    UI_KEY_START,
    UI_KEY_SELECT
} UiKey;

typedef enum {
    UI_INPUT_KEY = 0,
    UI_INPUT_STICK
} UiInputKind;

typedef struct {
    UiInputKind kind;
    UiKey key;
    int repeat;
    int stick_y; /* -1, 0, or 1 after the deadzone */
} UiInputEvent;

typedef struct {
    UiKey held;
    uint64_t next_repeat_ms;
    uint64_t next_stick_ms;
} InputRepeat;

void input_repeat_init(InputRepeat *repeat);

/*
 * Returns the number of semantic events written. The caller should provide at
 * least two slots; a key edge and a stick step can share one poll.
 */
int input_repeat_poll(InputRepeat *repeat, const InputState *state,
                      uint64_t now_ms, UiInputEvent *out, int max);

#endif
