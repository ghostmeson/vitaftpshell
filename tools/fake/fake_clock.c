#include "fake_clock.h"

#include <stddef.h>

static uint64_t s_now;
static ClockPort s_port;

static uint64_t fake_now(void *ctx)
{
    (void)ctx;
    return s_now;
}

static void fake_sleep(void *ctx, unsigned ms)
{
    (void)ctx;
    s_now += ms;
}

void fake_clock_reset(void)
{
    s_now = 0;
    s_port.ctx = NULL;
    s_port.now_ms = fake_now;
    s_port.sleep_ms = fake_sleep;
}

ClockPort *fake_clock_port(void)
{
    return &s_port;
}

void fake_clock_set_now(uint64_t now_ms)
{
    if (now_ms > s_now)
        s_now = now_ms;
}

void fake_clock_advance(uint64_t delta_ms)
{
    s_now += delta_ms;
}
