/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_PSP2_INPUT_H
#define VFTP_PLATFORM_PSP2_INPUT_H

#include "platform/input.h"

/* The Vita controller adapter. Repeat timing stays in the core reducer. */
InputPort *psp2_input_port(void);

#endif
