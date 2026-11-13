/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/fsop_run.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/ftp/ftp.h"

#define FSOP_COPY_BUF 8192

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

static int apply_rollback(FsPort *fs, const FsOpPlan *plan,
                          const uint16_t *applied, int applied_count,
                          int *failed_index)
{
    int ok = 1;
    for (int i = applied_count - 1; i >= 0; --i) {
        if (!rollback_step(fs, plan, applied[i])) {
            ok = 0;
            if (failed_index != NULL && *failed_index < 0)
                *failed_index = applied[i];
        }
    }
    return ok;
}

/*
 * Keep the 8 KiB copy buffer out of fsop_run's frame. With it inlined, the
 * frame was 19,540 bytes against a 19,756-byte worker stack (b20 core), and a
 * rename that never copies a byte still reserved it. A rename now enters only
 * fsop_run's small frame; the buffer exists only during an actual copy.
 */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int run_copy_file(FsPort *fs, const FsOpPlan *plan, int step_index,
                         FsOpProgress *progress, FsOpCancelFn cancel,
                         void *cancel_ctx, int *cancelled)
{
    const FsOpStep *step = &plan->steps[step_index];
    const char *source = fsop_plan_step_source(plan, step_index);
    const char *dest = fsop_plan_step_dest(plan, step_index);

    FsFile *src = NULL;
    FsResult r = fs->open(fs->ctx, source, FS_OPEN_READ, &src);
    if (r != FS_OK) {
        set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                   "SOURCE OPEN FAILED");
        return 0;
    }

    FtpTempWrite temp;
    r = ftp_temp_open(fs, dest, &temp);
    if (r != FS_OK) {
        fs->close(fs->ctx, src);
        set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                   "DESTINATION OPEN FAILED");
        return 0;
    }

    char buf[FSOP_COPY_BUF];
    for (;;) {
        if (cancel != NULL && cancel(cancel_ctx)) {
            ftp_temp_abort(fs, &temp);
            fs->close(fs->ctx, src);
            *cancelled = 1;
            set_result(progress, OP_CAUSE_USER, OP_STAGE_TRANSFER, "CANCELLED");
            return 0;
        }

        size_t got = 0;
        r = fs->read(fs->ctx, src, buf, sizeof(buf), &got);
        if (r == FS_END)
            break;
        if (r != FS_OK) {
            ftp_temp_abort(fs, &temp);
            fs->close(fs->ctx, src);
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                       "SOURCE READ FAILED");
            return 0;
        }
        if (got == 0)
            break;

        size_t written = 0;
        while (written < got) {
            size_t n = 0;
            r = fs->write(fs->ctx, temp.file, buf + written, got - written, &n);
            if (r != FS_OK || n == 0) {
                ftp_temp_abort(fs, &temp);
                fs->close(fs->ctx, src);
                set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
                           "DESTINATION WRITE FAILED");
                return 0;
            }
            written += n;
        }
        progress->bytes_done += got;
    }

    r = ftp_temp_finish(fs, &temp);
    if (r != FS_OK) {
        fs->close(fs->ctx, src);
        set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_COMMIT,
                   "DESTINATION COMMIT FAILED");
        return 0;
    }

    r = fs->close(fs->ctx, src);
    if (r != FS_OK) {
        set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE,
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
                set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_ROLLBACK,
                           "MOVE FAILED - DESTINATION KEPT");
                return 0;
            }
            FsResult undo = fs->remove(fs->ctx, dest);
            if (undo != FS_OK && undo != FS_NOT_FOUND) {
                snprintf(progress->message, sizeof(progress->message),
                         "MOVE INCOMPLETE: %s -> %s", source, dest);
                progress->cause = OP_CAUSE_STORAGE;
                progress->stage = OP_STAGE_ROLLBACK;
            } else if (progress->files_done > 0) {
                snprintf(progress->message, sizeof(progress->message),
                         "MOVE FAILED - %d OF %d MOVED", progress->files_done,
                         progress->files_total);
                progress->cause = OP_CAUSE_STORAGE;
                progress->stage = OP_STAGE_ROLLBACK;
            } else {
                set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_ROLLBACK,
                           "MOVE FAILED - NOTHING CHANGED");
            }
            return 0;
        }
    }

    progress->files_done++;
    return 1;
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

    progress->files_total = count_files(plan);
    progress->bytes_total = plan->total_bytes;
    progress->total_known = plan->total_known;

    /* A rollback index is at most FSOP_ROLLBACK_MAX - 1 (2047), so 16 bits
     * are enough and the array costs half the stack of an int array. */
    uint16_t applied[FSOP_ROLLBACK_MAX];
    int applied_count = 0;
    int cancelled = 0;
    int failed = 0;

    for (int i = 0; i < plan->step_count; ++i) {
        if (cancel != NULL && cancel(cancel_ctx)) {
            cancelled = 1;
            set_result(progress, OP_CAUSE_USER, OP_STAGE_TRANSFER, "CANCELLED");
            break;
        }

        const FsOpStep *step = &plan->steps[i];
        int ok = 1;
        switch ((FsOpStepKind)step->kind) {
        case FSOP_STEP_COPY_FILE:
            ok = run_copy_file(fs, plan, i, progress, cancel, cancel_ctx,
                               &cancelled);
            break;
        case FSOP_STEP_RENAME:
            ok = run_rename(fs, plan, i, progress);
            break;
        case FSOP_STEP_MKDIR:
            ok = run_mkdir(fs, plan, i, progress);
            break;
        case FSOP_STEP_DELETE_FILE:
        case FSOP_STEP_DELETE_DIR:
            ok = run_delete(fs, plan, i, progress);
            break;
        default:
            set_result(progress, OP_CAUSE_APP, OP_STAGE_PLAN, "UNKNOWN STEP");
            ok = 0;
            break;
        }

        if (!ok) {
            failed = 1;
            break;
        }
        if (step->rollback_index >= 0 &&
            applied_count < FSOP_ROLLBACK_MAX)
            applied[applied_count++] = (uint16_t)step->rollback_index;
    }

    if (cancelled || failed) {
        if (plan->kind == FSOP_DELETE) {
            snprintf(progress->message, sizeof(progress->message),
                     "DELETED %d OF %d", progress->files_done,
                     progress->files_total);
            if (cancelled)
                progress->cause = OP_CAUSE_USER;
            progress->stage = OP_STAGE_TRANSFER;
            return FSOP_RUN_PARTIAL;
        }

        int failed_rollback = -1;
        int rollback_ok = apply_rollback(fs, plan, applied, applied_count,
                                         &failed_rollback);
        if (!rollback_ok) {
            int owner = -1;
            for (int i = 0; i < plan->step_count; ++i) {
                if (plan->steps[i].rollback_index == failed_rollback) {
                    owner = i;
                    break;
                }
            }
            if (plan->kind == FSOP_MOVE && owner >= 0) {
                snprintf(progress->message, sizeof(progress->message),
                         "MOVE INCOMPLETE: %s -> %s",
                         fsop_plan_step_source(plan, owner),
                         fsop_plan_step_dest(plan, owner));
            } else {
                snprintf(progress->message, sizeof(progress->message),
                         "ROLLBACK INCOMPLETE");
            }
            progress->cause = OP_CAUSE_STORAGE;
            progress->stage = OP_STAGE_ROLLBACK;
            return FSOP_RUN_FAILED;
        }

        if (cancelled) {
            set_result(progress, OP_CAUSE_USER, OP_STAGE_TRANSFER, "CANCELLED");
            return FSOP_RUN_CANCELLED;
        }
        if (progress->message[0] == '\0')
            set_result(progress, OP_CAUSE_STORAGE, OP_STAGE_WRITE, "FAILED");
        return FSOP_RUN_FAILED;
    }

    set_result(progress, OP_CAUSE_APP, OP_STAGE_TRANSFER, "COMPLETE");
    return FSOP_RUN_OK;
}
