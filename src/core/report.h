/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_REPORT_H
#define VFTP_CORE_REPORT_H

#include <stddef.h>

#include "core/xfer_plan.h"
#include "platform/fs.h"

/*
 * The one folder-transfer report (R21-R23).
 *
 * A transfer that reaches its copy phase leaves `skipped.txt` in the local
 * folder captured at job start: a header naming the direction and both roots,
 * the count of entries the transfer did not complete, and one line per entry
 * with its local path relative to that folder (or absolute outside it) and its
 * reason. A download skip with no local path names the remote path and marks
 * it REMOTE. Control bytes are escaped as \xNN so one entry stays one line.
 *
 * The file is written UTF-8 with LF through a temporary file, synced, closed,
 * and renamed with the aside fallback (KTD9). A directory already using the
 * name is never replaced; a file the transfer itself wrote or left as a source
 * takes the free name (`skipped 2.txt`). A report that cannot be written
 * changes nothing about the transfer's outcome: the caller appends its dock
 * notice to the terminal reason.
 *
 * The module is policy over FsPort only, so the whole report format runs on
 * the host.
 */

#define XFER_REPORT_NAME "skipped.txt"
/* The free-name search stops here; a folder holding every candidate is
 * treated as refusing the report rather than renamed or overwritten. */
#define XFER_REPORT_FREE_MAX 99

typedef enum {
    XFER_REPORT_OK = 0,
    /* A directory owns the name, or the folder refuses the write. */
    XFER_REPORT_REFUSED,
    /* The write, sync, close, or rename failed. */
    XFER_REPORT_FAILED
} XferReportResult;

typedef struct {
    /* "DOWNLOAD" or "UPLOAD"; the header names the direction it was given. */
    const char *direction;
    /* One line naming both roots, composed by the caller. */
    const char *roots;
    int move;
    /* 1 only when the job reached its terminal with every phase done. A
     * cancel or failure never writes the complete line, even when no row is
     * missing. */
    int complete;
    /* XFER_LIST_DELETE entries already removed, or -1 when the job never
     * entered its delete phase. The remaining sources are named so a partial
     * move lists what it kept. */
    int deleted;
    /* The row reason for an entry the job never attempted or did not delete:
     * "CANCELLED", "DELETE FAILED", or the terminal's own cause. */
    const char *pending_reason;
} XferReportRequest;

/*
 * Write the one report. `report_folder` is the folder captured at job start,
 * never the folder the pane shows later. On success `chosen_path` holds the
 * file actually written (receiving `skipped 2.txt` when the base name is the
 * transfer's own); on refusal or failure it is empty. A NULL or short buffer
 * is allowed when the caller does not need the name.
 */
XferReportResult xfer_report_write(FsPort *fs, const char *report_folder,
                                   const XferPlan *plan,
                                   const XferReportRequest *request,
                                   char *chosen_path, size_t chosen_cap);

#endif
