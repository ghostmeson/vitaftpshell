/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_FAKE_CLOCK_H
#define VFTP_FAKE_CLOCK_H

#include <stdint.h>

#include "platform/clock.h"

void fake_clock_reset(void);
ClockPort *fake_clock_port(void);

/* Monotonic: a backwards set is ignored. */
void fake_clock_set_now(uint64_t now_ms);
void fake_clock_advance(uint64_t delta_ms);

#endif
