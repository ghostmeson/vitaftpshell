/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_PSP2_NET_H
#define VFTP_PLATFORM_PSP2_NET_H

#include "platform/net.h"

/*
 * The Vita network adapter. Construct it once from the composition root; the
 * stack itself starts on first use, so a missing module or a failed init is a
 * runtime result for the engine, not a startup abort.
 */
NetPort *psp2_net_port(void);

#endif
