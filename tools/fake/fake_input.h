/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_FAKE_INPUT_H
#define VFTP_FAKE_INPUT_H

#include <stdint.h>

#include "platform/input.h"

void fake_input_reset(void);
InputPort *fake_input_port(void);

/* The pressed set is returned by exactly one poll, then cleared. */
void fake_input_set(uint32_t pressed, uint32_t held, int stick_x, int stick_y);

#endif
