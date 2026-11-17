/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_PSP2_FS_H
#define VFTP_PLATFORM_PSP2_FS_H

#include "platform/fs.h"

/* The Vita filesystem adapter over sceIo. */
FsPort *psp2_fs_port(void);
/*
 * The install-tree path the local `app0:` token denotes. The adapter owns it
 * because it is an unverified platform fact; the composition root hands it to
 * the path module before the worker starts.
 */
const char *psp2_fs_install_alias(void);

#endif
