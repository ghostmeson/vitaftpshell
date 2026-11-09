/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_STORE_H
#define VFTP_CORE_STORE_H

#include "core/settings.h"
#include "core/site.h"
#include "platform/fs.h"

/*
 * Persistence policy. Two line-oriented text files with a version header and
 * percent-encoded fields. A save writes a temporary file in the destination
 * directory, syncs it, closes it, and renames it into place. A failed save
 * leaves the previous file byte-identical.
 */

#define STORE_DIR "ux0:data/vitaFTPShell"
#define STORE_SITES_NAME "sites.cfg"
#define STORE_SETTINGS_NAME "settings.cfg"

typedef enum {
    STORE_OK = 0,
    STORE_NOT_FOUND,
    STORE_BAD_VERSION,
    STORE_PARTIAL,
    STORE_IO,
    STORE_NO_SPACE
} StoreResult;

StoreResult store_load_sites(FsPort *fs, const char *dir, SiteBook *book);
StoreResult store_save_sites(FsPort *fs, const char *dir, const SiteBook *book);

StoreResult store_load_settings(FsPort *fs, const char *dir,
                                Settings *settings);
StoreResult store_save_settings(FsPort *fs, const char *dir,
                                const Settings *settings);

#endif
