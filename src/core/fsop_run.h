/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_FSOP_RUN_H
#define VFTP_CORE_FSOP_RUN_H

#include "core/cause.h"
#include "core/fsop_plan.h"
#include "platform/fs.h"

/*
 * Local operation executor (U17). It walks a U16 plan through FsPort with the
 * templed-write discipline, applies the plan's rollback in reverse, and
 * reports the one cause class and stage for the dock and the log.
 *
 * A recursive delete cannot be rolled back. Cancelling or failing it stops
 * after the item in flight and reports `DELETED n OF m`.
 */

typedef enum {
    FSOP_RUN_OK = 0,
    FSOP_RUN_FAILED,
    FSOP_RUN_CANCELLED,
    FSOP_RUN_PARTIAL
} FsOpRunResult;

typedef struct {
    int files_done;
    int files_total;
    uint64_t bytes_done;
    uint64_t bytes_total;
    int total_known;
    char message[192];
    OpCause cause;
    OpStage stage;
} FsOpProgress;

typedef int (*FsOpCancelFn)(void *ctx);

FsOpRunResult fsop_run(FsPort *fs, const FsOpPlan *plan, FsOpProgress *progress,
                       FsOpCancelFn cancel, void *cancel_ctx);

#endif
