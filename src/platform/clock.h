/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_CLOCK_H
#define VFTP_PLATFORM_CLOCK_H

#include <stdint.h>

/* Monotonic milliseconds since the process started, and a bounded sleep. */
typedef struct ClockPort ClockPort;

struct ClockPort {
    void *ctx;
    uint64_t (*now_ms)(void *ctx);
    void (*sleep_ms)(void *ctx, unsigned ms);
};

#endif
