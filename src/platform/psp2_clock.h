/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_PSP2_CLOCK_H
#define VFTP_PLATFORM_PSP2_CLOCK_H

#include "platform/clock.h"

/* Monotonic process time and a bounded thread sleep. */
ClockPort *psp2_clock_port(void);

#endif
