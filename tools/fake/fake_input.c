#include "fake_input.h"

#include <string.h>

static InputState s_state;
static InputPort s_port;

static void fake_poll(void *ctx, InputState *out)
{
    (void)ctx;
    *out = s_state;
    s_state.pressed = 0;
}

void fake_input_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_port.ctx = NULL;
    s_port.poll = fake_poll;
}

InputPort *fake_input_port(void)
{
    return &s_port;
}

void fake_input_set(uint32_t pressed, uint32_t held, int stick_x, int stick_y)
{
    s_state.pressed = pressed;
    s_state.held = held;
    s_state.stick_x = stick_x;
    s_state.stick_y = stick_y;
}
