/* SPDX-License-Identifier: GPL-3.0-only */

#include "platform/psp2_log.h"

#include <errno.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

/*
 * The one adapter that writes the session log. It appends one whole line, or
 * refuses it; at 256 KiB it asks the caller to rotate once. Three generations
 * are kept: session.log, session.old.log, and session.old2.log. The data folder
 * is created by the worker, so an open failure here is a normal LOG_ERROR and
 * not a reason to mkdir.
 */

#define PSP2_LOG_PATH "ux0:data/vitaFTPShell/session.log"
#define PSP2_LOG_OLD_PATH "ux0:data/vitaFTPShell/session.old.log"
#define PSP2_LOG_OLD2_PATH "ux0:data/vitaFTPShell/session.old2.log"
#define PSP2_LOG_CAP 262144u
#define PSP2_LOG_FILE_MODE 0666
#define PSP2_LOG_NEWLINE "\n"

static SceUID s_fd = -1;
static uint64_t s_size;
static int s_open;
static LogPort s_port;

static int log_error_is(int code, int error)
{
    return code == (int)(0x80010000u | (unsigned)error);
}

static LogResult log_open(void)
{
    if (s_open)
        return LOG_OK;

    SceUID fd = sceIoOpen(PSP2_LOG_PATH,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND,
                          PSP2_LOG_FILE_MODE);
    if (fd < 0)
        return LOG_ERROR;

    /* [C] sceIoLseek(SCE_SEEK_END) on an append handle is assumed to report
     * the existing file size; that is unverified on hardware. If it fails the
     * adapter refuses to write, because an untracked size could pass the
     * rotation cap. */
    SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (end < 0) {
        int rc = sceIoClose(fd);
        (void)rc;
        return LOG_ERROR;
    }

    s_fd = fd;
    s_open = 1;
    s_size = (uint64_t)end;
    return LOG_OK;
}

static LogResult log_append(void *ctx, const char *line, size_t len)
{
    (void)ctx;
    if (line == NULL)
        return LOG_ERROR; /* a caller bug, not an oversized line */

    /* A line that cannot fit even an empty file is dropped whole, never cut. */
    if ((uint64_t)len + 1u > (uint64_t)PSP2_LOG_CAP)
        return LOG_DROPPED;

    LogResult opened = log_open();
    if (opened != LOG_OK)
        return opened;

    if (s_size > 0 && s_size + (uint64_t)len + 1u > (uint64_t)PSP2_LOG_CAP)
        return LOG_ROTATE;

    size_t total = 0;
    while (total < len) {
        SceSSize wrote = sceIoWrite(s_fd, line + total,
                                    (SceSize)(len - total));
        if (wrote < 0) {
            s_size += (uint64_t)total;
            return LOG_ERROR;
        }
        if (wrote == 0) {
            s_size += (uint64_t)total;
            return LOG_ERROR;
        }
        total += (size_t)wrote;
    }

    SceSSize newline = sceIoWrite(s_fd, PSP2_LOG_NEWLINE, 1);
    if (newline != 1) {
        s_size += (uint64_t)total;
        return LOG_ERROR;
    }

    s_size += (uint64_t)total + 1u;
    return LOG_OK;
}

static LogResult log_rotate(void *ctx)
{
    (void)ctx;

    if (s_open) {
        int rc = sceIoClose(s_fd);
        s_open = 0;
        s_fd = -1;
        if (rc < 0)
            return LOG_ERROR;
    }

    SceIoStat stat;
    memset(&stat, 0, sizeof(stat));

    int old2_rc = sceIoGetstat(PSP2_LOG_OLD2_PATH, &stat);
    if (old2_rc >= 0) {
        if (sceIoRemove(PSP2_LOG_OLD2_PATH) < 0)
            return LOG_ERROR;
    } else if (!log_error_is(old2_rc, ENOENT)) {
        return LOG_ERROR;
    }

    int old_rc = sceIoGetstat(PSP2_LOG_OLD_PATH, &stat);
    if (old_rc >= 0) {
        if (sceIoRename(PSP2_LOG_OLD_PATH, PSP2_LOG_OLD2_PATH) < 0)
            return LOG_ERROR;
    } else if (!log_error_is(old_rc, ENOENT)) {
        return LOG_ERROR;
    }

    int log_rc = sceIoGetstat(PSP2_LOG_PATH, &stat);
    if (log_rc >= 0) {
        if (sceIoRename(PSP2_LOG_PATH, PSP2_LOG_OLD_PATH) < 0)
            return LOG_ERROR;
    } else if (!log_error_is(log_rc, ENOENT)) {
        return LOG_ERROR;
    }

    SceUID fd = sceIoOpen(PSP2_LOG_PATH,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                          PSP2_LOG_FILE_MODE);
    if (fd < 0)
        return LOG_ERROR;

    s_fd = fd;
    s_open = 1;
    s_size = 0;
    return LOG_OK;
}

LogPort *psp2_log_port(void)
{
    s_port.ctx = NULL;
    s_port.append = log_append;
    s_port.rotate = log_rotate;
    return &s_port;
}
