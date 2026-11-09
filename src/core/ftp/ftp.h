/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_FTP_H
#define VFTP_CORE_FTP_H

#include <stddef.h>
#include <stdint.h>

#include "core/path.h"
#include "platform/fs.h"

/*
 * Shared FTP policy. One engine, two roles (KTD2): the client and server own
 * their state machines, and this module owns the pieces both need so a fix to
 * a line cap or a remote path rule cannot drift between them.
 *
 * A control line is at most 4096 bytes. Overlong lines are discarded through
 * their terminator and reported once, so the following line still parses.
 */

#define FTP_LINE_MAX 4096
#define FTP_PATH_MAX 1024
#define FTP_REPLY_TEXT_MAX 512

typedef enum {
    FTP_CAUSE_APP = 0,
    FTP_CAUSE_SERVER,
    FTP_CAUSE_NETWORK,
    FTP_CAUSE_STORAGE,
    FTP_CAUSE_USER
} FtpCause;

typedef enum {
    FTP_STAGE_NONE = 0,
    FTP_STAGE_RESOLVE,
    FTP_STAGE_CONNECT,
    FTP_STAGE_GREETING,
    FTP_STAGE_LOGIN,
    FTP_STAGE_FIRST_LISTING,
    FTP_STAGE_COMMAND,
    FTP_STAGE_DATA,
    FTP_STAGE_TRANSFER
} FtpStage;

typedef enum {
    FTP_DATA_NONE = 0,
    FTP_DATA_ARMED,
    FTP_DATA_ACCEPTED,
    FTP_DATA_ACTIVE,
    FTP_DATA_DRAINING
} FtpDataState;

const char *ftp_cause_name(FtpCause cause);
const char *ftp_stage_name(FtpStage stage);

typedef struct {
    char line[FTP_LINE_MAX + 1];
    size_t len;
    int overflow;
    int ready;
} FtpLineReader;

typedef enum {
    FTP_LINE_NONE = 0,
    FTP_LINE_READY,
    FTP_LINE_OVERFLOW
} FtpLineResult;

void ftp_line_init(FtpLineReader *reader);
FtpLineResult ftp_line_feed(FtpLineReader *reader, char byte);

/*
 * Normalize a remote (server-side) path. `base` is the current directory and
 * `path` may be relative or absolute. Empty and `.` segments collapse, `..`
 * clamps at `/`, and the result always starts with `/`. This is not the local
 * Vita path module: a remote path is a protocol string, not a device path.
 *
 * Returns 1 on success, 0 when the result would not fit.
 */
int ftp_remote_normalize(const char *base, const char *path, char *out,
                         size_t cap);

/* A remote path used in a command must not carry a CR or LF. */
int ftp_remote_path_is_safe(const char *path);

/* Case-insensitive substring search for reply capability checks. */
int ftp_text_contains(const char *text, const char *needle);

/*
 * Templed write shared by a client download and a server upload (KTD12). The
 * target is opened as `<target>.vftp.part` in the destination directory. A
 * stale `.vftp.old` aside is restored when the target is missing and cleared
 * when both exist. Completion syncs, closes, and renames into place, using the
 * aside fallback when the platform cannot replace on rename.
 */
typedef struct {
    Path target;
    Path temp;
    Path aside;
    FsFile *file;
    int open;
} FtpTempWrite;

FsResult ftp_temp_open(FsPort *fs, const char *target, FtpTempWrite *out);
FsResult ftp_temp_finish(FsPort *fs, FtpTempWrite *temp);
void ftp_temp_abort(FsPort *fs, FtpTempWrite *temp);

#endif
