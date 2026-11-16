/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_PSP2_FS_H
#define VFTP_PLATFORM_PSP2_FS_H

#include "platform/fs.h"

/* The Vita filesystem adapter over sceIo. */
FsPort *psp2_fs_port(void);

#endif
