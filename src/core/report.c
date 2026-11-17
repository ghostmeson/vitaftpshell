/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/report.h"

#include <stdio.h>
#include <string.h>

#define REPORT_HEADER "vitaFTPShell transfer report"
#define REPORT_COMPLETE "transfer complete - nothing is missing"
#define REPORT_STOPPED "transfer stopped before it completed"
#define REPORT_PENDING_DEFAULT "NOT ATTEMPTED"
#define REPORT_UNVERIFIED "NOT VERIFIED"
#define REPORT_ESCAPE_CHUNK 128

static size_t text_len(const char *text)
{
    size_t len = 0;
    if (text == NULL)
        return 0;
    while (text[len] != '\0')
        len++;
    return len;
}

/* `target` plus a short suffix, or 0 when the result would not fit. */
static int sidecar_path(char *out, size_t cap, const char *target,
                        const char *suffix)
{
    size_t t = text_len(target);
    size_t s = text_len(suffix);
    if (t + s >= cap)
        return 0;
    memcpy(out, target, t);
    memcpy(out + t, suffix, s + 1);
    return 1;
}

/*
 * An interrupted fallback can leave the only complete report under `.old`.
 * Before a new write touches the target, restore that copy when the target is
 * missing; when both exist, the target is at least as new (the store's rule).
 */
static void recover_target(FsPort *fs, const char *target, const char *aside)
{
    FsEntry entry;
    int target_exists = fs->stat(fs->ctx, target, &entry) == FS_OK;
    int aside_exists = fs->stat(fs->ctx, aside, &entry) == FS_OK;

    if (!target_exists && aside_exists)
        fs->rename(fs->ctx, aside, target);
    else if (target_exists && aside_exists)
        fs->remove(fs->ctx, aside);
}

static XferReportResult replace_file(FsPort *fs, const char *temp,
                                     const char *target)
{
    FsEntry entry;
    /* R22: a directory is never replaced, whatever its name. */
    if (fs->stat(fs->ctx, target, &entry) == FS_OK &&
        entry.type == FS_ENTRY_DIR) {
        fs->remove(fs->ctx, temp);
        return XFER_REPORT_REFUSED;
    }

    char aside[PATH_MAX_LEN];
    if (!sidecar_path(aside, sizeof(aside), target, ".old")) {
        fs->remove(fs->ctx, temp);
        return XFER_REPORT_FAILED;
    }
    recover_target(fs, target, aside);

    if (fs->rename_replaces(fs->ctx)) {
        if (fs->rename(fs->ctx, temp, target) == FS_OK)
            return XFER_REPORT_OK;
        fs->remove(fs->ctx, temp);
        return XFER_REPORT_FAILED;
    }

    int target_exists = fs->stat(fs->ctx, target, &entry) == FS_OK;
    if (!target_exists) {
        if (fs->rename(fs->ctx, temp, target) == FS_OK)
            return XFER_REPORT_OK;
        fs->remove(fs->ctx, temp);
        return XFER_REPORT_FAILED;
    }

    if (fs->rename(fs->ctx, target, aside) != FS_OK) {
        fs->remove(fs->ctx, temp);
        return XFER_REPORT_FAILED;
    }
    if (fs->rename(fs->ctx, temp, target) != FS_OK) {
        fs->rename(fs->ctx, aside, target);
        fs->remove(fs->ctx, temp);
        return XFER_REPORT_FAILED;
    }
    fs->remove(fs->ctx, aside);
    return XFER_REPORT_OK;
}

/* 1 when the whole buffer was written, 0 on an I/O or space failure. */
static int write_all(FsPort *fs, FsFile *file, const char *data, size_t len)
{
    size_t total = 0;
    while (total < len) {
        size_t written = 0;
        if (fs->write(fs->ctx, file, data + total, len - total, &written) !=
                FS_OK ||
            written == 0)
            return 0;
        total += written;
    }
    return 1;
}

static int write_text(FsPort *fs, FsFile *file, const char *text)
{
    return text != NULL && text[0] != '\0'
               ? write_all(fs, file, text, text_len(text))
               : 1;
}

/* Stream an escaped string without a path-sized buffer. */
static int write_escaped(FsPort *fs, FsFile *file, const char *text)
{
    char chunk[REPORT_ESCAPE_CHUNK];
    size_t used = 0;
    for (const char *p = text != NULL ? text : ""; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)*p;
        size_t cost = (c < 0x20 || c == 0x7f) ? 4u : 1u;
        if (used + cost > sizeof(chunk)) {
            if (!write_all(fs, file, chunk, used))
                return 0;
            used = 0;
        }
        if (cost == 4) {
            static const char hex[] = "0123456789ABCDEF";
            chunk[used++] = '\\';
            chunk[used++] = 'x';
            chunk[used++] = hex[c >> 4];
            chunk[used++] = hex[c & 0x0fu];
        } else {
            chunk[used++] = (char)c;
        }
    }
    return used == 0 || write_all(fs, file, chunk, used);
}

static int entry_missing(const XferPlanEntry *entry)
{
    /* A completed folder copy carries no status of its own: the files it
     * holds are the entries that can prove a miss. A pending folder row
     * would claim work that did happen, so only files are listed that way.
     * A skipped or unverified folder is still a real missing record. */
    if (entry->status == XFER_STATUS_PENDING)
        return entry->type == XFER_ENTRY_FILE;
    return entry->status == XFER_STATUS_SKIPPED ||
           entry->status == XFER_STATUS_UNVERIFIED;
}

static const char *skip_reason(const XferPlanEntry *entry)
{
    if (entry->status == XFER_STATUS_UNVERIFIED)
        return REPORT_UNVERIFIED;
    if (entry->status == XFER_STATUS_SKIPPED)
        return xfer_skip_reason_text((XferSkipReason)entry->skip_reason);
    return REPORT_PENDING_DEFAULT;
}

/* The local side of an entry: a download's destination, an upload's source. */
static const char *entry_local(const XferPlan *plan, int entry)
{
    return plan->direction == XFER_PLAN_UPLOAD
               ? xfer_plan_source(plan, entry)
               : xfer_plan_dest(plan, entry);
}

/* R22: the transfer itself wrote or left this name, so it is never replaced. */
static int plan_owns_local(const XferPlan *plan, const char *path)
{
    for (int i = 0; i < plan->entry_count; ++i) {
        const char *local = entry_local(plan, i);
        if (local[0] != '\0' && path_ci_equal(local, path))
            return 1;
    }
    return 0;
}

/* R22: a candidate is unusable when the transfer owns the name itself or the
 * temp/aside the report writer consumes beside it. Without this check a
 * transferred `skipped.txt.tmp` is deleted as leftover debris: write_report
 * removes `<target>.tmp` and replace_file moves `<target>.old` away. */
static int candidate_owned(const XferPlan *plan, const char *target)
{
    if (plan_owns_local(plan, target))
        return 1;
    char side[PATH_MAX_LEN];
    if (sidecar_path(side, sizeof(side), target, ".tmp") &&
        plan_owns_local(plan, side))
        return 1;
    if (sidecar_path(side, sizeof(side), target, ".old") &&
        plan_owns_local(plan, side))
        return 1;
    return 0;
}

/* Compose one candidate name in the report folder. */
static XferReportResult candidate_path(const char *folder, int index,
                                       char *out, size_t cap)
{
    char name[32];
    if (index == 1)
        snprintf(name, sizeof(name), "%s", XFER_REPORT_NAME);
    else
        snprintf(name, sizeof(name), "skipped %d.txt", index);

    Path path;
    if (path_join(folder, name, &path) != PATH_OK)
        return XFER_REPORT_REFUSED;
    if (text_len(path.text) >= cap)
        return XFER_REPORT_FAILED;
    memcpy(out, path.text, text_len(path.text) + 1);
    return XFER_REPORT_OK;
}

static XferReportResult choose_target(FsPort *fs, const char *report_folder,
                                      const XferPlan *plan, char *target)
{
    if (report_folder == NULL || report_folder[0] == '\0' ||
        strcmp(report_folder, "*") == 0)
        return XFER_REPORT_REFUSED;

    char base[PATH_MAX_LEN];
    XferReportResult made = candidate_path(report_folder, 1, base, sizeof(base));
    if (made != XFER_REPORT_OK)
        return made;

    FsEntry entry;
    memset(&entry, 0, sizeof(entry));
    FsResult found = fs->stat(fs->ctx, base, &entry);
    if (found != FS_OK && found != FS_NOT_FOUND)
        return found == FS_DENIED ? XFER_REPORT_REFUSED
                                  : XFER_REPORT_FAILED;
    if (found == FS_NOT_FOUND) {
        if (!candidate_owned(plan, base)) {
            memcpy(target, base, text_len(base) + 1);
            return XFER_REPORT_OK;
        }
    } else {
        if (entry.type == FS_ENTRY_DIR)
            return XFER_REPORT_REFUSED;
        if (!candidate_owned(plan, base)) {
            memcpy(target, base, text_len(base) + 1);
            return XFER_REPORT_OK;
        }
    }

    /* The transfer owns the base name, or the sidecars beside it: take the
     * first free numbered name. A directory on a candidate simply means that
     * name is taken. */
    for (int i = 2; i <= XFER_REPORT_FREE_MAX; ++i) {
        char candidate[PATH_MAX_LEN];
        made = candidate_path(report_folder, i, candidate, sizeof(candidate));
        if (made != XFER_REPORT_OK)
            return made;

        memset(&entry, 0, sizeof(entry));
        found = fs->stat(fs->ctx, candidate, &entry);
        if (found == FS_NOT_FOUND) {
            if (candidate_owned(plan, candidate))
                continue;
            memcpy(target, candidate, text_len(candidate) + 1);
            return XFER_REPORT_OK;
        }
        if (found != FS_OK)
            return found == FS_DENIED ? XFER_REPORT_REFUSED
                                      : XFER_REPORT_FAILED;
        if (entry.type == FS_ENTRY_DIR || candidate_owned(plan, candidate))
            continue;
        memcpy(target, candidate, text_len(candidate) + 1);
        return XFER_REPORT_OK;
    }
    return XFER_REPORT_REFUSED;
}

static const char *pending_reason(const XferReportRequest *request)
{
    if (request->pending_reason != NULL &&
        request->pending_reason[0] != '\0')
        return request->pending_reason;
    return REPORT_PENDING_DEFAULT;
}

static int write_row(FsPort *fs, FsFile *file, const char *kind,
                     const char *path, const char *reason)
{
    return write_text(fs, file, kind) && write_text(fs, file, " ") &&
           write_text(fs, file, path) && write_text(fs, file, " - ") &&
           write_escaped(fs, file, reason) && write_text(fs, file, "\n");
}

/* The missing entries, then the verified sources a partial move kept. */
static int write_rows(FsPort *fs, FsFile *file, const char *report_folder,
                      const XferPlan *plan, const XferReportRequest *request)
{
    char row[XFER_REPORT_ROW_MAX];
    const char *reason = pending_reason(request);

    for (int i = 0; i < plan->entry_count; ++i) {
        const XferPlanEntry *entry = &plan->entries[i];
        if (!entry_missing(entry))
            continue;
        if (xfer_plan_report_row(plan, i, report_folder, row, sizeof(row)) < 0)
            return 0;
        const char *kind = entry->status == XFER_STATUS_SKIPPED
                               ? "SKIPPED"
                               : (entry->status == XFER_STATUS_UNVERIFIED
                                      ? "UNVERIFIED"
                                      : "NOT ATTEMPTED");
        const char *text = entry->status == XFER_STATUS_PENDING
                               ? reason
                               : skip_reason(entry);
        if (!write_row(fs, file, kind, row, text))
            return 0;
    }

    if (request->deleted < 0)
        return 1;

    int total = xfer_plan_list_count(plan, XFER_LIST_DELETE);
    for (int i = request->deleted; i < total; ++i) {
        int entry = xfer_plan_list_at(plan, XFER_LIST_DELETE, i);
        if (entry == XFER_PLAN_NO_INDEX)
            continue;
        if (!write_text(fs, file, "NOT DELETED "))
            return 0;
        if (plan->direction == XFER_PLAN_UPLOAD) {
            /* The local source is the row's path, relative when inside the
             * report folder (U1's one path rule). */
            if (xfer_plan_report_row(plan, entry, report_folder, row,
                                     sizeof(row)) < 0)
                return 0;
            if (!write_text(fs, file, row))
                return 0;
        } else {
            /* A download's source is remote: the same REMOTE marker the
             * missing rows use, never a local path it never had. */
            if (!write_text(fs, file, "REMOTE ") ||
                !write_escaped(fs, file, xfer_plan_source(plan, entry)))
                return 0;
        }
        if (!write_text(fs, file, " - ") || !write_escaped(fs, file, reason) ||
            !write_text(fs, file, "\n"))
            return 0;
    }
    return 1;
}

static int count_rows(const XferPlan *plan, const XferReportRequest *request)
{
    int count = 0;
    for (int i = 0; i < plan->entry_count; ++i) {
        if (entry_missing(&plan->entries[i]))
            count++;
    }
    if (request->deleted >= 0) {
        int total = xfer_plan_list_count(plan, XFER_LIST_DELETE);
        if (request->deleted < total)
            count += total - request->deleted;
    }
    return count;
}

static int write_body(FsPort *fs, FsFile *file, const char *report_folder,
                      const XferPlan *plan, const XferReportRequest *request)
{
    char line[64];
    int missing = count_rows(plan, request);

    if (!write_text(fs, file, REPORT_HEADER "\n"))
        return 0;
    if (!write_text(fs, file, "direction: ") ||
        !write_text(fs, file,
                    request->direction != NULL ? request->direction : "") ||
        !write_text(fs, file, request->move ? " (MOVE)\n" : "\n"))
        return 0;
    if (!write_text(fs, file, "roots: ") ||
        !write_escaped(fs, file, request->roots) ||
        !write_text(fs, file, "\n"))
        return 0;

    snprintf(line, sizeof(line), "missing: %d\n\n", missing);
    if (!write_text(fs, file, line))
        return 0;

    if (missing == 0)
        return write_text(fs, file, request->complete ? REPORT_COMPLETE "\n"
                                                      : REPORT_STOPPED "\n");
    return write_rows(fs, file, report_folder, plan, request);
}

static XferReportResult write_report(FsPort *fs, const char *report_folder,
                                     const char *target, const XferPlan *plan,
                                     const XferReportRequest *request)
{
    char temp[PATH_MAX_LEN];
    if (!sidecar_path(temp, sizeof(temp), target, ".tmp"))
        return XFER_REPORT_FAILED;
    /* A leftover temp is never a source of truth. */
    fs->remove(fs->ctx, temp);

    FsFile *file = NULL;
    FsResult opened = fs->open(fs->ctx, temp, FS_OPEN_WRITE_TRUNC, &file);
    if (opened != FS_OK)
        return opened == FS_DENIED ? XFER_REPORT_REFUSED
                                   : XFER_REPORT_FAILED;

    int ok = write_body(fs, file, report_folder, plan, request);
    if (ok && fs->sync(fs->ctx, file) != FS_OK)
        ok = 0;
    if (fs->close(fs->ctx, file) != FS_OK)
        ok = 0;
    if (!ok) {
        fs->remove(fs->ctx, temp);
        return XFER_REPORT_FAILED;
    }
    return replace_file(fs, temp, target);
}

XferReportResult xfer_report_write(FsPort *fs, const char *report_folder,
                                   const XferPlan *plan,
                                   const XferReportRequest *request,
                                   char *chosen_path, size_t chosen_cap)
{
    if (chosen_path != NULL && chosen_cap > 0)
        chosen_path[0] = '\0';
    if (fs == NULL || plan == NULL || request == NULL)
        return XFER_REPORT_FAILED;

    char target[PATH_MAX_LEN];
    XferReportResult result =
        choose_target(fs, report_folder, plan, target);
    if (result != XFER_REPORT_OK)
        return result;

    result = write_report(fs, report_folder, target, plan, request);
    if (result == XFER_REPORT_OK && chosen_path != NULL && chosen_cap > 0)
        snprintf(chosen_path, chosen_cap, "%s", target);
    return result;
}
