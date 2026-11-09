/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/input.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static InputState state(uint32_t pressed, uint32_t held, int stick_y)
{
    InputState s;
    memset(&s, 0, sizeof(s));
    s.pressed = pressed;
    s.held = held;
    s.stick_y = stick_y;
    return s;
}

static void test_mapping(void)
{
    InputRepeat repeat;
    input_repeat_init(&repeat);
    UiInputEvent events[4];

    InputState s = state(INPUT_BIT(INPUT_CROSS), INPUT_BIT(INPUT_CROSS), 0);
    CHECK(input_repeat_poll(&repeat, &s, 0, events, 4) == 1);
    CHECK(events[0].kind == UI_INPUT_KEY);
    CHECK(events[0].key == UI_KEY_CROSS);
    CHECK(events[0].repeat == 0);

    s = state(INPUT_BIT(INPUT_L1) | INPUT_BIT(INPUT_R1),
              INPUT_BIT(INPUT_L1) | INPUT_BIT(INPUT_R1), 0);
    CHECK(input_repeat_poll(&repeat, &s, 10, events, 4) == 2);
    CHECK(events[0].key == UI_KEY_L1);
    CHECK(events[1].key == UI_KEY_R1);
}

static void test_repeat(void)
{
    InputRepeat repeat;
    input_repeat_init(&repeat);
    UiInputEvent events[4];

    InputState s = state(INPUT_BIT(INPUT_UP), INPUT_BIT(INPUT_UP), 0);
    CHECK(input_repeat_poll(&repeat, &s, 100, events, 4) == 1);
    CHECK(events[0].key == UI_KEY_UP);
    CHECK(events[0].repeat == 0);

    s = state(0, INPUT_BIT(INPUT_UP), 0);
    CHECK(input_repeat_poll(&repeat, &s, 300, events, 4) == 0);
    CHECK(input_repeat_poll(&repeat, &s, 500, events, 4) == 1);
    CHECK(events[0].repeat == 1);
    CHECK(input_repeat_poll(&repeat, &s, 600, events, 4) == 1);
    CHECK(events[0].repeat == 1);
    CHECK(input_repeat_poll(&repeat, &s, 650, events, 4) == 0);

    s = state(0, 0, 0);
    CHECK(input_repeat_poll(&repeat, &s, 700, events, 4) == 0);
}

static void test_stick(void)
{
    InputRepeat repeat;
    input_repeat_init(&repeat);
    UiInputEvent events[4];

    InputState s = state(0, 0, -100);
    CHECK(input_repeat_poll(&repeat, &s, 0, events, 4) == 1);
    CHECK(events[0].kind == UI_INPUT_STICK);
    CHECK(events[0].stick_y == -1);
    CHECK(input_repeat_poll(&repeat, &s, 50, events, 4) == 0);
    CHECK(input_repeat_poll(&repeat, &s, 100, events, 4) == 1);

    s = state(0, 0, 0);
    CHECK(input_repeat_poll(&repeat, &s, 120, events, 4) == 0);

    s = state(0, 0, 100);
    CHECK(input_repeat_poll(&repeat, &s, 200, events, 4) == 1);
    CHECK(events[0].stick_y == 1);
}

static void test_change_direction(void)
{
    InputRepeat repeat;
    input_repeat_init(&repeat);
    UiInputEvent events[4];

    InputState s = state(INPUT_BIT(INPUT_UP), INPUT_BIT(INPUT_UP), 0);
    CHECK(input_repeat_poll(&repeat, &s, 0, events, 4) == 1);
    CHECK(events[0].key == UI_KEY_UP);

    s = state(INPUT_BIT(INPUT_DOWN),
              INPUT_BIT(INPUT_UP) | INPUT_BIT(INPUT_DOWN), 0);
    CHECK(input_repeat_poll(&repeat, &s, 10, events, 4) == 1);
    CHECK(events[0].key == UI_KEY_DOWN);
    CHECK(events[0].repeat == 0);

    s = state(0, INPUT_BIT(INPUT_UP) | INPUT_BIT(INPUT_DOWN), 0);
    CHECK(input_repeat_poll(&repeat, &s, 410, events, 4) == 1);
    CHECK(events[0].key == UI_KEY_DOWN);
    CHECK(events[0].repeat == 1);
}

static void test_small_buffer(void)
{
    InputRepeat repeat;
    input_repeat_init(&repeat);
    UiInputEvent events[1];

    InputState s = state(INPUT_BIT(INPUT_CROSS) | INPUT_BIT(INPUT_SQUARE),
                         INPUT_BIT(INPUT_CROSS) | INPUT_BIT(INPUT_SQUARE), 0);
    CHECK(input_repeat_poll(&repeat, &s, 0, events, 1) == 1);
    CHECK(events[0].key == UI_KEY_CROSS);
}

int main(void)
{
    test_mapping();
    test_repeat();
    test_stick();
    test_change_direction();
    test_small_buffer();

    if (s_failures != 0) {
        fprintf(stderr, "test_input: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_input: OK\n");
    return 0;
}
