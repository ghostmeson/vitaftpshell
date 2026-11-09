/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_PSP2_IME_H
#define VFTP_PLATFORM_PSP2_IME_H

#include "platform/ime.h"

/* The Vita system text-entry adapter: UTF-8 in, UTF-8 or cancellation out. */
TextInputPort *psp2_ime_port(void);

#endif
