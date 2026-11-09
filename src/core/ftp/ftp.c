/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/ftp/ftp.h"

#include <stdio.h>
#include <string.h>

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

const char *ftp_cause_name(FtpCause cause)
{
    switch (cause) {
    case FTP_CAUSE_SERVER:
        return "server";
    case FTP_CAUSE_NETWORK:
        return "network";
    case FTP_CAUSE_STORAGE:
        return "storage";
    case FTP_CAUSE_USER:
        return "user";
    case FTP_CAUSE_APP:
    default:
        return "app";
    }
}

const char *ftp_stage_name(FtpStage stage)
{
    switch (stage) {
    case FTP_STAGE_RESOLVE:
        return "resolve";
    case FTP_STAGE_CONNECT:
        return "connect";
    case FTP_STAGE_GREETING:
        return "greeting";
    case FTP_STAGE_LOGIN:
        return "login";
    case FTP_STAGE_FIRST_LISTING:
        return "first_listing";
    case FTP_STAGE_COMMAND:
        return "command";
    case FTP_STAGE_DATA:
        return "data";
    case FTP_STAGE_TRANSFER:
        return "transfer";
    case FTP_STAGE_NONE:
    default:
        return "none";
    }
}

void ftp_line_init(FtpLineReader *reader)
{
    if (reader == NULL)
        return;
    memset(reader, 0, sizeof(*reader));
}

FtpLineResult ftp_line_feed(FtpLineReader *reader, char byte)
{
    if (reader == NULL)
        return FTP_LINE_NONE;

    if (reader->ready) {
        reader->ready = 0;
        reader->len = 0;
    }

    if (byte == '\n') {
        if (reader->overflow) {
            reader->overflow = 0;
            reader->len = 0;
            return FTP_LINE_OVERFLOW;
        }
        if (reader->len > 0 && reader->line[reader->len - 1] == '\r')
            reader->len--;
        reader->line[reader->len] = '\0';
        reader->ready = 1;
        return FTP_LINE_READY;
    }

    if (byte == '\r')
        return FTP_LINE_NONE;

    if (reader->len >= FTP_LINE_MAX) {
        reader->overflow = 1;
        return FTP_LINE_NONE;
    }

    reader->line[reader->len++] = byte;
    return FTP_LINE_NONE;
}

int ftp_remote_normalize(const char *base, const char *path, char *out,
                         size_t cap)
{
    if (out == NULL || cap == 0)
        return 0;

    char joined[FTP_PATH_MAX * 2];
    const char *relative = path != NULL ? path : "";

    if (relative[0] == '/') {
        if (strlen(relative) >= sizeof(joined))
            return 0;
        strcpy(joined, relative);
    } else {
        const char *root = (base != NULL && base[0] == '/') ? base : "/";
        int n = snprintf(joined, sizeof(joined), "%s/%s", root, relative);
        if (n < 0 || (size_t)n >= sizeof(joined))
            return 0;
    }

    char result[FTP_PATH_MAX];
    size_t used = 0;
    size_t i = 0;

    while (joined[i] != '\0') {
        while (joined[i] == '/')
            i++;
        if (joined[i] == '\0')
            break;

        size_t start = i;
        while (joined[i] != '\0' && joined[i] != '/')
            i++;
        size_t len = i - start;

        if (len == 1 && joined[start] == '.')
            continue;

        if (len == 2 && joined[start] == '.' && joined[start + 1] == '.') {
            if (used > 0) {
                while (used > 0 && result[used - 1] != '/')
                    used--;
                if (used > 0)
                    used--;
            }
            continue;
        }

        if (used + 1 + len >= sizeof(result))
            return 0;
        result[used++] = '/';
        memcpy(result + used, joined + start, len);
        used += len;
    }

    if (used == 0)
        result[used++] = '/';
    result[used] = '\0';

    if (used + 1 > cap)
        return 0;
    memcpy(out, result, used + 1);
    return 1;
}

int ftp_remote_path_is_safe(const char *path)
{
    if (path == NULL)
        return 0;
    size_t len = strlen(path);
    if (len >= FTP_PATH_MAX)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '\r' || path[i] == '\n' || path[i] == '\0')
            return 0;
    }
    return 1;
}

int ftp_text_contains(const char *text, const char *needle)
{
    if (text == NULL || needle == NULL || needle[0] == '\0')
        return 0;

    size_t n = strlen(needle);
    for (const char *p = text; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < n && p[i] != '\0' &&
               lower_ascii(p[i]) == lower_ascii(needle[i]))
            i++;
        if (i == n)
            return 1;
    }
    return 0;
}

static int build_sidecar(char *out, size_t cap, const char *base,
                         const char *suffix)
{
    int n = snprintf(out, cap, "%s%s", base, suffix);
    return n > 0 && (size_t)n < cap;
}

static void recover(FsPort *fs, const char *target, const char *aside)
{
    FsEntry entry;
    int target_exists = fs->stat(fs->ctx, target, &entry) == FS_OK;
    int aside_exists = fs->stat(fs->ctx, aside, &entry) == FS_OK;

    if (!target_exists && aside_exists)
        fs->rename(fs->ctx, aside, target);
    else if (target_exists && aside_exists)
        fs->remove(fs->ctx, aside);
}

FsResult ftp_temp_open(FsPort *fs, const char *target, FtpTempWrite *out)
{
    if (fs == NULL || target == NULL || out == NULL)
        return FS_INVALID;

    memset(out, 0, sizeof(*out));
    if (path_canonicalize(target, &out->target) != PATH_OK)
        return FS_INVALID;

    /* A destination directory is never replaced by a file. */
    FsEntry existing;
    if (fs->stat(fs->ctx, out->target.text, &existing) == FS_OK &&
        existing.type == FS_ENTRY_DIR)
        return FS_INVALID;

    if (!build_sidecar(out->temp.text, sizeof(out->temp.text),
                       out->target.text, ".vftp.part") ||
        !build_sidecar(out->aside.text, sizeof(out->aside.text),
                       out->target.text, ".vftp.old"))
        return FS_INVALID;

    recover(fs, out->target.text, out->aside.text);
    fs->remove(fs->ctx, out->temp.text);

    FsResult r = fs->open(fs->ctx, out->temp.text, FS_OPEN_WRITE_TRUNC,
                          &out->file);
    if (r != FS_OK) {
        out->file = NULL;
        out->open = 0;
        return r;
    }
    out->open = 1;
    return FS_OK;
}

/* The rename makes the new file visible; sync the file itself before the
 * transfer is reported complete. Directory sync is a separate, deferred gap. */
static FsResult sync_destination(FsPort *fs, const char *target)
{
    FsFile *file = NULL;
    FsResult r = fs->open(fs->ctx, target, FS_OPEN_READ, &file);
    if (r != FS_OK)
        return r;
    FsResult sync = fs->sync(fs->ctx, file);
    FsResult close = fs->close(fs->ctx, file);
    if (sync != FS_OK)
        return sync;
    return close;
}

FsResult ftp_temp_finish(FsPort *fs, FtpTempWrite *temp)
{
    if (fs == NULL || temp == NULL || !temp->open || temp->file == NULL)
        return FS_INVALID;

    FsResult r = fs->sync(fs->ctx, temp->file);
    FsResult closed = fs->close(fs->ctx, temp->file);
    temp->file = NULL;
    temp->open = 0;

    if (r != FS_OK || closed != FS_OK) {
        fs->remove(fs->ctx, temp->temp.text);
        return r != FS_OK ? r : closed;
    }

    if (fs->rename_replaces(fs->ctx)) {
        r = fs->rename(fs->ctx, temp->temp.text, temp->target.text);
        if (r != FS_OK) {
            fs->remove(fs->ctx, temp->temp.text);
            return r;
        }
        return sync_destination(fs, temp->target.text);
    }

    FsEntry entry;
    int target_exists = fs->stat(fs->ctx, temp->target.text, &entry) == FS_OK;
    if (!target_exists) {
        r = fs->rename(fs->ctx, temp->temp.text, temp->target.text);
        if (r != FS_OK) {
            fs->remove(fs->ctx, temp->temp.text);
            return r;
        }
        return sync_destination(fs, temp->target.text);
    }

    if (fs->rename(fs->ctx, temp->target.text, temp->aside.text) != FS_OK) {
        fs->remove(fs->ctx, temp->temp.text);
        return FS_IO;
    }

    r = fs->rename(fs->ctx, temp->temp.text, temp->target.text);
    if (r != FS_OK) {
        fs->rename(fs->ctx, temp->aside.text, temp->target.text);
        fs->remove(fs->ctx, temp->temp.text);
        return r;
    }

    r = sync_destination(fs, temp->target.text);
    if (r != FS_OK) {
        /* The old file is still in the aside; put it back rather than leave a
         * new file whose contents may not have reached the card. */
        fs->remove(fs->ctx, temp->target.text);
        fs->rename(fs->ctx, temp->aside.text, temp->target.text);
        return r;
    }

    fs->remove(fs->ctx, temp->aside.text);
    return FS_OK;
}

void ftp_temp_abort(FsPort *fs, FtpTempWrite *temp)
{
    if (fs == NULL || temp == NULL)
        return;
    if (temp->file != NULL)
        fs->close(fs->ctx, temp->file);
    if (temp->open)
        fs->remove(fs->ctx, temp->temp.text);
    temp->file = NULL;
    temp->open = 0;
}
