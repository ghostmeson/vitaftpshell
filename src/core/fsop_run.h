/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_FSOP_RUN_H
#define VFTP_CORE_FSOP_RUN_H

#include <stddef.h>
#include <stdint.h>

#include "core/cause.h"
#include "core/fsop_plan.h"

typedef enum {
    FSOP_RUN_OK = 0,
    FSOP_RUN_CANCELLED,
    FSOP_RUN_PARTIAL,
    FSOP_RUN_FAILED
} FsOpRunResult;

typedef struct {
    int files_done;
    int files_total;
    uint64_t bytes_done;
    uint64_t bytes_total;
    int total_known;
    /* A transfer completed even though the platform refused the durability
     * sync. Recorded so the worker can log it; never a failure (D76). */
    int sync_skipped;
    char message[192];
    OpCause cause;
    OpStage stage;
} FsOpProgress;

typedef int (*FsOpCancelFn)(void *ctx);

/*
 * A run owns everything the executor needs between calls: the step cursor, the
 * applied-step list, the copy sub-phase, the open handles, progress, and the
 * copy buffer. It lives on the heap so the worker can suspend a copy while the
 * client, server, and queue tick and resume it on the next step (KTD20).
 */
typedef struct FsOpRun FsOpRun;

/* NULL only when the plan is NULL or allocation fails. The caller keeps the
 * plan alive until the run is destroyed. */
FsOpRun *fsop_run_create(const FsOpPlan *plan);
void fsop_run_destroy(FsOpRun *run);
/* Abort an in-flight copy (temp removed, source closed) before destroy. */
void fsop_run_abort(FsOpRun *run, FsPort *fs);

/*
 * Advance the run by at most `byte_budget` copied bytes and a bounded number of
 * atomic steps. `byte_budget` 0 means one byte. Checks `cancel` at every copy
 * chunk and before every step. Re-entrant calls after the run finishes return
 * the terminal result without touching the filesystem.
 */
FsOpRunResult fsop_run_step(FsOpRun *run, FsPort *fs, size_t byte_budget,
                            FsOpCancelFn cancel, void *cancel_ctx);
int fsop_run_finished(const FsOpRun *run);
const FsOpProgress *fsop_run_progress(const FsOpRun *run);

/* Blocking convenience wrapper over the stepped executor. */
FsOpRunResult fsop_run(FsPort *fs, const FsOpPlan *plan, FsOpProgress *progress,
                       FsOpCancelFn cancel, void *cancel_ctx);

#endif
