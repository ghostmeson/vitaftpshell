/* SPDX-License-Identifier: GPL-3.0-only */

#include "fake_clock.h"

#include <stddef.h>

static uint64_t s_now;
static ClockPort s_port;

/* The app thread reads the clock while the worker sleeps; the singleton is
 * shared, so the time is atomic (test_app_thread under TSan). */
static uint64_t fake_now(void *ctx)
{
    (void)ctx;
    return __atomic_load_n(&s_now, __ATOMIC_ACQUIRE);
}

static void fake_sleep(void *ctx, unsigned ms)
{
    (void)ctx;
    __atomic_fetch_add(&s_now, ms, __ATOMIC_RELEASE);
}

void fake_clock_reset(void)
{
    __atomic_store_n(&s_now, 0, __ATOMIC_RELEASE);
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
    uint64_t current = __atomic_load_n(&s_now, __ATOMIC_ACQUIRE);
    while (now_ms > current &&
           !__atomic_compare_exchange_n(&s_now, &current, now_ms, 0,
                                        __ATOMIC_RELEASE,
                                        __ATOMIC_ACQUIRE)) {
    }
}

void fake_clock_advance(uint64_t delta_ms)
{
    __atomic_fetch_add(&s_now, delta_ms, __ATOMIC_RELEASE);
}
