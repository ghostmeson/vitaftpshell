/* SPDX-License-Identifier: GPL-3.0-only */

#include "platform/psp2_input.h"

#include <string.h>

#include <psp2/ctrl.h>

/*
 * The one adapter that reads the controller. It reports the raw edge and the
 * held set; repeat timing is the reducer's business and does not appear here.
 */

#define PSP2_INPUT_CENTRE 128
#define PSP2_INPUT_STICK_MIN (-128)
#define PSP2_INPUT_STICK_MAX 127

typedef struct {
    unsigned bit;
    InputButton button;
} InputButtonMap;

static const InputButtonMap s_buttons[] = {
    /* [C] Under the plain sceCtrlPeekBufferPositive call the Vita shoulder
     * buttons are reported as LTRIGGER/RTRIGGER; only the Ext2 variants name
     * them L1/R1 explicitly. This mapping is unverified on hardware. */
    { SCE_CTRL_UP, INPUT_UP },
    { SCE_CTRL_DOWN, INPUT_DOWN },
    { SCE_CTRL_LEFT, INPUT_LEFT },
    { SCE_CTRL_RIGHT, INPUT_RIGHT },
    { SCE_CTRL_CROSS, INPUT_CROSS },
    { SCE_CTRL_CIRCLE, INPUT_CIRCLE },
    { SCE_CTRL_SQUARE, INPUT_SQUARE },
    { SCE_CTRL_TRIANGLE, INPUT_TRIANGLE },
    { SCE_CTRL_LTRIGGER, INPUT_L1 },
    { SCE_CTRL_RTRIGGER, INPUT_R1 },
    { SCE_CTRL_START, INPUT_START },
    { SCE_CTRL_SELECT, INPUT_SELECT },
};

static unsigned s_previous;
static int s_sampling_set;
static InputPort s_port;

static int clamp_stick(int value)
{
    if (value < PSP2_INPUT_STICK_MIN)
        return PSP2_INPUT_STICK_MIN;
    if (value > PSP2_INPUT_STICK_MAX)
        return PSP2_INPUT_STICK_MAX;
    return value;
}

static void input_poll(void *ctx, InputState *out)
{
    (void)ctx;
    if (out == NULL)
        return;

    memset(out, 0, sizeof(*out));

    if (!s_sampling_set) {
        /* [C] SCE_CTRL_MODE_ANALOG_WIDE is assumed to report lx/ly in 0..255
         * centred at 128; the wide range is unverified on hardware. A failed
         * mode set is not fatal: the default sampling mode still reports
         * buttons and a stick, just with the narrower range. */
        int rc = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
        s_sampling_set = 1;
        (void)rc;
    }

    SceCtrlData data;
    memset(&data, 0, sizeof(data));

    if (sceCtrlPeekBufferPositive(0, &data, 1) < 0) {
        /* A read failure is not "all buttons up": report no edges and a
         * centred stick for this frame, and forget the previous set so a stale
         * edge cannot fire when the controller returns. */
        s_previous = 0;
        return;
    }

    unsigned held = 0;
    for (unsigned i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        if ((data.buttons & s_buttons[i].bit) != 0)
            held |= INPUT_BIT(s_buttons[i].button);
    }

    out->pressed = held & ~s_previous;
    out->held = held;
    s_previous = held;

    out->stick_x = clamp_stick((int)data.lx - PSP2_INPUT_CENTRE);
    out->stick_y = clamp_stick((int)data.ly - PSP2_INPUT_CENTRE);
}

InputPort *psp2_input_port(void)
{
    s_port.ctx = NULL;
    s_port.poll = input_poll;
    return &s_port;
}
