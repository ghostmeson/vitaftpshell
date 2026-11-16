/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_PSP2_LOG_H
#define VFTP_PLATFORM_PSP2_LOG_H

#include "platform/log.h"

/*
 * The session-log adapter. The data folder itself belongs to the worker; a
 * missing folder surfaces as LOG_ERROR here and is never created from this
 * layer.
 */
LogPort *psp2_log_port(void);

#endif
