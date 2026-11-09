/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/input.h"

#include <string.h>

typedef struct {
    InputButton button;
    UiKey key;
    int direction;
} KeyMap;

static const KeyMap KEYS[] = {
    {INPUT_UP, UI_KEY_UP, 1},       {INPUT_DOWN, UI_KEY_DOWN, 1},
    {INPUT_LEFT, UI_KEY_LEFT, 1},   {INPUT_RIGHT, UI_KEY_RIGHT, 1},
    {INPUT_CROSS, UI_KEY_CROSS, 0}, {INPUT_CIRCLE, UI_KEY_CIRCLE, 0},
    {INPUT_SQUARE, UI_KEY_SQUARE, 0}, {INPUT_TRIANGLE, UI_KEY_TRIANGLE, 0},
    {INPUT_L1, UI_KEY_L1, 0},       {INPUT_R1, UI_KEY_R1, 0},
    {INPUT_START, UI_KEY_START, 0}, {INPUT_SELECT, UI_KEY_SELECT, 0},
};

#define KEY_COUNT (int)(sizeof(KEYS) / sizeof(KEYS[0]))

static uint32_t button_bit(UiKey key)
{
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (KEYS[i].key == key)
            return INPUT_BIT(KEYS[i].button);
    }
    return 0;
}

void input_repeat_init(InputRepeat *repeat)
{
    if (repeat == NULL)
        return;
    memset(repeat, 0, sizeof(*repeat));
    repeat->held = UI_KEY_NONE;
}

static int push(UiInputEvent *out, int max, int *count, UiInputKind kind,
                UiKey key, int repeat, int stick_y)
{
    if (*count >= max)
        return 0;
    UiInputEvent *event = &out[*count];
    event->kind = kind;
    event->key = key;
    event->repeat = repeat;
    event->stick_y = stick_y;
    (*count)++;
    return 1;
}

int input_repeat_poll(InputRepeat *repeat, const InputState *state,
                      uint64_t now_ms, UiInputEvent *out, int max)
{
    if (repeat == NULL || state == NULL || out == NULL || max <= 0)
        return 0;

    int count = 0;
    int pressed_direction = 0;

    for (int i = 0; i < KEY_COUNT; ++i) {
        uint32_t bit = INPUT_BIT(KEYS[i].button);
        if ((state->pressed & bit) == 0)
            continue;

        push(out, max, &count, UI_INPUT_KEY, KEYS[i].key, 0, 0);
        if (KEYS[i].direction) {
            repeat->held = KEYS[i].key;
            repeat->next_repeat_ms = now_ms + INPUT_REPEAT_DELAY_MS;
            pressed_direction = 1;
        }
    }

    if (repeat->held != UI_KEY_NONE && !pressed_direction) {
        uint32_t bit = button_bit(repeat->held);
        if (bit != 0 && (state->held & bit) == 0) {
            repeat->held = UI_KEY_NONE;
        } else if (bit != 0 && now_ms >= repeat->next_repeat_ms) {
            push(out, max, &count, UI_INPUT_KEY, repeat->held, 1, 0);
            repeat->next_repeat_ms = now_ms + INPUT_REPEAT_MS;
        }
    }

    int stick = state->stick_y;
    if (stick < -INPUT_STICK_DEADZONE)
        stick = -1;
    else if (stick > INPUT_STICK_DEADZONE)
        stick = 1;
    else
        stick = 0;

    if (stick != 0 && now_ms >= repeat->next_stick_ms) {
        push(out, max, &count, UI_INPUT_STICK, UI_KEY_NONE, 0, stick);
        repeat->next_stick_ms = now_ms + INPUT_STICK_REPEAT_MS;
    } else if (stick == 0) {
        repeat->next_stick_ms = 0;
    }

    return count;
}
