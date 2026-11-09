/* SPDX-License-Identifier: GPL-3.0-only */

#include "platform/psp2_clock.h"

#include <stdint.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

/*
 * The one adapter that reads time and sleeps. The process clock is monotonic
 * for the lifetime of the app, which is exactly the budget's unit.
 */

#define PSP2_CLOCK_US_PER_MS 1000u
#define PSP2_CLOCK_MAX_US 0xFFFFFFFFu

static uint64_t clock_now_ms(void *ctx)
{
    (void)ctx;
    return (uint64_t)(sceKernelGetProcessTimeWide() / PSP2_CLOCK_US_PER_MS);
}

static void clock_sleep_ms(void *ctx, unsigned ms)
{
    (void)ctx;
    if (ms == 0)
        return;

    /* A single SceUInt microsecond argument saturates rather than wrapping; a
     * caller asking for more than ~71 minutes gets the longest representable
     * sleep and re-reads the clock afterwards. */
    uint64_t usec = (uint64_t)ms * PSP2_CLOCK_US_PER_MS;
    if (usec > PSP2_CLOCK_MAX_US)
        usec = PSP2_CLOCK_MAX_US;

    int rc = sceKernelDelayThread((SceUInt)usec);
    (void)rc; /* the contract has no error channel; the caller re-reads time */
}

ClockPort *psp2_clock_port(void)
{
    static ClockPort port;
    port.ctx = NULL;
    port.now_ms = clock_now_ms;
    port.sleep_ms = clock_sleep_ms;
    return &port;
}
