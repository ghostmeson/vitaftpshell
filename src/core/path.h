/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_PATH_H
#define VFTP_CORE_PATH_H

#include <stddef.h>

/*
 * One path module for the client, the server, and the local manager.
 *
 * Canonical form: lowercase volume prefix, no leading slash after the colon,
 * no `.` or empty segments, collapsed separators, and `..` clamped at the
 * volume root. A backslash is refused for every role, so one spelling the
 * platform might fold cannot bypass a denial. Segment bytes are preserved and
 * passed to the filesystem as written; the denial and risk tables compare
 * case-insensitively, because the exposed volumes are case-insensitive and a
 * case variant must not slip past.
 */

#define PATH_MAX_LEN 1024
#define PATH_NAME_MAX 256

typedef enum {
    VOLUME_UX0 = 0,
    VOLUME_UR0,
    VOLUME_UMA0,
    VOLUME_COUNT
} Volume;

typedef enum {
    PATH_OK = 0,
    PATH_INVALID,
    PATH_DENIED,
    PATH_PROTECTED
} PathResult;

typedef enum {
    PATH_RISK_NORMAL = 0,
    PATH_RISK_HIGH
} PathRisk;

typedef struct {
    char text[PATH_MAX_LEN];
} Path;

PathResult path_canonicalize(const char *input, Path *out);
PathResult path_join(const char *base, const char *name, Path *out);
PathResult path_parent(const char *path, Path *out);

int path_is_volume_root(const char *path);
int path_volume(const char *path, Volume *out);
const char *path_volume_name(Volume volume);

/* Server reads deny boot_config.txt and the app data folder; the tai folders
 * remain readable. The local manager may read boot_config.txt. */
PathResult path_check_read(const char *path, int server_role, PathRisk *risk);
/* Write/create at `path` (files and directories). */
PathResult path_check_write(const char *path, int server_role, PathRisk *risk);
/* Delete at `path`; protected roots and directories are refused. */
PathResult path_check_delete(const char *path, int server_role, PathRisk *risk);
/* Rename checks both operands. */
PathResult path_check_rename(const char *from, const char *to, int server_role,
                             PathRisk *risk);

int path_is_protected(const char *path);
int path_is_high_risk(const char *path);

/* A single component from a remote listing: no `/`, `\`, `:`, or `..`. */
int path_name_is_safe(const char *name);

#endif
