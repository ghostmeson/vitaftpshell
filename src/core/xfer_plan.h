/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_XFER_PLAN_H
#define VFTP_CORE_XFER_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "core/path.h"

/*
 * Cross-pane folder transfer planning (U1).
 *
 * The plan is a pure, heap-owned value: the walker fills it with entries, then
 * the job transfers, verifies, reports, and deletes from it. No port call lives
 * here, so the whole recursive policy runs on the host and the worker keeps
 * every filesystem and socket call.
 *
 * Paths live in a fixed arena and entries in a fixed table. A tree that would
 * overflow either is refused before the first byte, never truncated (KTD2):
 * a partial recursive plan is worse than no plan. The directory worklist makes
 * the walk iterative, so the worker stack never holds a recursion.
 *
 * Direction fixes which side is local: a download's source is remote and its
 * destination local; an upload's source is local and its destination remote.
 * Report rows are written against the local side only.
 */

#define XFER_PLAN_ENTRY_MAX 4096
#define XFER_PLAN_ARENA_MAX (512u * 1024u)
#define XFER_PLAN_CONFLICT_NAMES 16
#define XFER_PLAN_NO_PARENT 0xFFFFu
#define XFER_PLAN_NO_INDEX (-1)
#define XFER_PLAN_NO_PATH UINT32_MAX
/* A relative path of PATH_MAX_LEN bytes, with every control byte escaped. */
#define XFER_REPORT_ROW_MAX (PATH_MAX_LEN * 4)

typedef enum {
    XFER_PLAN_DOWNLOAD = 0,
    XFER_PLAN_UPLOAD
} XferPlanDirection;

typedef enum {
    XFER_ENTRY_DIR = 0,
    XFER_ENTRY_FILE
} XferEntryType;

typedef enum {
    XFER_STATUS_PENDING = 0,
    /* The copy completed and recorded its byte count; verification has not
     * compared it yet. */
    XFER_STATUS_COPIED,
    XFER_STATUS_VERIFIED,
    XFER_STATUS_UNVERIFIED,
    XFER_STATUS_SKIPPED
} XferEntryStatus;

typedef enum {
    XFER_SKIP_NONE = 0,
    XFER_SKIP_UNSAFE_NAME,
    XFER_SKIP_READ_ONLY,
    XFER_SKIP_LIST_REFUSED,
    XFER_SKIP_LIST_CAP,
    XFER_SKIP_DEST_IS_FILE,
    XFER_SKIP_LOCAL_UNREADABLE
} XferSkipReason;

typedef enum {
    /* A destination row that matches no planned entry: never counted. */
    XFER_VERIFY_IGNORED = 0,
    XFER_VERIFY_VERIFIED,
    XFER_VERIFY_UNVERIFIED
} XferVerifyResult;

typedef enum {
    XFER_LIST_VERIFIED = 0,
    XFER_LIST_UNVERIFIED,
    /* Skipped and unverified entries, in plan order: the report's rows. */
    XFER_LIST_MISSING,
    /* Verified entries whose whole subtree verified, children first (KTD8). */
    XFER_LIST_DELETE
} XferListKind;

typedef struct {
    uint32_t source_off;
    uint32_t dest_off;
    uint16_t parent; /* XFER_PLAN_NO_PARENT for a root */
    uint8_t type;
    uint8_t status;
    uint8_t skip_reason;
    uint8_t size_known;
    /* The destination already holds this name, so the walk will ask the one
     * R41 question. Existing folders merge and are not marked. */
    uint8_t dest_existed;
    uint64_t size;
    /* Bytes the transfer completed for this entry; meaningful after
     * xfer_plan_mark_moved. */
    uint64_t moved;
} XferPlanEntry;

typedef struct {
    int count;  /* every marked conflict, including those past `named` */
    int named;
    Path names[XFER_PLAN_CONFLICT_NAMES];
} XferPlanConflicts;

typedef struct {
    XferPlanDirection direction;

    int ok;
    int finalized;
    char reason[128];

    int entry_count;
    int root_count;
    int file_count;
    int dir_count;
    int skipped_count;
    uint64_t total_bytes;
    /* Every transferable file's size is known (R2's summed known bytes). */
    int bytes_known;

    XferPlanEntry entries[XFER_PLAN_ENTRY_MAX];
    char arena[XFER_PLAN_ARENA_MAX];
    size_t arena_used;

    /* Indices of folders the walk still has to list (KTD2). */
    uint16_t worklist[XFER_PLAN_ENTRY_MAX];
    int work_head;
    int work_tail;

    /* Set for an entry whose subtree holds a skipped or unverified entry. */
    uint8_t subtree_bad[XFER_PLAN_ENTRY_MAX];

    XferPlanConflicts conflicts;
} XferPlan;

/*
 * Create an empty plan on the heap, or NULL when the allocation fails. The
 * plan owns one allocation and is released with xfer_plan_free.
 */
XferPlan *xfer_plan_create(XferPlanDirection direction);
void xfer_plan_free(XferPlan *plan);

/*
 * Builders. Each returns the new entry index, or -1 when the plan refuses:
 * the refusal clears the table and the arena, sets `reason`, and makes every
 * later builder fail, so no partial plan can run.
 *
 * add_root adds one user-selected source. add_child composes both paths from
 * the parent and checks the name; a name that cannot cross the boundary is
 * recorded as one skipped entry (R5) and returned, never expanded.
 * add_skip records an explicit whole-subtree miss with its reason.
 */
int xfer_plan_add_root(XferPlan *plan, const char *source, const char *dest,
                       XferEntryType type, uint64_t size, int size_known);
int xfer_plan_add_child(XferPlan *plan, int parent, const char *name,
                        XferEntryType type, uint64_t size, int size_known);
int xfer_plan_add_skip(XferPlan *plan, int parent, const char *source,
                       const char *dest, XferEntryType type,
                       XferSkipReason reason);

/* The next folder to list, or -1 when the walk is complete. */
int xfer_plan_next_dir(XferPlan *plan);

/*
 * Mark a destination entry as already present for the single R41 question.
 * The finalizer collects at most XFER_PLAN_CONFLICT_NAMES names and the full
 * count.
 */
void xfer_plan_note_dest_exists(XferPlan *plan, int entry);

/*
 * Close the walk: refuse a plan with unexpanded folders or no entries, then
 * compute the totals and the conflict set. Returns 1 when the plan may run.
 */
int xfer_plan_finalize(XferPlan *plan);

int xfer_plan_parent(const XferPlan *plan, int entry);
const char *xfer_plan_source(const XferPlan *plan, int entry);
const char *xfer_plan_dest(const XferPlan *plan, int entry);
const char *xfer_skip_reason_text(XferSkipReason reason);

/* True when the entry and every descendant verified (KTD8's delete guard). */
int xfer_plan_subtree_verified(const XferPlan *plan, int entry);

/*
 * Record the bytes the transfer completed for a file. A verification compares
 * this count to the destination's reported size (KTD4). Returns 0 for a
 * directory or an entry that is not awaiting verification.
 */
int xfer_plan_mark_moved(XferPlan *plan, int entry, uint64_t bytes);

/*
 * Verification streams one destination folder's listing. `parent` is the
 * planned folder entry, or XFER_PLAN_NO_INDEX for root entries. Matching is
 * exact-name first; a case-only match, a wrong type, an unconfirmed size, or
 * an entry that never transferred marks the source unverified. Extra
 * destination rows are ignored.
 */
XferVerifyResult xfer_plan_verify_row(XferPlan *plan, int parent,
                                      const char *name, XferEntryType type,
                                      uint64_t size, int size_known);
/* The listing was read to the end: absent children are unverified, and a root
 * folder is verified by its own successful listing. */
void xfer_plan_verify_folder_end(XferPlan *plan, int parent);
/* The listing could not be read: every child stays unverified (R13). */
void xfer_plan_verify_folder_failed(XferPlan *plan, int parent);

/* True when every non-skipped entry verified; skipped sources stay put. */
int xfer_plan_move_ready(const XferPlan *plan);

int xfer_plan_list_count(const XferPlan *plan, XferListKind kind);
int xfer_plan_list_at(const XferPlan *plan, XferListKind kind, int index);

/*
 * Format one report row's path (R21): the local path relative to
 * `report_root`, its full local path when it lies outside, or the remote path
 * prefixed with `REMOTE ` when a download skip never got a local destination.
 * Control bytes are escaped as \xNN so one entry stays one line. Returns the
 * length written, or -1 (and an empty string) when the row does not fit.
 */
int xfer_plan_report_row(const XferPlan *plan, int entry,
                         const char *report_root, char *out, size_t cap);

#endif
