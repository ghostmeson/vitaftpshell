/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_FSOP_PLAN_H
#define VFTP_CORE_FSOP_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "core/cause.h"
#include "core/path.h"
#include "platform/fs.h"

/*
 * Local operation planning (U16). The planner never touches the filesystem:
 * the caller supplies the directory facts and gets an ordered plan, a rollback
 * list, and the path checks. That keeps the whole recursive operation testable
 * on the host and keeps every filesystem call in the executor.
 *
 * Paths live in a fixed arena. A plan that would overflow the arena or the
 * step table is refused, never truncated: a partial recursive plan is worse
 * than no plan.
 */

#define FSOP_ARENA_MAX 262144
#define FSOP_STEP_MAX 2048
#define FSOP_ROLLBACK_MAX 2048
#define FSOP_CONFLICT_NAMES 16

typedef enum {
    FSOP_COPY = 0,
    FSOP_MOVE,
    FSOP_DELETE,
    FSOP_MKDIR,
    FSOP_RENAME
} FsOpKind;

typedef enum {
    FSOP_STEP_MKDIR = 0,
    FSOP_STEP_COPY_FILE,
    FSOP_STEP_RENAME,
    FSOP_STEP_DELETE_FILE,
    FSOP_STEP_DELETE_DIR
} FsOpStepKind;

typedef enum {
    FSOP_ROLLBACK_REMOVE_FILE = 0,
    FSOP_ROLLBACK_REMOVE_DIR,
    FSOP_ROLLBACK_RENAME_BACK
} FsOpRollbackKind;

typedef enum {
    FSOP_CONFLICT_ASK = 0,
    FSOP_CONFLICT_OVERWRITE,
    FSOP_CONFLICT_SKIP,
    /* A same-folder COPY: choose a free copy name instead of asking again
     * (R17, KTD6). */
    FSOP_CONFLICT_DUPLICATE
} FsOpConflict;

typedef struct {
    const char *path;
    FsEntryType type;
    uint64_t size;
    int size_known;
    int exists;
} FsOpFact;

typedef struct {
    const FsOpFact *items;
    int count;
} FsOpFacts;

typedef struct {
    uint8_t kind;
    uint8_t move_source;
    uint8_t dest_existed;
    int8_t reserved;
    int16_t rollback_index; /* index into rollback[], or -1 */
    uint32_t source_off;
    uint32_t dest_off;
} FsOpStep;

typedef struct {
    uint8_t kind;
    uint8_t reserved[3];
    uint32_t from_off;
    uint32_t to_off;
} FsOpRollback;

/* One recorded set of named paths: the replacement list for the conflict
 * prompt, or the already-here list for the duplicate prompt. */
typedef struct {
    int count;
    int named;
    Path items[FSOP_CONFLICT_NAMES];
} FsOpNamedPaths;

typedef struct {
    FsOpKind kind;
    FsOpStep steps[FSOP_STEP_MAX];
    int step_count;
    FsOpRollback rollback[FSOP_ROLLBACK_MAX];
    int rollback_count;

    char arena[FSOP_ARENA_MAX];
    size_t arena_used;


    int needs_conflict;
    int skipped_count;
    FsOpNamedPaths existing;

    /* A same-folder COPY under ASK: every item already lives in the
     * destination, so the caller asks SAVE DUPLICATES or CANCEL and no step
     * exists until the answer arrives (R16, KTD6). */
    int needs_duplicate;
    FsOpNamedPaths duplicate;

    int item_count;
    int file_count;
    int all_sizes_known;
    uint64_t total_bytes;
    int total_known;

    int ok;
    char reason[128];
} FsOpPlan;

typedef struct {
    FsOpKind kind;
    /* Copy and move take one or more sources; rename takes one. */
    const char *const *sources;
    int source_count;
    /* Destination directory for copy/move, full target for mkdir/rename. */
    const char *destination;
    const FsOpFacts *facts;
    FsOpConflict conflict;
} FsOpRequest;

/*
 * Build a plan. Returns 1 when a plan (possibly one that still needs the R41
 * decision) was produced, 0 when the request was refused; `plan->reason`
 * names the protection or the missing fact.
 */
int fsop_plan_build(FsOpPlan *plan, const FsOpRequest *request);

/*
 * The one copy-name rule, shared by the planner and the worker's fact collector
 * so the prompt and the plan cannot disagree (R17, KTD6). Candidate 0 is
 * `A copy.TXT`, candidate n is `A copy <n + 1>.TXT`; a folder keeps its whole
 * name. Returns 0 when `out` cannot hold the name.
 */
int fsop_copy_name(const char *source, int source_is_dir, int n, char *out,
                   size_t cap);

const char *fsop_plan_step_source(const FsOpPlan *plan, int step);
const char *fsop_plan_step_dest(const FsOpPlan *plan, int step);
const char *fsop_plan_existing_dest(const FsOpPlan *plan, int index);
const char *fsop_plan_duplicate_source(const FsOpPlan *plan, int index);
const char *fsop_plan_rollback_from(const FsOpPlan *plan, int rollback);
const char *fsop_plan_rollback_to(const FsOpPlan *plan, int rollback);

#endif
