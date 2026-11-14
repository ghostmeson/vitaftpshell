/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/fsop_run.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/ftp/ftp.h"

#define FSOP_COPY_BUF 8192
/* Atomic steps are single filesystem calls. Cap them per drain so one call
 * cannot monopolize the worker; copy steps honor the byte budget instead. */
#define FSOP_ATOMIC_PER_STEP 64

struct FsOpRun {
    const FsOpPlan *plan;
    FsOpProgress progress;
    FsOpRunResult result;
    int finished;
    int step_index;
    uint16_t applied[FSOP_ROLLBACK_MAX];
    int applied_count;

    int cancelled;
    int failed;
    int rolling_back;
    int rollback_cursor;
    int rollback_failed_index;

    int copying;
    int copy_step;
    FsFile *src;
    FtpTempWrite temp;
    char buffer[FSOP_COPY_BUF];
};

static void set_result(FsOpProgress *progress, OpCause cause, OpStage stage,
                       const char *message)
{
    if (progress == NULL)
        return;
    progress->cause = cause;
    progress->stage = stage;
    if (message != NULL)
        snprintf(progress->message, sizeof(progress->message), "%s", message);
}

static int count_files(const FsOpPlan *plan)
{
    if (plan->kind == FSOP_MKDIR || plan->kind == FSOP_RENAME)
        return 1;

    int count = 0;
    for (int i = 0; i < plan->step_count; ++i) {
        FsOpStepKind kind = (FsOpStepKind)plan->steps[i].kind;
        if (kind == FSOP_STEP_COPY_FILE || kind == FSOP_STEP_RENAME)
            count++;
        if (plan->kind == FSOP_DELETE &&
            (kind == FSOP_STEP_DELETE_FILE || kind == FSOP_STEP_DELETE_DIR))
            count++;
    }
    return count;
}

static int build_aside(const char *target, char *out, size_t cap)
{
    int n = snprintf(out, cap, "%s.vftp.old", target);
    return n > 0 && (size_t)n < cap;
}

static int stat_type(FsPort *fs, const char *path, FsEntryType *out)
{
    FsEntry entry;
    if (fs->stat(fs->ctx, path, &entry) != FS_OK)
        return 0;
    if (out != NULL)
        *out = entry.type;
    return 1;
}

static int recover_aside(FsPort *fs, const char *target, const char *aside)
{
    int target_exists = stat_type(fs, target, NULL);
    int aside_exists = stat_type(fs, aside, NULL);
    if (!aside_exists)
        return 1;
    if (!target_exists)
        return fs->rename(fs->ctx, aside, target) == FS_OK;
    return fs->remove(fs->ctx, aside) == FS_OK;
}

static int rollback_step(FsPort *fs, const FsOpPlan *plan, int index)
{
    const FsOpRollback *rollback = &plan->rollback[index];
    const char *from = fsop_plan_rollback_from(plan, index);
    const char *to = fsop_plan_rollback_to(plan, index);

    switch ((FsOpRollbackKind)rollback->kind) {
    case FSOP_ROLLBACK_REMOVE_FILE: {
        FsResult r = fs->remove(fs->ctx, from);
        return r == FS_OK || r == FS_NOT_FOUND;
    }
    case FSOP_ROLLBACK_REMOVE_DIR: {
        FsResult r = fs->rmdir(fs->ctx, from);
        return r == FS_OK || r == FS_NOT_FOUND;
    }
    case FSOP_ROLLBACK_RENAME_BACK: {
        FsResult r = fs->rename(fs->ctx, from, to);
        return r == FS_OK || r == FS_NOT_FOUND;
    }
    default:
        return 0;
    }
}

static void record_applied(FsOpRun *run, const FsOpStep *step)
{
    if (step->rollback_index >= 0 && run->applied_count < FSOP_ROLLBACK_MAX)
        run->applied[run->applied_count++] = (uint16_t)step->rollback_index;
}

static void finish(FsOpRun *run, FsOpRunResult result)
{
    run->result = result;
    run->finished = 1;
}

/* The delete path does not roll back: completed removals stay and the dock
 * reports the partial count (the existing contract). Everything else applies
 * the rollback list in reverse. */
static void enter_failure(FsOpRun *run)
{
    FsOpProgress *progress = &run->progress;

    if (run->plan->kind == FSOP_DELETE) {
        snprintf(progress->message, sizeof(progress->message),
                 "DELETED %d OF %d", progress->files_done,
                 progress->files_total);
        if (run->cancelled)
            progress->cause = OP_CAUSE_USER;
        progress->stage = OP_STAGE_TRANSFER;
        finish(run, FSOP_RUN_PARTIAL);
        return;
    }

    if (run->cancelled)
        set_result(progress, OP_CAUSE_USER, OP_STAGE_TRANSFER, "CANCELLED");
    else if (progress->message[0] == '\0')
        set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE, "FAILED");

    run->rolling_back = 1;
    run->rollback_cursor = run->applied_count - 1;
}

static void pump_rollback(FsOpRun *run, FsPort *fs)
{
    int ok = 1;
    while (run->rollback_cursor >= 0) {
        int index = run->applied[run->rollback_cursor--];
        if (!rollback_step(fs, run->plan, index)) {
            ok = 0;
            if (run->rollback_failed_index < 0)
                run->rollback_failed_index = index;
        }
    }
    run->rolling_back = 0;

    if (!ok) {
        int owner = -1;
        for (int i = 0; i < run->plan->step_count; ++i) {
            if (run->plan->steps[i].rollback_index ==
                run->rollback_failed_index) {
                owner = i;
                break;
            }
        }
        if (run->plan->kind == FSOP_MOVE && owner >= 0) {
            snprintf(run->progress.message, sizeof(run->progress.message),
                     "MOVE INCOMPLETE: %s -> %s",
                     fsop_plan_step_source(run->plan, owner),
                     fsop_plan_step_dest(run->plan, owner));
        } else {
            snprintf(run->progress.message, sizeof(run->progress.message),
                     "ROLLBACK INCOMPLETE");
        }
        run->progress.cause = OP_CAUSE_STORAGE;
        run->progress.stage = OP_STAGE_ROLLBACK;
        finish(run, FSOP_RUN_FAILED);
        return;
    }

    finish(run, run->cancelled ? FSOP_RUN_CANCELLED : FSOP_RUN_FAILED);
}

static void copy_abort(FsOpRun *run, FsPort *fs)
{
    if (run->temp.open)
        ftp_temp_abort(fs, &run->temp);
    if (run->src != NULL)
        fs->close(fs->ctx, run->src);
    run->src = NULL;
    run->copying = 0;
}

static int copy_begin(FsOpRun *run, FsPort *fs)
{
    int step_index = run->step_index;
    const char *source = fsop_plan_step_source(run->plan, step_index);
    const char *dest = fsop_plan_step_dest(run->plan, step_index);

    run->copying = 0;
    run->copy_step = step_index;
    run->src = NULL;
    memset(&run->temp, 0, sizeof(run->temp));

    FsResult r = fs->open(fs->ctx, source, FS_OPEN_READ, &run->src);
    if (r != FS_OK) {
        set_result(&run->progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                   "SOURCE OPEN FAILED");
        return 0;
    }

    r = ftp_temp_open(fs, dest, &run->temp);
    if (r != FS_OK) {
        fs->close(fs->ctx, run->src);
        run->src = NULL;
        set_result(&run->progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                   "DESTINATION OPEN FAILED");
        return 0;
    }

    run->copying = 1;
    return 1;
}

/* At EOF: commit, close the source, and remove it for a move. Returns 1 when
 * the step completed and 0 when it failed (the caller enters rollback). */
static int copy_finish(FsOpRun *run, FsPort *fs)
{
    const FsOpStep *step = &run->plan->steps[run->copy_step];
    const char *source = fsop_plan_step_source(run->plan, run->copy_step);

    FtpCommitStep commit = FTP_COMMIT_OK;
    FsResult r = ftp_temp_finish(fs, &run->temp, &commit);
    if (r != FS_OK) {
        fs->close(fs->ctx, run->src);
        run->src = NULL;
        run->copying = 0;
        set_result(&run->progress, OP_CAUSE_STORAGE, OP_STAGE_COMMIT,
                   commit == FTP_COMMIT_RENAME ? "DESTINATION RENAME FAILED"
                                               : "DESTINATION CLOSE FAILED");
        return 0;
    }
    if (run->temp.sync_failed)
        run->progress.sync_skipped = 1;

    r = fs->close(fs->ctx, run->src);
    run->src = NULL;
    run->copying = 0;
    if (r != FS_OK) {
        set_result(&run->progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                   "SOURCE CLOSE FAILED");
        return 0;
    }

    if (step->move_source) {
        r = fs->remove(fs->ctx, source);
        if (r != FS_OK && r != FS_NOT_FOUND) {
            if (step->dest_existed) {
                /* The old destination was already replaced at commit. Removing
                 * the new file now would leave neither; keep it and name the
                 * source that still exists. */
                set_result(&run->progress, OP_CAUSE_STORAGE, OP_STAGE_ROLLBACK,
                           "MOVE FAILED - DESTINATION KEPT");
                return 0;
            }
            FsResult undo =
                fs->remove(fs->ctx,
                           fsop_plan_step_dest(run->plan, run->copy_step));
            if (undo != FS_OK && undo != FS_NOT_FOUND) {
                snprintf(run->progress.message, sizeof(run->progress.message),
                         "MOVE INCOMPLETE: %s -> %s", source,
                         fsop_plan_step_dest(run->plan, run->copy_step));
                run->progress.cause = OP_CAUSE_STORAGE;
                run->progress.stage = OP_STAGE_ROLLBACK;
            } else if (run->progress.files_done > 0) {
                snprintf(run->progress.message, sizeof(run->progress.message),
                         "MOVE FAILED - %d OF %d MOVED",
                         run->progress.files_done, run->progress.files_total);
                run->progress.cause = OP_CAUSE_STORAGE;
                run->progress.stage = OP_STAGE_ROLLBACK;
            } else {
                set_result(&run->progress, OP_CAUSE_STORAGE,
                           OP_STAGE_ROLLBACK, "MOVE FAILED - NOTHING CHANGED");
            }
            return 0;
        }
    }

    run->progress.files_done++;
    return 1;
}

/* Copy up to `budget` bytes. Returns the number copied this call. A finished
 * copy advances the step cursor; a failure enters rollback. */
static size_t copy_pump(FsOpRun *run, FsPort *fs, size_t budget,
                        FsOpCancelFn cancel, void *cancel_ctx)
{
    size_t moved = 0;
    while (moved < budget) {
        if (cancel != NULL && cancel(cancel_ctx)) {
            copy_abort(run, fs);
            run->cancelled = 1;
            enter_failure(run);
            return moved;
        }

        size_t want = sizeof(run->buffer);
        if (want > budget - moved)
            want = budget - moved;
        size_t got = 0;
        FsResult r = fs->read(fs->ctx, run->src, run->buffer, want, &got);
        if (r == FS_END || (r == FS_OK && got == 0)) {
            if (!copy_finish(run, fs)) {
                run->failed = 1;
                enter_failure(run);
            } else {
                const FsOpStep *step = &run->plan->steps[run->copy_step];
                record_applied(run, step);
                run->step_index++;
            }
            return moved;
        }
        if (r != FS_OK) {
            copy_abort(run, fs);
            set_result(&run->progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                       "SOURCE READ FAILED");
            run->failed = 1;
            enter_failure(run);
            return moved;
        }

        size_t written = 0;
        while (written < got) {
            size_t n = 0;
            r = fs->write(fs->ctx, run->temp.file, run->buffer + written,
                          got - written, &n);
            if (r != FS_OK || n == 0) {
                copy_abort(run, fs);
                set_result(&run->progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                           "DESTINATION WRITE FAILED");
                run->failed = 1;
                enter_failure(run);
                return moved;
            }
            written += n;
        }
        run->progress.bytes_done += got;
        moved += got;
    }
    return moved;
}

static int run_rename(FsPort *fs, const FsOpPlan *plan, int step_index,
                      FsOpProgress *progress)
{
    const char *source = fsop_plan_step_source(plan, step_index);
    const char *dest = fsop_plan_step_dest(plan, step_index);

    int target_exists = stat_type(fs, dest, NULL);

    if (target_exists && !fs->rename_replaces(fs->ctx)) {
        char aside[PATH_MAX_LEN];
        if (!build_aside(dest, aside, sizeof(aside))) {
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_COMMIT,
                       "NAME TOO LONG");
            return 0;
        }
        if (!recover_aside(fs, dest, aside)) {
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_ROLLBACK,
                       "ASIDE RECOVERY FAILED");
            return 0;
        }
        if (fs->rename(fs->ctx, dest, aside) != FS_OK) {
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_COMMIT,
                       "RENAME FAILED");
            return 0;
        }
        if (fs->rename(fs->ctx, source, dest) != FS_OK) {
            fs->rename(fs->ctx, aside, dest);
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_COMMIT,
                       "RENAME FAILED - NOTHING CHANGED");
            return 0;
        }
        if (fs->remove(fs->ctx, aside) != FS_OK) {
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_COMMIT,
                       "ASIDE CLEANUP FAILED");
            return 0;
        }
    } else {
        if (fs->rename(fs->ctx, source, dest) != FS_OK) {
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_COMMIT,
                       "RENAME FAILED");
            return 0;
        }
    }

    progress->files_done++;
    return 1;
}

static int run_mkdir(FsPort *fs, const FsOpPlan *plan, int step_index,
                     FsOpProgress *progress)
{
    const char *dest = fsop_plan_step_dest(plan, step_index);
    FsEntryType type;
    if (stat_type(fs, dest, &type)) {
        if (type != FS_ENTRY_DIR) {
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                       "DESTINATION IS A FILE");
            return 0;
        }
        return 1;
    }

    FsResult r = fs->mkdir(fs->ctx, dest);
    if (r != FS_OK && r != FS_EXISTS) {
        set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                   "CREATE FOLDER FAILED");
        return 0;
    }
    if (plan->kind == FSOP_MKDIR)
        progress->files_done++;
    return 1;
}

static int run_delete(FsPort *fs, const FsOpPlan *plan, int step_index,
                      FsOpProgress *progress)
{
    const FsOpStep *step = &plan->steps[step_index];
    const char *path = fsop_plan_step_source(plan, step_index);

    FsResult r;
    if (step->kind == FSOP_STEP_DELETE_DIR)
        r = fs->rmdir(fs->ctx, path);
    else
        r = fs->remove(fs->ctx, path);

    if (r != FS_OK) {
        set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                   step->kind == FSOP_STEP_DELETE_DIR ? "REMOVE FOLDER FAILED"
                                                      : "REMOVE FILE FAILED");
        return 0;
    }
    if (plan->kind == FSOP_DELETE)
        progress->files_done++;
    return 1;
}

FsOpRun *fsop_run_create(const FsOpPlan *plan)
{
    if (plan == NULL)
        return NULL;

    FsOpRun *run = calloc(1, sizeof(FsOpRun));
    if (run == NULL)
        return NULL;

    run->plan = plan;
    run->result = FSOP_RUN_FAILED;
    run->progress.cause = OP_CAUSE_APP;
    run->progress.stage = OP_STAGE_PLAN;
    run->progress.files_total = count_files(plan);
    run->progress.bytes_total = plan->total_bytes;
    run->progress.total_known = plan->total_known;
    run->rollback_failed_index = -1;
    return run;
}

void fsop_run_destroy(FsOpRun *run)
{
    free(run);
}

int fsop_run_finished(const FsOpRun *run)
{
    return run != NULL && run->finished;
}

const FsOpProgress *fsop_run_progress(const FsOpRun *run)
{
    return run != NULL ? &run->progress : NULL;
}

FsOpRunResult fsop_run_step(FsOpRun *run, FsPort *fs, size_t byte_budget,
                            FsOpCancelFn cancel, void *cancel_ctx)
{
    if (run == NULL || fs == NULL)
        return FSOP_RUN_FAILED;
    if (run->finished)
        return run->result;

    if (run->rolling_back) {
        pump_rollback(run, fs);
        return run->result;
    }

    if (byte_budget == 0)
        byte_budget = 1;
    size_t remaining = byte_budget;
    int atomic = 0;

    while (!run->finished) {
        if (run->copying) {
            size_t moved = copy_pump(run, fs, remaining, cancel, cancel_ctx);
            if (run->finished || run->rolling_back)
                break;
            if (moved == 0)
                break;
            if (moved >= remaining)
                break;
            remaining -= moved;
            continue;
        }

        if (run->step_index >= run->plan->step_count) {
            set_result(&run->progress, OP_CAUSE_APP, OP_STAGE_TRANSFER,
                       "COMPLETE");
            finish(run, FSOP_RUN_OK);
            break;
        }

        if (cancel != NULL && cancel(cancel_ctx)) {
            run->cancelled = 1;
            enter_failure(run);
            break;
        }

        const FsOpStep *step = &run->plan->steps[run->step_index];
        if (step->kind == FSOP_STEP_COPY_FILE) {
            if (!copy_begin(run, fs)) {
                run->failed = 1;
                enter_failure(run);
                break;
            }
            continue;
        }

        int ok;
        switch ((FsOpStepKind)step->kind) {
        case FSOP_STEP_RENAME:
            ok = run_rename(fs, run->plan, run->step_index, &run->progress);
            break;
        case FSOP_STEP_MKDIR:
            ok = run_mkdir(fs, run->plan, run->step_index, &run->progress);
            break;
        case FSOP_STEP_DELETE_FILE:
        case FSOP_STEP_DELETE_DIR:
            ok = run_delete(fs, run->plan, run->step_index, &run->progress);
            break;
        default:
            set_result(&run->progress, OP_CAUSE_APP, OP_STAGE_PLAN,
                       "UNKNOWN STEP");
            ok = 0;
            break;
        }

        if (!ok) {
            run->failed = 1;
            enter_failure(run);
            break;
        }

        record_applied(run, step);
        run->step_index++;
        if (++atomic >= FSOP_ATOMIC_PER_STEP)
            break;
    }

    if (run->rolling_back)
        pump_rollback(run, fs);
    return run->result;
}

/* The blocking wrapper's cancel callback may read progress between steps. Mirror
 * the run's live progress into the caller's struct before each check. */
typedef struct {
    FsOpRun *run;
    FsOpProgress *out;
    FsOpCancelFn cancel;
    void *ctx;
} ProgressBridge;

static int progress_cancel(void *ctx)
{
    ProgressBridge *bridge = ctx;
    const FsOpProgress *live = fsop_run_progress(bridge->run);
    if (live != NULL)
        *bridge->out = *live;
    return bridge->cancel != NULL ? bridge->cancel(bridge->ctx) : 0;
}

FsOpRunResult fsop_run(FsPort *fs, const FsOpPlan *plan, FsOpProgress *progress,
                       FsOpCancelFn cancel, void *cancel_ctx)
{
    if (progress == NULL)
        return FSOP_RUN_FAILED;
    memset(progress, 0, sizeof(*progress));
    progress->cause = OP_CAUSE_APP;
    progress->stage = OP_STAGE_PLAN;

    if (fs == NULL || plan == NULL) {
        set_result(progress, OP_CAUSE_APP, OP_STAGE_PLAN, "NO PLAN");
        return FSOP_RUN_FAILED;
    }
    if (plan->needs_conflict) {
        set_result(progress, OP_CAUSE_APP, OP_STAGE_PLAN, "CONFLICT UNDECIDED");
        return FSOP_RUN_FAILED;
    }
    if (!plan->ok) {
        set_result(progress, OP_CAUSE_APP, OP_STAGE_PLAN,
                   plan->reason[0] != '\0' ? plan->reason : "PLAN REFUSED");
        return FSOP_RUN_FAILED;
    }

    FsOpRun *run = fsop_run_create(plan);
    if (run == NULL) {
        set_result(progress, OP_CAUSE_APP, OP_STAGE_PLAN, "OUT OF MEMORY");
        return FSOP_RUN_FAILED;
    }

    ProgressBridge bridge = {run, progress, cancel, cancel_ctx};
    while (!fsop_run_finished(run)) {
        fsop_run_step(run, fs, (size_t)-1, progress_cancel, &bridge);
        *progress = run->progress;
    }

    FsOpRunResult result = run->result;
    *progress = run->progress;
    fsop_run_destroy(run);
    return result;
}
