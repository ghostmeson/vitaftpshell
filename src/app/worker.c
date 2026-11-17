/* SPDX-License-Identifier: GPL-3.0-only */

#include "app/worker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cause.h"
#include "core/fmt.h"
#include "core/ftp/ftp.h"
#include "core/ftp/limits.h"
#include "core/fsop_plan.h"
#include "core/fsop_run.h"
#include "core/logev.h"
#include "core/path.h"
#include "core/reserve.h"
#include "core/store.h"

#ifndef VFTPSHELL_VERSION
#define VFTPSHELL_VERSION "dev"
#endif
#ifndef VFTPSHELL_BUILD
#define VFTPSHELL_BUILD 0
#endif

#define WORKER_CMD_BUDGET 8
/* A batch terminal waits this long for any last item to land before it books
 * the remainder as failed. */
#define WORKER_BATCH_IDLE_MS 250
/* One local slice per worker step; the network engine ticks before it. */
#define WORKER_NET_WAIT_MS 50
#define WORKER_LOCAL_BYTES_PER_STEP (64u * 1024u)
#define WORKER_LOCAL_SLICE_MS 6
#define WORKER_LOCAL_STEPS_MAX 16
#define WORKER_CONFIRM_ITEMS HANDOFF_CONFIRM_ITEMS
/* KTD7: one local item changes at most two parent folders, and one clipboard
 * batch carries UI_CLIP_MAX items, so the deduped set cannot overflow. */
#define WORKER_CHANGED_DIRS_MAX (UI_CLIP_MAX * 2)
#define WORKER_FOLDER_GONE_TEXT "FOLDER GONE"
/* The editor and the start path share the server's own range, so a port the
 * user can enter is a port the server can bind. */
#define WORKER_SERVER_PORT_MIN FTP_SERVER_PORT_MIN
#define WORKER_SERVER_PORT_MAX FTP_SERVER_PORT_MAX

/* Which prompt owns the answer. A bare int let the local-action arm vanish
 * without a compiler warning; the enum makes a missing case loud. */
typedef enum {
    PENDING_NONE = 0,
    PENDING_TRANSFER,
    PENDING_LOCAL_CONFLICT,
    PENDING_BATCH_CONFLICT,
    PENDING_BUSY,
    PENDING_REMOTE_WAIT
} PendingKind;

/* What a preflight parent-listing refresh did. The caller must tell them apart:
 * a held command slot is retried, a started listing owns the lane until it
 * ends, and an uncollected listing leaves the destination untyped (KTD5). */
typedef enum {
    UPLOAD_REFRESH_UNTYPED = 0,
    UPLOAD_REFRESH_BUSY,
    UPLOAD_REFRESH_STARTED,
    UPLOAD_REFRESH_UNCOLLECTED
} UploadRefresh;

typedef struct {
    int valid;
    PendingKind kind;
    unsigned prompt_id;
    UiCommand command;
    XferItem item;
    FsOpConflict conflict;
} PendingWork;

typedef struct {
    int active;
    unsigned id;
    unsigned dropped_id;
    int total;
    int index;
    int done;
    int skipped;
    int failed;
    int cancelled;
    XferDirection direction;
    uint64_t started_ms;
    uint64_t last_ms;
    uint64_t bytes_done;
    uint64_t bytes_total;
    int total_known;
} LocalBatch;

/* One row of a local confirm. A batch names its real replacements first and
 * then the items already in the destination, which stay (R19, KTD6). */
typedef struct {
    Path path;
    uint64_t size;
    int size_known;
    int stays;
} PromptItem;

/*
 * U2: the private remote walk (KTD3). It borrows the caller's plan and root
 * paths and fills plan entries from listings it starts itself; the pane's
 * rows, path, and generation are never written. `request` is the walk's own
 * listing in flight: `token` identifies the walk, and the pair keeps a user
 * listing of the same path from entering the plan. Everything is reset by the
 * next begin, so a cancelled walk leaves only its unfinished plan behind.
 */
typedef struct {
    int active;      /* folders are still being listed */
    int done;        /* the walk finalized the plan and it may run */
    int cancel;      /* a cancel latched before the walk stopped */
    int abort;       /* the plan refused a row; stop at the next pump */
    XferPlan *plan;  /* borrowed; the caller frees it */
    unsigned token;  /* this walk's identity, moved on by begin and cancel */
    unsigned request; /* the token of the listing the walk started, else 0 */
    char roots[WORKER_WALK_ROOTS][PATH_MAX_LEN]; /* normalized remote roots */
    int root_count;
    char path[PATH_MAX_LEN]; /* the folder the current listing fills */
    int dir;                 /* the plan entry the current listing fills */
    int retry_dir;           /* a popped folder whose request must be retried */
    int folders;             /* folders listed to the end */
    int rows;                /* rows collected for the current folder */
    int overflow;            /* rows past WORKER_LIST_CAP in the folder */
    char refusal[128];       /* the last refusal or stop reason */
} Walk;

struct Worker {
    WorkerPorts ports;
    Handoff *handoff;
    FtpClient *client;
    FtpServer *server;
    XferQueue queue;
    SiteBook book;
    Settings settings;

    int ready;
    int done;
    int save_unavailable;
    int site_dirty;
    int settings_dirty;
    uint32_t site_generation;

    char local_path[UI_PANE_COUNT][PATH_MAX_LEN];
    char remote_path[PATH_MAX_LEN];
    char store_dir[PATH_MAX_LEN];
    /* KTD7: the exact parent folders the running local batch has changed.
     * The terminal reloads only the local views that show one of them. */
    Path changed_dirs[WORKER_CHANGED_DIRS_MAX];
    int changed_dir_count;

    /* Free space is a device property. Query it once per volume (and refresh
     * at the volume root), not on every subfolder; ur0:/uma0: stay unknown. */
    uint64_t volume_free_bytes[VOLUME_COUNT];
    int volume_free_valid[VOLUME_COUNT];

    Site connecting_site;
    int connecting_valid;
    int last_site_index;
    int connected;
    int initial_list;
    /* A completed remote command cannot start its listing from inside the
     * client event (the operation slot is still held). Set this and let the
     * next worker step refresh once the engine is idle. */
    int refresh_remote;
    int remote_action_count;
    UiCommand remote_actions[UI_CONFIRM_MAX];

    UiRow *rows; /* [pane][generation][WORKER_LIST_CAP] */

    int collecting;
    UiPane collect_pane;
    UiPane listing_pane; /* the view that asked for the running remote list */
    int collect_slot;
    int collect_count;
    int remote_row_count;
    int collect_truncated;
    /* R18: the current remote folder's write-capability facts, learned from an
     * MLSD cdir/pdir row or an MLST after connect, navigation, and refresh.
     * The folder walk will stamp destination folders from the same listing
     * mask (KTD7). */
    int remote_perm_known;
    int remote_perm_mask;
    int remote_perm_pending;
    int remote_perm_active;

    FsOpFact facts[WORKER_FACTS_MAX];
    char fact_paths[WORKER_FACTS_MAX][PATH_MAX_LEN];
    int fact_count;
    int facts_truncated;

    PendingWork pending;
    int pending_command_count;
    UiCommand pending_commands[64];
    int batch_conflict_decided;
    XferConflict batch_conflict;
    int batch_total;
    int batch_done;
    int batch_skipped;
    int batch_failed;
    XferDirection batch_direction;
    FsOpConflict local_conflict;
    int local_conflict_set;
    int local_cancel_seen;
    /* The resumable local job (U25): the run, its owned plan, and the command
     * that started it survive across worker steps. */
    FsOpRun *local_run;
    FsOpPlan *local_plan;
    UiCommand local_run_command;
    uint64_t local_run_started_ms;
    uint64_t local_run_last_bytes;
    int local_cancel_requested;
    unsigned event_lost_seen;
    int local_batch_cancel_requested;
    /* U26: reservations between the lanes, and the item waiting on one. */
    Reserve *reserve;
    UiCommand busy_command;
    FsOpPlan *busy_plan;
    char busy_blocker[PATH_MAX_LEN];
    int busy_waiting;
    unsigned prompt_seq;
    /* The network item is held by a local path and retries head-of-line. */
    int network_waiting;
    LocalBatch local_batch;
    UiCommand batch_preflight[UI_CLIP_MAX];
    /* The one local prompt a batch may ask. Kept in the heap-owned Worker: a
     * PromptItem is large, and sixteen on handle_action's stack would blow the
     * worker budget (b40 crash, D77). Replacements come first; the items
     * already in the destination follow and stay (R19, KTD6). */
    PromptItem prompt_items[WORKER_CONFIRM_ITEMS];
    int prompt_count;
    int batch_preflight_count;
    unsigned batch_preflight_id;
    int batch_executing;
    int batch_execute_index;
    int transfer_active;
    int transfer_cancelled;
    uint64_t transfer_start_ms;
    uint64_t transfer_last_bytes;
    int upload_stat_active;
    int upload_stat_ready;
    int upload_stat_failed;
    OpCause upload_stat_cause;
    OpStage upload_stat_stage;
    int upload_stat_exists;
    int upload_stat_is_dir;
    int upload_stat_unsupported;
    /* The preflight has used SIZE for this session (MLST was refused). */
    int upload_stat_used_size;
    /* Neither MLST nor SIZE is available; the session goes straight to STOR. */
    int upload_stat_disabled;
    /* KTD3: the answer belongs to the item it was asked for, so the item is
     * named by a token rather than by its path. `batch_reset` and every new
     * preflight move the token on, and the request records the value it was
     * asked under; an answer that outlives its item therefore matches nothing,
     * even when the next batch reuses the same destination path. */
    unsigned upload_stat_token;
    unsigned upload_stat_answer_token;
    /* The destination the pending answer describes. Identity is the token; the
     * path only says which destination the answer is about. */
    char upload_stat_path[PATH_MAX_LEN];
    /* R9: one parent refresh per item, the latch that owns the network lane
     * while that listing runs, and the flag that hands the decision back to
     * pump_queue once the listing ends. */
    int upload_stat_refresh_used;
    int upload_stat_refresh_active;
    int upload_stat_refresh_done;

    int server_transfer_active;
    XferDirection server_direction;
    char server_name[PATH_NAME_MAX];
    uint64_t server_transfer_start_ms;

    Walk walk; /* U2: one folder transfer's scan, or all-zero when idle */

    char last_error[160];
    unsigned log_lost_seen;
};

static void handle_action(Worker *worker, const UiCommand *command);
static void handle_transfer(Worker *worker, const UiCommand *command);
static void log_error(Worker *worker, UiPane pane, const char *text,
                      OpCause cause, OpStage stage);
static void log_detail(Worker *worker, LogEvent kind, const char *detail);
static void enqueue_transfer(Worker *worker, const UiCommand *command);
static void pump_queue(Worker *worker);
static void upload_preflight_clear(Worker *worker);
static void start_transfer_item(Worker *worker, const XferItem *item);
static void pump_remote_actions(Worker *worker);
static void worker_list_local(Worker *worker, UiPane pane, const char *path);
static void worker_list_root(Worker *worker, UiPane pane);
static UiPane local_view(const UiCommand *command);
static int source_in_folder(const char *source, const char *folder);
static const char *last_name(const char *path);
static int source_in_folder(const char *source, const char *folder);
static void queue_pending_action(Worker *worker, const UiCommand *command);
static void local_batch_advance(Worker *worker);
static void local_batch_complete(Worker *worker);
static void changed_dir_add(Worker *worker, const char *dir);
static void changed_dir_add_parent(Worker *worker, const char *path);
static void reload_views_showing(Worker *worker, const char *folder);
static int run_plan_begin(Worker *worker, const UiCommand *command,
                          FsOpPlan *plan);
static void execute_action(Worker *worker, const UiCommand *command,
                           FsOpConflict conflict);

static uint64_t now_ms(Worker *worker)
{
    return worker->ports.clock->now_ms(worker->ports.clock->ctx);
}

/* U26: the FTP server asks the reservation table before a data or mutating
 * command. CHECK refuses a held path; HOLD claims the transfer's path;
 * RELEASE drops the server lane when the transfer ends. */
static int server_path_guard(void *ctx, FtpGuardAction action,
                             const char *path, int write, int tree)
{
    Worker *worker = ctx;
    if (worker == NULL)
        return 0;

    if (action == FTP_GUARD_RELEASE) {
        reserve_release(worker->reserve, RESERVE_LANE_SERVER);
        return 0;
    }
    if (path == NULL)
        return 0;

    ReserveKind kind =
        tree ? RESERVE_TREE : (write ? RESERVE_WRITE : RESERVE_READ);
    if (action == FTP_GUARD_HOLD) {
        return reserve_add(worker->reserve, RESERVE_LANE_SERVER, kind, path)
                   ? 0
                   : 1;
    }

    char blocker[PATH_MAX_LEN];
    return reserve_conflict(worker->reserve, kind, path, blocker,
                            sizeof(blocker));
}

static UiRow *rows_for(Worker *worker, UiPane pane, int slot)
{
    return &worker->rows[((int)pane * HANDOFF_LIST_GENS + slot) *
                          WORKER_LIST_CAP];
}

/* A local command names the view that issued it; an old or partial command
 * falls back to the left local view. */
static UiPane local_view(const UiCommand *command)
{
    if (command == NULL)
        return UI_PANE_LOCAL;
    return ui_pane_is_local(command->pane) ? command->pane : UI_PANE_LOCAL;
}

/* Iterate the local views by role instead of repeating the filter-and-skip
 * rule at every call site (KTD1). */
static int local_view_count(void)
{
    int count = 0;
    for (int i = 0; i < UI_PANE_COUNT; ++i) {
        if (ui_pane_is_local((UiPane)i))
            count++;
    }
    return count;
}

static UiPane local_view_at(int index)
{
    int seen = 0;
    for (int i = 0; i < UI_PANE_COUNT; ++i) {
        if (!ui_pane_is_local((UiPane)i))
            continue;
        if (seen == index)
            return (UiPane)i;
        seen++;
    }
    return UI_PANE_LOCAL;
}

static int local_index(UiPane pane)
{
    return ui_pane_is_local(pane) ? (int)pane : (int)UI_PANE_LOCAL;
}

static void event_init(HandoffEvent *event, HandoffEventKind kind)
{
    memset(event, 0, sizeof(*event));
    event->kind = kind;
}

static void emit(Worker *worker, const HandoffEvent *event)
{
    handoff_event_push(worker->handoff, event);
    unsigned lost = handoff_event_lost(worker->handoff);
    if (lost > worker->event_lost_seen) {
        worker->event_lost_seen = lost;
        log_detail(worker, LOGEV_ERROR, "event ring lost a record");
    }
}

/* Every worker prompt gets one identity; the answer must echo it so a local
 * dialog can never consume a deferred transfer prompt. */
static void confirm_request(Worker *worker, HandoffConfirm *request)
{
    request->id = ++worker->prompt_seq;
    worker->pending.prompt_id = request->id;
    handoff_confirm_request(worker->handoff, request);
}

static void emit_error(Worker *worker, UiPane pane, const char *text,
                       OpCause cause, OpStage stage)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_ERROR);
    event.pane = pane;
    event.cause = cause;
    event.stage = stage;
    event.ok = 0;
    fmt_copy(event.detail, sizeof(event.detail), text != NULL ? text : "ERROR");
    emit(worker, &event);
    fmt_copy(worker->last_error, sizeof(worker->last_error), event.detail);
    log_error(worker, pane, event.detail, cause, stage);
}

static XferDirection action_direction(UiAction action)
{
    switch (action) {
    case UI_ACTION_MOVE:
        return XFER_DIR_LOCAL_MOVE;
    case UI_ACTION_DELETE:
        return XFER_DIR_LOCAL_DELETE;
    case UI_ACTION_RENAME:
        return XFER_DIR_LOCAL_RENAME;
    case UI_ACTION_NEW_FOLDER:
        return XFER_DIR_LOCAL_MKDIR;
    default:
        return XFER_DIR_LOCAL_COPY;
    }
}

static void local_batch_begin(Worker *worker, const UiCommand *command,
                              XferDirection direction)
{
    unsigned id = command->batch_id;
    int total = command->batch_total > 0 ? command->batch_total : 1;

    if (id != 0 && id == worker->local_batch.dropped_id)
        return;
    if (worker->local_batch.active && worker->local_batch.id == id) {
        worker->local_batch.index = command->batch_index;
        worker->local_batch.last_ms = now_ms(worker);
        return;
    }

    memset(&worker->local_batch, 0, sizeof(worker->local_batch));
    worker->local_batch.active = 1;
    worker->local_batch.id = id;
    worker->local_batch.total = total;
    worker->local_batch.index = command->batch_index;
    worker->local_batch.direction = direction;
    worker->local_conflict = FSOP_CONFLICT_ASK;
    worker->local_conflict_set = 0;
    worker->batch_executing = 0;
    worker->batch_execute_index = 0;
    worker->batch_preflight_count = 0;
    worker->batch_preflight_id = 0;
    worker->local_batch.started_ms = now_ms(worker);
    worker->local_batch.last_ms = worker->local_batch.started_ms;
    worker->local_batch.total_known = 1;
    worker->changed_dir_count = 0;
}

static void local_batch_emit_done(Worker *worker)
{
    LocalBatch *batch = &worker->local_batch;
    if (!batch->active)
        return;

    HandoffEvent done;
    event_init(&done, HANDOFF_EVT_BATCH_DONE);
    done.ok = batch->failed == 0 && batch->cancelled == 0;
    done.direction = batch->direction;
    done.item_index = batch->done;
    done.item_total = batch->total;
    done.batch_skipped = batch->skipped;
    done.batch_failed = batch->failed;
    done.batch_cancelled = batch->cancelled;
    done.bytes = batch->bytes_done;
    done.total = batch->bytes_total;
    done.total_known = batch->total_known;
    uint64_t elapsed = now_ms(worker) - batch->started_ms;
    fmt_copy(done.detail, sizeof(done.detail), worker->last_error);
    done.elapsed_ms = elapsed;
    done.elapsed_known = elapsed > 0;
    if (elapsed > 0 && batch->bytes_done > 0) {
        done.rate = batch->bytes_done * 1000u / elapsed;
        done.rate_known = 1;
    }
    emit(worker, &done);

    unsigned dropped = batch->id;
    memset(batch, 0, sizeof(*batch));
    batch->dropped_id = dropped;
    worker->batch_executing = 0;
    worker->batch_execute_index = 0;
    worker->batch_preflight_count = 0;
    worker->batch_preflight_id = 0;
}

/* KTD7: command paths are already canonical, so recording a parent never walks
 * or widens the set: an operation inside ux0:data changes ux0:data, never
 * ux0:. The candidate is stored in the next slot; commit keeps the set unique
 * and counts it only when it is new. */
static void changed_dir_commit(Worker *worker)
{
    const Path *candidate = &worker->changed_dirs[worker->changed_dir_count];
    for (int i = 0; i < worker->changed_dir_count; ++i) {
        if (path_ci_equal(worker->changed_dirs[i].text, candidate->text))
            return;
    }
    worker->changed_dir_count++;
}

/* The folder itself changed (a copy or move destination). */
static void changed_dir_add(Worker *worker, const char *dir)
{
    if (dir == NULL || dir[0] == '\0' || strcmp(dir, "*") == 0)
        return;
    if (worker->changed_dir_count >= WORKER_CHANGED_DIRS_MAX)
        return;
    Path *slot = &worker->changed_dirs[worker->changed_dir_count];
    if (path_canonicalize(dir, slot) != PATH_OK)
        return;
    changed_dir_commit(worker);
}

/* The parent of a changed path (a delete, mkdir, or rename operand). */
static void changed_dir_add_parent(Worker *worker, const char *path)
{
    if (path == NULL || path[0] == '\0' || strcmp(path, "*") == 0)
        return;
    if (worker->changed_dir_count >= WORKER_CHANGED_DIRS_MAX)
        return;
    Path *slot = &worker->changed_dirs[worker->changed_dir_count];
    if (path_parent(path, slot) != PATH_OK)
        return;
    changed_dir_commit(worker);
}

/* Reload every local view whose current folder is exactly this folder. */
static void reload_views_showing(Worker *worker, const char *folder)
{
    for (int n = 0; n < local_view_count(); ++n) {
        UiPane view = local_view_at(n);
        const char *path = worker->local_path[view];
        if (strcmp(path, "*") == 0)
            continue;
        if (path_ci_equal(path, folder))
            worker_list_local(worker, view, path);
    }
}

/* One terminal, then one reload for each affected view and none for the rest
 * (R9, KTD7). A view is reloaded at most once because the view loop owns the
 * outer pass. */
static void local_batch_complete(Worker *worker)
{
    if (!worker->local_batch.active)
        return;
    local_batch_emit_done(worker);
    for (int n = 0; n < local_view_count(); ++n) {
        UiPane view = local_view_at(n);
        const char *path = worker->local_path[view];
        if (strcmp(path, "*") == 0)
            continue;
        for (int c = 0; c < worker->changed_dir_count; ++c) {
            if (path_ci_equal(path, worker->changed_dirs[c].text)) {
                worker_list_local(worker, view, path);
                break;
            }
        }
    }
    worker->changed_dir_count = 0;
}

static void local_batch_record(Worker *worker, FsOpRunResult result,
                               const FsOpPlan *plan,
                               const FsOpProgress *progress)
{
    LocalBatch *batch = &worker->local_batch;
    if (!batch->active || progress == NULL)
        return;

    if (result == FSOP_RUN_OK) {
        if (plan != NULL && plan->step_count == 0 &&
            plan->skipped_count > 0)
            batch->skipped++;
        else
            batch->done++;
    } else if (result == FSOP_RUN_CANCELLED) {
        batch->cancelled++;
    } else {
        batch->failed++;
    }
    batch->bytes_done += progress->bytes_done;
    batch->bytes_total += progress->bytes_total;
    if (!progress->total_known)
        batch->total_known = 0;
    batch->last_ms = now_ms(worker);
}

static void local_batch_skip_item(Worker *worker, const UiCommand *command)
{
    LocalBatch *batch = &worker->local_batch;
    if (!batch->active)
        return;
    batch->skipped++;
    batch->last_ms = now_ms(worker);
    if (command->batch_last || batch->index + 1 >= batch->total)
        local_batch_complete(worker);
}

static void local_batch_cancel(Worker *worker)
{
    LocalBatch *batch = &worker->local_batch;
    if (!batch->active)
        return;
    int missing = batch->total - batch->done - batch->skipped -
                  batch->failed - batch->cancelled;
    if (missing > 0)
        batch->cancelled += missing;
    local_batch_complete(worker);
    worker->pending_command_count = 0;
    worker->batch_execute_index = 0;
    worker->batch_preflight_count = 0;
    worker->batch_executing = 0;
    free(worker->busy_plan);
    worker->busy_plan = NULL;
    worker->busy_waiting = 0;
}

static void local_batch_flush(Worker *worker)
{
    LocalBatch *batch = &worker->local_batch;
    if (!batch->active || worker->pending.valid ||
        worker->pending_command_count > 0 || worker->batch_executing ||
        worker->local_run != NULL || worker->busy_plan != NULL)
        return;
    if (now_ms(worker) - batch->last_ms < WORKER_BATCH_IDLE_MS)
        return;
    int missing = batch->total - batch->done - batch->skipped -
                  batch->failed - batch->cancelled;
    if (missing > 0)
        batch->failed += missing;
    local_batch_complete(worker);
}

static int local_cancel_requested(void *ctx)
{
    Worker *worker = ctx;
    if (worker == NULL)
        return 0;
    if (worker->local_cancel_requested) {
        worker->local_cancel_seen = 1;
        return 1;
    }
    if (worker->handoff == NULL)
        return 0;
    int type = handoff_cancel_type(worker->handoff, HANDOFF_CANCEL_LOCAL);
    if (type != UI_CMD_CANCEL_TRANSFER && type != UI_CMD_CANCEL_LOCAL)
        return 0;
    worker->local_cancel_seen = 1;
    /* Consume the press the executor just observed, so it cannot be replayed
     * against another lane or another run. */
    handoff_cancel_consume(worker->handoff, HANDOFF_CANCEL_LOCAL);
    return 1;
}

static int preflight_dest(const UiCommand *command, Path *dest)
{
    if (command->action != UI_ACTION_COPY &&
        command->action != UI_ACTION_MOVE)
        return 0;
    /* A same-folder item resolves to its own path; it is not a replacement,
     * but the preflight must still see it so the batch can ask once (R16). */
    const char *name = last_name(command->path);
    if (!path_name_is_safe(name))
        return 0;
    return path_join(command->path2, name, dest) == PATH_OK;
}

static void prompt_item_add(Worker *worker, const char *path, uint64_t size,
                            int size_known, int stays)
{
    if (worker->prompt_count >= WORKER_CONFIRM_ITEMS)
        return;
    PromptItem *item = &worker->prompt_items[worker->prompt_count++];
    fmt_copy(item->path.text, sizeof(item->path.text), path);
    item->size = size;
    item->size_known = size_known;
    item->stays = stays;
}

/* One request builder for both local questions, so the duplicate and the
 * mixed conflict carry the same item record (KTD6). A prompt never names more
 * items than its view holds: a 64-item paste must not claim 64 in a 16-item
 * list (R16, R19). */
static void request_prompt(Worker *worker, UiConfirmKind kind, UiPane pane,
                           const char *context, int count, int existing_count)
{
    HandoffConfirm request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.valid = 1;
    request.pane = pane;
    fmt_copy(request.context, sizeof(request.context),
             context != NULL ? context : "");
    if (count > WORKER_CONFIRM_ITEMS)
        count = WORKER_CONFIRM_ITEMS;
    if (existing_count > count)
        existing_count = count;
    request.item_count = count;
    request.existing_count = existing_count;
    for (int i = 0; i < count; ++i) {
        request.items[i].size = worker->prompt_items[i].size;
        request.items[i].size_known = worker->prompt_items[i].size_known;
        request.items[i].stays = worker->prompt_items[i].stays;
        fmt_copy(request.items[i].name, sizeof(request.items[i].name),
                 worker->prompt_items[i].path.text);
    }
    confirm_request(worker, &request);
}

static void local_batch_execute(Worker *worker)
{
    /* Hand the preflighted items to worker_step. Each runs from the step frame;
     * nesting a second handle_action frame is what overflowed the worker stack
     * in b40 (D77). */
    worker->batch_executing = 1;
    worker->batch_execute_index = 0;
}

/* Run one preflighted item per worker step, from the step frame rather than
 * from inside another handle_action call. */
static void local_batch_advance(Worker *worker)
{
    LocalBatch *batch = &worker->local_batch;
    if (!batch->active || !worker->batch_executing)
        return;
    if (worker->local_run != NULL || worker->busy_plan != NULL)
        return;
    if (worker->pending.valid || worker->pending_command_count > 0)
        return;
    if (worker->batch_execute_index >= worker->batch_preflight_count) {
        worker->batch_executing = 0;
        worker->batch_execute_index = 0;
        worker->batch_preflight_count = 0;
        /* A refused item never reaches run_plan and therefore never records
         * itself; flush the batch here instead of waiting for the idle timer. */
        if (batch->active)
            local_batch_complete(worker);
        return;
    }

    int before = batch->done + batch->skipped + batch->failed + batch->cancelled;
    UiCommand command = worker->batch_preflight[worker->batch_execute_index++];
    handle_action(worker, &command);

    /* No plan ran and no prompt is waiting: the item was refused before it
     * could execute (busy slot, bad name, plan refused). Count it so the
     * terminal reports the batch honestly. A pending prompt records the item
     * when it is answered. */
    if (worker->local_run != NULL)
        return;
    if (batch->active && !worker->pending.valid &&
        before == batch->done + batch->skipped + batch->failed +
                      batch->cancelled)
        batch->failed++;
}

static void local_batch_preflight(Worker *worker)
{
    UiPane pane = local_view(&worker->batch_preflight[0]);
    const char *destination = worker->batch_preflight[0].path2;
    worker->prompt_count = 0;

    /* The destinations that would actually be replaced come first. A folder
     * destination is not a replacement: the planner merges into it. */
    for (int i = 0; i < worker->batch_preflight_count; ++i) {
        const UiCommand *pre = &worker->batch_preflight[i];
        Path dest;
        if (!preflight_dest(pre, &dest))
            continue;
        if (source_in_folder(pre->path, pre->path2))
            continue;
        FsEntry entry;
        memset(&entry, 0, sizeof(entry));
        if (worker->ports.fs->stat(worker->ports.fs->ctx, dest.text,
                                   &entry) != FS_OK ||
            entry.type == FS_ENTRY_DIR)
            continue;
        int duplicate = 0;
        for (int j = 0; j < worker->prompt_count; ++j) {
            if (path_ci_equal(worker->prompt_items[j].path.text, dest.text)) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate)
            prompt_item_add(worker, dest.text, entry.size, entry.size_known,
                            0);
    }
    int existing_count = worker->prompt_count;

    /* Every collision already lives here and the action is a copy: the
     * duplicate question, not the overwrite question (R16). A MOVE has no
     * duplicate answer; its already-here item is skipped by the plan (R18). */
    if (existing_count == 0 &&
        worker->batch_preflight[0].action == UI_ACTION_COPY) {
        for (int i = 0; i < worker->batch_preflight_count; ++i) {
            const UiCommand *pre = &worker->batch_preflight[i];
            Path dest;
            if (!preflight_dest(pre, &dest))
                continue;
            if (!source_in_folder(pre->path, pre->path2))
                continue;
            FsEntry entry;
            memset(&entry, 0, sizeof(entry));
            int stat_ok = worker->ports.fs->stat(worker->ports.fs->ctx,
                                                 dest.text, &entry) == FS_OK;
            prompt_item_add(worker, dest.text, stat_ok ? entry.size : 0,
                            stat_ok ? entry.size_known : 0, 0);
        }
        if (worker->prompt_count > 0) {
            request_prompt(worker, UI_CONFIRM_DUPLICATE, pane, destination,
                           worker->prompt_count, 0);
            worker->pending.valid = 1;
            worker->pending.kind = PENDING_BATCH_CONFLICT;
            worker->pending.conflict = FSOP_CONFLICT_DUPLICATE;
            return;
        }
    }

    if (existing_count == 0) {
        local_batch_execute(worker);
        return;
    }

    /* A mixed batch: the replacements were counted above; the items already
     * here are appended and named as staying, so the prompt never claims to
     * replace them (R19, AE5). */
    for (int i = 0; i < worker->batch_preflight_count; ++i) {
        const UiCommand *pre = &worker->batch_preflight[i];
        Path dest;
        if (!preflight_dest(pre, &dest))
            continue;
        if (!source_in_folder(pre->path, pre->path2))
            continue;
        prompt_item_add(worker, dest.text, 0, 0, 1);
    }

    request_prompt(worker, UI_CONFIRM_CONFLICT, pane,
                   ui_action_context(worker->batch_preflight[0].action),
                   worker->prompt_count, existing_count);

    worker->pending.valid = 1;
    worker->pending.kind = PENDING_BATCH_CONFLICT;
    worker->pending.conflict = FSOP_CONFLICT_ASK;
}

static void emit_session(Worker *worker, int connected, const char *detail)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_SESSION);
    event.connected = connected;
    event.ok = connected;
    event.pane = UI_PANE_REMOTE;
    if (connected && worker->connecting_valid) {
        fmt_copy(event.detail, sizeof(event.detail), worker->connecting_site.name);
        fmt_copy(event.path, sizeof(event.path), worker->connecting_site.host);
        event.port = worker->connecting_site.port;
    } else if (detail != NULL) {
        fmt_copy(event.detail, sizeof(event.detail), detail);
    }
    emit(worker, &event);
}

static void format_ip(uint32_t addr, char *out, size_t cap)
{
    out[0] = '\0';
    if (addr == 0)
        return;
    snprintf(out, cap, "%u.%u.%u.%u",
             (unsigned)((addr >> 24) & 0xFF),
             (unsigned)((addr >> 16) & 0xFF),
             (unsigned)((addr >> 8) & 0xFF),
             (unsigned)(addr & 0xFF));
}

static void emit_server_state(Worker *worker, UiServerRun run, int clients)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_SERVER_STATE);
    event.server_run = run;
    event.clients = clients;
    event.value = (int)run;
    event.port = worker->settings.server_port;

    NetAddress lan = ftp_server_lan_address(worker->server);
    if (run == UI_SERVER_ON && lan.addr != 0) {
        format_ip(lan.addr, event.address, sizeof(event.address));
        event.address_known = event.address[0] != '\0';
    } else {
        event.address[0] = '\0';
        event.address_known = 0;
    }

    handoff_latch_server(worker->handoff, run, clients);
    emit(worker, &event);
}

static void emit_listing_begin(Worker *worker, UiPane pane, const char *path)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_LISTING_BEGIN);
    event.pane = pane;
    fmt_copy(event.path, sizeof(event.path), path != NULL ? path : "");
    emit(worker, &event);
}

static void emit_listing_end(Worker *worker, UiPane pane, const char *path,
                             int ok, int truncated, uint64_t free_bytes,
                             int free_known, const char *detail)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_LISTING_END);
    event.pane = pane;
    event.ok = ok;
    event.truncated = truncated;
    event.free_bytes = free_bytes;
    event.free_known = free_known;
    fmt_copy(event.path, sizeof(event.path), path != NULL ? path : "");
    fmt_copy(event.detail, sizeof(event.detail),
             detail != NULL ? detail : (ok ? "" : "LISTING FAILED"));
    emit(worker, &event);
}

static const char *fs_result_text(FsResult result)
{
    switch (result) {
    case FS_NOT_FOUND:
        return "NOT FOUND";
    case FS_DENIED:
        return "DENIED";
    case FS_EXISTS:
        return "EXISTS";
    case FS_NOT_EMPTY:
        return "NOT EMPTY";
    case FS_NO_SPACE:
        return "NO SPACE";
    case FS_BUSY:
        return "BUSY";
    case FS_INVALID:
        return "INVALID PATH";
    case FS_UNSUPPORTED:
        return "UNSUPPORTED";
    case FS_IO:
        return "IO ERROR";
    default:
        return "LISTING FAILED";
    }
}

/* A finished download changes its destination folder, and a kept file changes
 * it too. Reload every local view that shows that exact folder and leave the
 * others alone (R9, KTD7). */
static void refresh_local_after_download(Worker *worker,
                                         const XferItem *item)
{
    Path parent;
    if (path_parent(item->destination.text, &parent) == PATH_OK)
        reload_views_showing(worker, parent.text);
}

static void emit_transfer_done(Worker *worker, XferDirection direction,
                               const char *name, int ok, int file_kept,
                               uint64_t bytes, uint64_t total, int total_known,
                               OpCause cause, OpStage stage, uint64_t start_ms,
                               const char *detail)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_TRANSFER_DONE);
    event.ok = ok;
    event.file_kept = file_kept;
    event.direction = direction;
    event.direction_known = 1;
    event.bytes = bytes;
    event.total = total;
    event.total_known = total_known;
    event.cause = cause;
    event.stage = stage;
    event.item_index = worker->batch_done > 0 ? worker->batch_done : 1;
    event.item_total = worker->batch_total > 0 ? worker->batch_total : 1;
    event.rate_known = 0;
    if (start_ms != 0) {
        uint64_t elapsed = now_ms(worker) - start_ms;
        event.elapsed_ms = elapsed;
        event.elapsed_known = elapsed > 0;
        if (elapsed > 0) {
            event.rate = bytes * 1000u / elapsed;
            event.rate_known = 1;
        }
    }
    fmt_copy(event.path, sizeof(event.path), name != NULL ? name : "");
    if (detail != NULL)
        fmt_copy(event.detail, sizeof(event.detail), detail);
    emit(worker, &event);
}

/* --- paths ------------------------------------------------------------- */

static const char *last_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == ':')
            name = p + 1;
    }
    return name;
}

/* A remote listing supplies one name; it may not smuggle a local path. R44. */

static const char *remote_last_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/')
            name = p + 1;
    }
    return name;
}

static int remote_join(const char *dir, const char *name, char *out,
                       size_t cap)
{
    int n;
    if (strcmp(dir, "/") == 0)
        n = snprintf(out, cap, "/%s", name);
    else
        n = snprintf(out, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

static int remote_parent(const char *path, char *out, size_t cap)
{
    if (strcmp(path, "/") == 0) {
        snprintf(out, cap, "/");
        return 0;
    }
    const char *slash = NULL;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/')
            slash = p;
    }
    if (slash == NULL || slash == path) {
        snprintf(out, cap, "/");
        return 1;
    }
    size_t len = (size_t)(slash - path);
    if (len >= cap)
        return 0;
    memcpy(out, path, len);
    out[len] = '\0';
    return 1;
}

static void format_peer(NetAddress address, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u", (unsigned)((address.addr >> 24) & 0xFF),
             (unsigned)((address.addr >> 16) & 0xFF),
             (unsigned)((address.addr >> 8) & 0xFF),
             (unsigned)(address.addr & 0xFF));
}

/* --- logging ----------------------------------------------------------- */

static void log_event(Worker *worker, LogEvent kind, const char *site,
                      const char *host, const char *path, const char *stage,
                      const char *cls, uint64_t bytes, uint64_t total)
{
    if (worker->ports.log == NULL)
        return;
    LogRecord record;
    logev_init(&record, kind);
    record.site = site;
    record.host = host;
    record.path = path;
    record.stage = stage;
    record.cls = cls;
    record.bytes = bytes;
    record.total = total;
    record.port = -1;
    record.clients = -1;
    (void)logev_emit(worker->ports.log, now_ms(worker), &record);
}

static void log_detail(Worker *worker, LogEvent kind, const char *detail)
{
    if (worker->ports.log == NULL)
        return;
    LogRecord record;
    logev_init(&record, kind);
    record.detail = detail;
    (void)logev_emit(worker->ports.log, now_ms(worker), &record);
}

static void log_error(Worker *worker, UiPane pane, const char *text,
                      OpCause cause, OpStage stage)
{
    if (worker->ports.log == NULL)
        return;
    LogRecord record;
    logev_init(&record, LOGEV_ERROR);
    record.stage = op_stage_name(stage);
    record.cls = op_cause_name(cause);
    record.detail = cause == OP_CAUSE_SERVER ? "server reply withheld" : text;
    record.value = (int)pane;
    (void)logev_emit(worker->ports.log, now_ms(worker), &record);
}

static void log_command_received(Worker *worker, const UiCommand *command)
{
    if (worker->ports.log == NULL)
        return;
    LogRecord record;
    logev_init(&record, LOGEV_COMMAND);
    record.detail = "worker";
    record.command = ui_command_name(command->type);
    record.path = command->path[0] != '\0' ? command->path : NULL;
    record.value = command->value;
    (void)logev_emit(worker->ports.log, now_ms(worker), &record);
}

static void drain_ui_log(Worker *worker)
{
    HandoffLogRecord entry;
    while (handoff_log_pop(worker->handoff, &entry)) {
        LogEvent event = LOGEV_SCREEN;
        switch (entry.kind) {
        case HANDOFF_LOG_KEY:
            event = LOGEV_KEY;
            break;
        case HANDOFF_LOG_COMMAND:
            event = LOGEV_COMMAND;
            break;
        case HANDOFF_LOG_IME:
            event = LOGEV_IME;
            break;
        case HANDOFF_LOG_SCREEN:
        default:
            event = LOGEV_SCREEN;
            break;
        }

        LogRecord record;
        logev_init(&record, event);
        record.screen = entry.screen[0] != '\0' ? entry.screen : NULL;
        record.key = entry.key[0] != '\0' ? entry.key : NULL;
        record.command = entry.command[0] != '\0' ? entry.command : NULL;
        record.path = entry.path[0] != '\0' ? entry.path : NULL;
        record.detail = entry.detail[0] != '\0' ? entry.detail : NULL;
        record.repeat = entry.repeat;
        record.masked = entry.masked;
        if (entry.value_set)
            record.value = entry.value;
        (void)logev_emit(worker->ports.log, now_ms(worker), &record);
    }

    unsigned lost = handoff_log_lost(worker->handoff);
    if (lost != worker->log_lost_seen) {
        LogRecord record;
        logev_init(&record, LOGEV_LOG);
        record.detail = "ui log entries lost";
        record.value = (int)(lost - worker->log_lost_seen);
        (void)logev_emit(worker->ports.log, now_ms(worker), &record);
        worker->log_lost_seen = lost;
    }
}

/* --- local listing ----------------------------------------------------- */

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int row_compare(const void *a, const void *b)
{
    const UiRow *ra = a;
    const UiRow *rb = b;
    int da = ra->type == UI_ROW_DIR;
    int db = rb->type == UI_ROW_DIR;
    if (da != db)
        return da ? -1 : 1;
    const char *pa = ra->name;
    const char *pb = rb->name;
    while (*pa != '\0' && *pb != '\0') {
        char ca = lower_char(*pa);
        char cb = lower_char(*pb);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        pa++;
        pb++;
    }
    return *pa == '\0' ? (*pb == '\0' ? 0 : -1) : 1;
}

static void sort_rows(UiRow *rows, int count, int start)
{
    if (count - start > 1)
        qsort(rows + start, (size_t)(count - start), sizeof(UiRow),
              row_compare);
}

static int row_risk(const char *dir, const char *name)
{
    Path joined;
    if (path_join(dir, name, &joined) != PATH_OK)
        return 0;
    return path_is_high_risk(joined.text) ? PATH_RISK_HIGH : PATH_RISK_NORMAL;
}

static void worker_list_root(Worker *worker, UiPane pane)
{
    emit_listing_begin(worker, pane, "*");
    /* The virtual root is the view's folder even when the listing itself
     * cannot start, so a fallback is never overwritten by a later failure. */
    fmt_copy(worker->local_path[local_index(pane)],
             sizeof(worker->local_path[local_index(pane)]), "*");
    log_event(worker, LOGEV_LIST, NULL, NULL, "*", "begin", "app", 0, 0);
    int slot = handoff_listing_begin(worker->handoff, pane);
    if (slot < 0) {
        emit_listing_end(worker, pane, "*", 0, 0, 0, 0, "LISTING BUSY");
        return;
    }

    UiRow *rows = rows_for(worker, pane, slot);
    int count = 0;
    for (int v = 0; v < VOLUME_COUNT; ++v) {
        const char *name = path_volume_name((Volume)v);
        FsEntry entry;
        if (worker->ports.fs->stat(worker->ports.fs->ctx, name, &entry) !=
                FS_OK ||
            entry.type != FS_ENTRY_DIR)
            continue;
        UiRow *row = &rows[count++];
        memset(row, 0, sizeof(*row));
        fmt_copy(row->name, sizeof(row->name), name);
        row->type = UI_ROW_VOLUME;
        row->readonly = (uint8_t)path_volume_is_read_only((Volume)v);
    }

    handoff_listing_publish(worker->handoff, pane, slot, rows, count);
    log_event(worker, LOGEV_LIST, NULL, NULL, "*", "end", "app",
              (uint64_t)count, 0);
    emit_listing_end(worker, pane, "*", 1, 0, 0, 0, "");
}

static void worker_list_local(Worker *worker, UiPane pane, const char *path)
{
    int index = local_index(pane);
    if (path == NULL)
        path = worker->local_path[index];
    if (path != NULL && strcmp(path, "*") == 0) {
        worker_list_root(worker, pane);
        return;
    }

    Path canonical;
    if (path == NULL || path_canonicalize(path, &canonical) != PATH_OK) {
        emit_error(worker, pane, "INVALID PATH", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }

    fmt_copy(worker->local_path[index], sizeof(worker->local_path[index]),
             canonical.text);
    emit_listing_begin(worker, pane, canonical.text);
    log_event(worker, LOGEV_LIST, NULL, NULL, canonical.text, "begin", "app",
              0, 0);

    int slot = handoff_listing_begin(worker->handoff, pane);
    if (slot < 0) {
        emit_listing_end(worker, pane, canonical.text, 0, 0, 0, 0,
                         "LISTING BUSY");
        return;
    }

    UiRow *rows = rows_for(worker, pane, slot);
    int count = 0;
    int truncated = 0;

    if (!path_is_volume_root(canonical.text)) {
        memset(&rows[count], 0, sizeof(rows[count]));
        snprintf(rows[count].name, sizeof(rows[count].name), "..");
        rows[count].type = UI_ROW_UP;
        count++;
    }

    FsDir *dir = NULL;
    FsResult result = worker->ports.fs->list_open(
        worker->ports.fs->ctx, canonical.text, &dir);
    if (result != FS_OK) {
        handoff_listing_publish(worker->handoff, pane, slot, rows, count);
        log_event(worker, LOGEV_LIST, NULL, NULL, canonical.text, "open",
                  "storage", count, 0);
        if (result == FS_NOT_FOUND) {
            /* The view's folder is gone: a removed mount or a deleted folder.
             * ALL DRIVES is the only honest view, and the reset is per view so
             * a hidden local view is fixed too. The failed listing gets no END:
             * both listings happen in one worker step, and an END for the dead
             * generation would clear the root rows the app applies from the
             * newer one. The reason is reported once, after the root listing,
             * so no LISTING FAILED stays on a dead path (R9, AE14). */
            worker_list_root(worker, pane);
            emit_error(worker, pane, WORKER_FOLDER_GONE_TEXT,
                       OP_CAUSE_STORAGE, OP_STAGE_COMMAND);
            return;
        }
        emit_listing_end(worker, pane, canonical.text, 0, 0, 0, 0,
                         fs_result_text(result));
        return;
    }

    FsResult next = FS_END;
    int stopped_at_cap = 0;
    FsEntry entry;
    while ((next = worker->ports.fs->list_next(worker->ports.fs->ctx, dir,
                                               &entry)) == FS_OK) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;
        /* A partial write is never a user file; a finished aside stays
         * visible so a kept copy is reachable (D81). */
        if (path_sidecar_kind(entry.name) == PATH_SIDECAR_PART)
            continue;
        if (count >= WORKER_LIST_CAP) {
            truncated = 1;
            stopped_at_cap = 1;
            break;
        }
        UiRow *row = &rows[count++];
        memset(row, 0, sizeof(*row));
        fmt_copy(row->name, sizeof(row->name), entry.name);
        row->type = entry.type == FS_ENTRY_DIR
                        ? UI_ROW_DIR
                        : (entry.type == FS_ENTRY_FILE ? UI_ROW_FILE
                                                       : UI_ROW_FILE);
        row->size = entry.size;
        row->size_known = entry.size_known;
        row->debris =
            (uint8_t)(path_sidecar_kind(entry.name) == PATH_SIDECAR_OLD);
        row->risk = (uint8_t)row_risk(canonical.text, entry.name);
    }
    worker->ports.fs->list_close(worker->ports.fs->ctx, dir);

    int start = count > 0 && rows[0].type == UI_ROW_UP ? 1 : 0;
    sort_rows(rows, count, start);

    handoff_listing_publish(worker->handoff, pane, slot, rows, count);

    int free_known = 0;
    uint64_t free_bytes = 0;
    Volume volume;
    if (path_volume(canonical.text, &volume)) {
        int idx = (int)volume;
        if (idx >= 0 && idx < VOLUME_COUNT) {
            int at_root = path_is_volume_root(canonical.text);
            if (at_root || !worker->volume_free_valid[idx]) {
                FsFreeSpace space;
                memset(&space, 0, sizeof(space));
                if (worker->ports.fs->free_space(
                        worker->ports.fs->ctx, canonical.text, &space) ==
                        FS_OK &&
                    space.known) {
                    worker->volume_free_bytes[idx] = space.bytes;
                    worker->volume_free_valid[idx] = 1;
                }
            }
            if (worker->volume_free_valid[idx]) {
                free_bytes = worker->volume_free_bytes[idx];
                free_known = 1;
            }
        }
    }

    /* A folder that stops at the cap is a complete, truncated listing, not a
     * read error. Only a real list_next failure is a failed listing. */
    int ok = stopped_at_cap || next == FS_END;
    log_event(worker, LOGEV_LIST, NULL, NULL, canonical.text,
              ok ? "end" : "read", ok ? "app" : "storage", count, 0);
    emit_listing_end(worker, pane, canonical.text, ok, truncated,
                     free_bytes, free_known,
                     ok ? "" : fs_result_text(next));
    if (pane == UI_PANE_LOCAL) {
        worker->settings_dirty = 1;
        fmt_copy(worker->settings.last_local,
                 sizeof(worker->settings.last_local), canonical.text);
    }
}

/* --- remote listing ---------------------------------------------------- */

static void remote_listing_begin(Worker *worker, UiPane pane,
                                 const char *path)
{
    log_event(worker, LOGEV_LIST, NULL, NULL, path, "begin", "app", 0, 0);
    /* A begin that cannot take a slot must not leave an earlier collection's
     * flag set: the preflight reads `collecting` right after starting its
     * refresh, and a stale flag would make it wait for a listing that never
     * publishes (KTD5). */
    worker->collecting = 0;
    int slot = handoff_listing_begin(worker->handoff, pane);
    if (slot < 0) {
        emit_listing_end(worker, pane, path, 0, 0, 0, 0, "LISTING BUSY");
        return;
    }
    worker->collecting = 1;
    worker->collect_pane = pane;
    worker->collect_slot = slot;
    worker->collect_count = 0;
    worker->remote_row_count = 0;
    worker->collect_truncated = 0;
    fmt_copy(worker->remote_path, sizeof(worker->remote_path), path != NULL ? path : "/");
    emit_listing_begin(worker, pane, worker->remote_path);
}

static void remote_listing_row(Worker *worker, const ListingEntry *entry)
{
    if (!worker->collecting || entry == NULL)
        return;
    if (worker->collect_count >= WORKER_LIST_CAP) {
        worker->collect_truncated = 1;
        return;
    }
    UiRow *rows = rows_for(worker, worker->collect_pane, worker->collect_slot);
    UiRow *row = &rows[worker->collect_count++];
    memset(row, 0, sizeof(*row));
    fmt_copy(row->name, sizeof(row->name), entry->name);
    row->type = entry->type == LISTING_DIR
                    ? UI_ROW_DIR
                    : (entry->type == LISTING_FILE ? UI_ROW_FILE : UI_ROW_FILE);
    row->size = entry->size;
    row->size_known = entry->size_known;
    row->debris =
        (uint8_t)(path_sidecar_kind(entry->name) == PATH_SIDECAR_OLD);
    row->unsafe = (uint8_t)!path_name_is_safe(entry->name);
}

static void remote_listing_end(Worker *worker, int ok)
{
    if (!worker->collecting)
        return;

    UiPane pane = worker->collect_pane;
    UiRow *rows = rows_for(worker, pane, worker->collect_slot);
    int count = worker->collect_count;
    int start = 0;
    /* The client already skips `.` and `..`; add the local UP row unless the
     * remote path is the root. */
    if (strcmp(worker->remote_path, "/") != 0) {
        if (count < WORKER_LIST_CAP) {
            memmove(&rows[1], &rows[0], (size_t)count * sizeof(UiRow));
            count++;
            memset(&rows[0], 0, sizeof(rows[0]));
            snprintf(rows[0].name, sizeof(rows[0].name), "..");
            rows[0].type = UI_ROW_UP;
            start = 1;
        } else {
            worker->collect_truncated = 1;
        }
    }

    sort_rows(rows, count, start);
    worker->remote_row_count = count;
    handoff_listing_publish(worker->handoff, pane,
                            worker->collect_slot, rows, count);
    log_event(worker, LOGEV_LIST, NULL, NULL, worker->remote_path,
              ok ? "end" : "read", ok ? "app" : "server",
              (uint64_t)count, 0);
    emit_listing_end(worker, pane, worker->remote_path, ok,
                     worker->collect_truncated, 0, 0,
                     ok ? "" : "REMOTE LISTING FAILED");
    worker->collecting = 0;
    worker->collect_count = 0;
}

/* SIZE cannot report a type. Only the rows published for the destination's
 * own parent can type it: rows left from another folder would type a name that
 * merely shares its last segment. Returns the row type, or -1 when no row for
 * the name belongs to the listed folder, so the caller can tell "a file" from
 * "the listing does not show it" (R7). */
static int remote_listing_type(Worker *worker, const char *destination)
{
    if (worker->remote_row_count <= 0)
        return -1;
    char parent[PATH_MAX_LEN];
    if (!remote_parent(destination, parent, sizeof(parent)) ||
        strcmp(worker->remote_path, parent) != 0)
        return -1;
    const char *name = last_name(destination);
    const UiRow *rows =
        rows_for(worker, worker->collect_pane, worker->collect_slot);
    for (int i = 0; i < worker->remote_row_count; ++i) {
        if (strcmp(rows[i].name, name) == 0)
            return rows[i].type;
    }
    return -1;
}

/* Release a reserved listing generation when the session dies mid-listing.
 * The UI already has the session-lost state; publishing the partial rows keeps
 * the arena from leaking a held slot for the rest of the session. */
static void remote_listing_abort(Worker *worker)
{
    if (!worker->collecting)
        return;
    UiPane pane = worker->collect_pane;
    UiRow *rows = rows_for(worker, pane, worker->collect_slot);
    handoff_listing_publish(worker->handoff, pane,
                            worker->collect_slot, rows, worker->collect_count);
    worker->collecting = 0;
    worker->collect_count = 0;
}

/* --- U2: private remote walk ------------------------------------------- */

static void walk_note(Walk *walk, const char *reason)
{
    fmt_copy(walk->refusal, sizeof(walk->refusal), reason);
}

/* The absolute path one folder entry is listed under. A root uses the caller's
 * own root string; a child composes from the plan's source. */
static int walk_dir_path(Worker *worker, int dir, char *out, size_t cap)
{
    Walk *walk = &worker->walk;
    const char *path = dir >= 0 && dir < walk->root_count
                           ? walk->roots[dir]
                           : xfer_plan_source(walk->plan, dir);
    if (path == NULL || path[0] != '/')
        return 0;
    return ftp_remote_normalize(NULL, path, out, cap);
}

/* One skipped entry stands for a whole subtree (R5). The folder's own entry
 * stays planned, so a download still creates the empty destination folder and
 * a move never deletes the skipped source. */
static void walk_add_skip(Worker *worker, int dir, XferSkipReason reason)
{
    Walk *walk = &worker->walk;
    const char *source = xfer_plan_source(walk->plan, dir);
    const char *dest = xfer_plan_dest(walk->plan, dir);
    (void)xfer_plan_add_skip(walk->plan, dir,
                             source[0] != '\0' ? source : NULL,
                             dest[0] != '\0' ? dest : NULL, XFER_ENTRY_DIR,
                             reason);
}

/* Stop an unfinished walk. The plan is deliberately left un-finalized: it is
 * the caller's to discard, and no partial plan may run (KTD2). A NULL reason
 * keeps the refusal the caller already recorded. */
static void walk_stop(Worker *worker, const char *reason)
{
    Walk *walk = &worker->walk;
    if (!walk->active)
        return;
    walk->active = 0;
    walk->done = 0;
    walk->request = 0;
    walk->retry_dir = XFER_PLAN_NO_INDEX;
    if (reason != NULL)
        walk_note(walk, reason);
}

int worker_walk_begin(Worker *worker, XferPlan *plan,
                      const char *const *roots, int root_count)
{
    if (worker == NULL || plan == NULL || roots == NULL || root_count <= 0)
        return 0;

    Walk *walk = &worker->walk;
    if (walk->active)
        return 0;

    walk->active = 0;
    walk->done = 0;
    walk->cancel = 0;
    walk->abort = 0;
    walk->request = 0;
    walk->plan = plan;
    walk->root_count = 0;
    walk->path[0] = '\0';
    walk->dir = XFER_PLAN_NO_INDEX;
    walk->retry_dir = XFER_PLAN_NO_INDEX;
    walk->folders = 0;
    walk->rows = 0;
    walk->overflow = 0;
    walk_note(walk, "");

    if (!plan->ok) {
        walk_note(walk, plan->reason);
        return 0;
    }
    if (plan->direction != XFER_PLAN_DOWNLOAD) {
        /* The upload walk also lists the local tree and each remote destination
         * folder; that half belongs to the job (U3), so it is refused rather
         * than half-built here. */
        walk_note(walk, "UPLOAD WALK NOT BUILT");
        return 0;
    }
    if (root_count > WORKER_WALK_ROOTS || root_count > plan->entry_count) {
        walk_note(walk, "TOO MANY ROOTS");
        return 0;
    }
    for (int i = 0; i < root_count; ++i) {
        if (roots[i] == NULL || roots[i][0] != '/' ||
            xfer_plan_parent(plan, i) != -1 ||
            !ftp_remote_normalize(NULL, roots[i], walk->roots[i],
                                  sizeof(walk->roots[i]))) {
            walk_note(walk, "INVALID ROOT");
            return 0;
        }
    }

    walk->root_count = root_count;
    walk->token++;
    if (walk->token == 0)
        walk->token = 1;
    walk->active = 1;
    return 1;
}

void worker_walk_cancel(Worker *worker)
{
    if (worker == NULL)
        return;
    Walk *walk = &worker->walk;
    if (!walk->active)
        return;
    /* Latch it: the pump owns the client call, so a cancel never re-enters the
     * client from inside a listing callback. The plan is left unfinished for
     * the caller to discard. */
    walk->cancel = 1;
    walk->done = 0;
}

int worker_walk_active(const Worker *worker)
{
    return worker != NULL && worker->walk.active;
}

int worker_walk_done(const Worker *worker)
{
    return worker != NULL && worker->walk.done;
}

XferPlan *worker_walk_plan(Worker *worker)
{
    return worker != NULL ? worker->walk.plan : NULL;
}

int worker_walk_folders(const Worker *worker)
{
    return worker != NULL ? worker->walk.folders : 0;
}

const char *worker_walk_refusal(const Worker *worker)
{
    return worker != NULL ? worker->walk.refusal : "";
}

/* The walk's own listing: the token was set before the request, so a user
 * listing of the same path (the walk's request slot is 0 then) never enters
 * the plan. */
static int walk_owns_listing(const Walk *walk, const FtpClientEvent *event)
{
    return walk->active && walk->request != 0 && event->path != NULL &&
           strcmp(event->path, walk->path) == 0;
}

static int walk_listing_begin(Worker *worker, const FtpClientEvent *event)
{
    Walk *walk = &worker->walk;
    if (!walk_owns_listing(walk, event))
        return 0;
    fmt_copy(walk->path, sizeof(walk->path), event->path);
    walk->rows = 0;
    walk->overflow = 0;
    return 1;
}

static int walk_listing_row(Worker *worker, const FtpClientEvent *event)
{
    Walk *walk = &worker->walk;
    if (!walk_owns_listing(walk, event) || event->row == NULL)
        return 0;
    if (walk->rows >= WORKER_LIST_CAP) {
        /* The client streams the whole listing. The first 2,000 rows transfer
         * and the rest become one missing entry (R5), never a silent loss. */
        walk->overflow++;
        return 1;
    }
    const ListingEntry *row = event->row;
    XferEntryType type =
        row->type == LISTING_DIR ? XFER_ENTRY_DIR : XFER_ENTRY_FILE;
    if (xfer_plan_add_child(walk->plan, walk->dir, row->name, type, row->size,
                            row->size_known) < 0) {
        /* The plan refused (too large, or a path that cannot be stored). Stop
         * at the next pump, outside the client's callback. */
        walk->abort = 1;
        return 1;
    }
    walk->rows++;
    return 1;
}

/* A listing can fail in two ways that look alike at LISTING_END. A refused or
 * failed command has already left the client READY, so one folder is lost and
 * the session survives. A terminal state - a timeout, a lost control
 * connection, or a closed session - means the walk cannot continue. A data
 * stream that dies is still LISTING at the event and becomes READY right after
 * the operation-done event, so only the terminal states end the walk. */
static int walk_session_closed(const Worker *worker)
{
    switch (ftp_client_state(worker->client)) {
    case FTP_CLIENT_IDLE:
    case FTP_CLIENT_LOST:
    case FTP_CLIENT_FAILED:
    case FTP_CLIENT_STALLED:
        return 1;
    default:
        return 0;
    }
}

static int walk_listing_end(Worker *worker, const FtpClientEvent *event)
{
    Walk *walk = &worker->walk;
    if (!walk_owns_listing(walk, event))
        return 0;

    walk->request = 0;
    if (event->ok) {
        if (walk->overflow > 0)
            walk_add_skip(worker, walk->dir, XFER_SKIP_LIST_CAP);
        walk->folders++;
        return 1;
    }

    walk_note(walk, event->detail != NULL ? event->detail : "LISTING FAILED");
    if (walk_session_closed(worker)) {
        walk_stop(worker, NULL);
        return 1;
    }
    walk_add_skip(worker, walk->dir, XFER_SKIP_LIST_REFUSED);
    return 1;
}

static void worker_walk_pump(Worker *worker)
{
    Walk *walk = &worker->walk;
    if (!walk->active)
        return;

    if (walk->cancel) {
        int listing = walk->request != 0;
        /* The request belongs to the cancelled walk, so its own END is ignored
         * instead of filling the next walk's plan. */
        walk->request = 0;
        walk->retry_dir = XFER_PLAN_NO_INDEX;
        walk->token++;
        walk_note(walk, "CANCELLED");
        walk_stop(worker, NULL);
        if (listing)
            (void)ftp_client_cancel(worker->client);
        return;
    }

    if (walk->abort) {
        walk->abort = 0;
        int listing = walk->request != 0;
        walk->request = 0;
        if (listing)
            (void)ftp_client_cancel(worker->client);
        walk_stop(worker, walk->plan->reason);
        return;
    }

    if (walk->request != 0)
        return;
    /* A connect in progress owns the slot and the walk simply waits for it: a
     * walk begun during a connect either starts when the session is READY or
     * stops when that connect fails. */
    if (ftp_client_busy(worker->client))
        return;
    if (ftp_client_state(worker->client) != FTP_CLIENT_READY) {
        walk_stop(worker, "SESSION LOST");
        return;
    }

    int guard = walk->plan->entry_count + 1;
    while (walk->active && walk->request == 0 && guard-- > 0) {
        /* The client slot is the only thing that can hold a folder back. The
         * check precedes the pop, so the slot never loses a worklist item. */
        if (ftp_client_busy(worker->client))
            return;
        if (ftp_client_state(worker->client) != FTP_CLIENT_READY) {
            walk_stop(worker, "SESSION LOST");
            return;
        }

        int dir;
        if (walk->retry_dir != XFER_PLAN_NO_INDEX) {
            dir = walk->retry_dir;
            walk->retry_dir = XFER_PLAN_NO_INDEX;
        } else {
            dir = xfer_plan_next_dir(walk->plan);
        }
        if (dir < 0) {
            walk->active = 0;
            walk->done = xfer_plan_finalize(walk->plan);
            if (!walk->done)
                walk_note(walk, walk->plan->reason);
            return;
        }

        char path[PATH_MAX_LEN];
        if (!walk_dir_path(worker, dir, path, sizeof(path))) {
            walk_add_skip(worker, dir, XFER_SKIP_LIST_REFUSED);
            continue;
        }

        walk->dir = dir;
        fmt_copy(walk->path, sizeof(walk->path), path);
        walk->rows = 0;
        walk->overflow = 0;
        /* The request token goes down before the call: ftp_client_list emits
         * LISTING_BEGIN from inside it. */
        walk->request = walk->token;
        FtpClientResult result = ftp_client_list(worker->client, walk->path);
        if (result == FTP_CLIENT_OK)
            return; /* LISTING_END advances the worklist */
        walk->request = 0;
        if (result == FTP_CLIENT_BUSY) {
            walk->retry_dir = dir;
            return;
        }
        if (result == FTP_CLIENT_INVALID) {
            walk_add_skip(worker, dir, XFER_SKIP_LIST_REFUSED);
            continue;
        }
        walk_stop(worker, "SESSION LOST");
        return;
    }
    /* Each pass either starts a listing or consumes one worklist entry, so the
     * bound is the plan's own size; reaching it means the worklist is lying. */
    if (walk->active && walk->request == 0)
        walk_stop(worker, "WALK LOOP");
}

static FtpClientResult remote_list(Worker *worker, UiPane pane)
{
    if (worker->client == NULL ||
        ftp_client_state(worker->client) != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    /* The walk's listing stream is private (KTD3): a pane refresh waits for
     * the walk to end instead of interleaving a user listing with it. */
    if (worker_walk_active(worker))
        return FTP_CLIENT_BUSY;
    worker->listing_pane = pane;
    return ftp_client_list(worker->client,
                           ftp_client_cwd_text(worker->client));
}

/* --- remote write capability (R18) ------------------------------------- */

/* The UI learns the current folder's write facts from this event, never from
 * a guess: an unknown folder stays enabled. */
static void remote_perm_emit(Worker *worker)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_REMOTE_PERM);
    event.pane = ui_pane_remote_view();
    event.perm_known = worker->remote_perm_known;
    event.perm_mask = worker->remote_perm_mask;
    emit(worker, &event);
}

/* A new folder or a new session starts unknown, so the old folder's mark can
 * never dim the new one. */
static void remote_perm_clear(Worker *worker, int announce)
{
    worker->remote_perm_known = 0;
    worker->remote_perm_mask = 0;
    if (announce)
        remote_perm_emit(worker);
}

static void remote_perm_set(Worker *worker, uint8_t mask)
{
    worker->remote_perm_known = 1;
    worker->remote_perm_mask = (int)(mask & LISTING_PERM_WRITES);
    remote_perm_emit(worker);
}

/* MLST the folder the user is in. A 550, an unsupported MLST, or a server
 * that reports no perm fact leaves the fact as it is (R18). */
static void remote_perm_pump(Worker *worker)
{
    if (!worker->remote_perm_pending || !worker->connected)
        return;
    if (worker->transfer_active || xfer_busy(&worker->queue) ||
        ftp_server_busy(worker->server) || ftp_client_busy(worker->client))
        return;

    worker->remote_perm_pending = 0;
    FtpClientResult result =
        ftp_client_mlst(worker->client, ftp_client_cwd_text(worker->client));
    if (result == FTP_CLIENT_OK)
        worker->remote_perm_active = 1;
    else if (result == FTP_CLIENT_BUSY)
        worker->remote_perm_pending = 1;
}

/* --- site book and settings -------------------------------------------- */

static void fill_snapshot(const Worker *worker, UiSiteSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->count = (int)site_book_count(&worker->book);
    if (snapshot->count > UI_SITES_MAX)
        snapshot->count = UI_SITES_MAX;
    snapshot->save_unavailable = worker->save_unavailable;
    for (int i = 0; i < snapshot->count; ++i) {
        const Site *site = site_book_at(&worker->book, (unsigned)i);
        UiSiteRow *row = &snapshot->rows[i];
        fmt_copy(row->name, sizeof(row->name), site->name);
        fmt_copy(row->host, sizeof(row->host), site->host);
        row->port = site->port;
        row->anonymous = site->anonymous;
        row->password_set = site->password[0] != '\0';
        fmt_copy(row->username, sizeof(row->username), site->username);
    }
}

static void publish_site_book(Worker *worker)
{
    if (!worker->site_dirty)
        return;
    UiSiteSnapshot snapshot;
    fill_snapshot(worker, &snapshot);
    if (handoff_bulk_write(worker->handoff, &snapshot, sizeof(snapshot),
                           ++worker->site_generation)) {
        worker->site_dirty = 0;
        HandoffEvent event;
        event_init(&event, HANDOFF_EVT_SITE_BOOK);
        emit(worker, &event);
    }
}

static int save_sites(Worker *worker)
{
    StoreResult result = store_save_sites(worker->ports.fs,
                                          worker->store_dir, &worker->book);
    worker->save_unavailable = result != STORE_OK;

    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_SITE_BOOK);
    event.ok = result == STORE_OK;
    fmt_copy(event.detail, sizeof(event.detail), result == STORE_OK ? "SAVED" : "SAVE UNAVAILABLE");
    emit(worker, &event);
    worker->site_dirty = 1;
    log_event(worker, LOGEV_SAVE, NULL, NULL, result == STORE_OK ? "sites" : NULL,
              result == STORE_OK ? "done" : "failed",
              result == STORE_OK ? "app" : "storage", 0, 0);
    return result == STORE_OK;
}

static void save_settings(Worker *worker)
{
    if (!worker->settings_dirty)
        return;
    StoreResult result = store_save_settings(worker->ports.fs,
                                             worker->store_dir,
                                             &worker->settings);
    if (result == STORE_OK) {
        worker->settings_dirty = 0;
    } else {
        log_detail(worker, LOGEV_SETTINGS, "save failed");
    }
}

/* --- confirmations ----------------------------------------------------- */

static void request_local_conflict(Worker *worker, const UiCommand *command,
                                   const FsOpPlan *plan)
{
    worker->prompt_count = 0;
    int named = plan->existing.named;
    if (named > WORKER_CONFIRM_ITEMS)
        named = WORKER_CONFIRM_ITEMS;
    for (int i = 0; i < named; ++i)
        prompt_item_add(worker, fsop_plan_existing_dest(plan, i), 0, 0, 0);

    /* The count is the number of names the prompt can show, so a folder full
     * of collisions never claims more than its 16-item view (R19, KTD6). */
    request_prompt(worker, UI_CONFIRM_CONFLICT, local_view(command),
                   ui_action_context(command->action), named, named);

    worker->pending.valid = 1;
    worker->pending.kind = PENDING_LOCAL_CONFLICT;
    worker->pending.command = *command;
    worker->pending.conflict = FSOP_CONFLICT_ASK;
}

static void request_transfer_conflict(Worker *worker, const XferItem *item)
{
    HandoffConfirm request;
    memset(&request, 0, sizeof(request));
    request.kind = UI_CONFIRM_CONFLICT;
    request.valid = 1;
    snprintf(request.context, sizeof(request.context), "TRANSFER");
    request.item_count = 1;
    request.existing_count = 1;
    fmt_copy(request.items[0].name, sizeof(request.items[0].name), last_name(item->destination.text));
    request.items[0].size = item->total;
    request.items[0].size_known = item->total_known;
    confirm_request(worker, &request);

    worker->pending.valid = 1;
    worker->pending.kind = PENDING_TRANSFER;
}

/* --- local action execution -------------------------------------------- */

static void facts_reset(Worker *worker)
{
    worker->fact_count = 0;
    worker->facts_truncated = 0;
}

static int fact_add(Worker *worker, const char *path, int exists,
                    FsEntryType type, uint64_t size, int size_known)
{
    if (worker->fact_count >= WORKER_FACTS_MAX) {
        worker->facts_truncated = 1;
        return 0;
    }
    FsOpFact *fact = &worker->facts[worker->fact_count];
    char *storage = worker->fact_paths[worker->fact_count];
    snprintf(storage, PATH_MAX_LEN, "%s", path);
    fact->path = storage;
    fact->exists = exists;
    fact->type = type;
    fact->size = size;
    fact->size_known = size_known;
    worker->fact_count++;
    return 1;
}

static void fact_stat(Worker *worker, const char *path)
{
    FsEntry entry;
    memset(&entry, 0, sizeof(entry));
    FsResult result =
        worker->ports.fs->stat(worker->ports.fs->ctx, path, &entry);
    fact_add(worker, path, result == FS_OK,
             result == FS_OK ? entry.type : FS_ENTRY_FILE,
             result == FS_OK ? entry.size : 0,
             result == FS_OK ? entry.size_known : 0);
}

/* The planner compares canonical paths, so a one-off fact must be stored under
 * the same form the plan will build. */
static void fact_stat_canonical(Worker *worker, const char *path)
{
    if (path == NULL)
        return;
    Path canon;
    if (path_canonicalize(path, &canon) == PATH_OK)
        fact_stat(worker, canon.text);
    else
        fact_stat(worker, path);
}

static void fact_tree(Worker *worker, const char *path, int depth)
{
    if (depth > WORKER_REMOTE_DEPTH || worker->fact_count >= WORKER_FACTS_MAX) {
        worker->facts_truncated = 1;
        return;
    }

    FsEntry entry;
    if (worker->ports.fs->stat(worker->ports.fs->ctx, path, &entry) != FS_OK) {
        fact_add(worker, path, 0, FS_ENTRY_FILE, 0, 0);
        return;
    }
    fact_add(worker, path, 1, entry.type, entry.size, entry.size_known);
    if (entry.type != FS_ENTRY_DIR)
        return;

    FsDir *dir = NULL;
    if (worker->ports.fs->list_open(worker->ports.fs->ctx, path, &dir) !=
        FS_OK)
        return;

    FsEntry child;
    while (worker->ports.fs->list_next(worker->ports.fs->ctx, dir, &child) ==
           FS_OK) {
        if (strcmp(child.name, ".") == 0 || strcmp(child.name, "..") == 0)
            continue;
        Path full;
        if (path_join(path, child.name, &full) != PATH_OK)
            continue;
        fact_tree(worker, full.text, depth + 1);
        if (worker->fact_count >= WORKER_FACTS_MAX) {
            worker->facts_truncated = 1;
            break;
        }
    }
    worker->ports.fs->list_close(worker->ports.fs->ctx, dir);
}

static int build_dest_facts(Worker *worker, const char *source,
                            const char *destination, int source_is_dir)
{
    Path dest_root;
    char name[PATH_NAME_MAX];
    fmt_copy(name, sizeof(name), last_name(source));
    if (path_join(destination, name, &dest_root) != PATH_OK)
        return 0;
    fact_stat(worker, dest_root.text);

    if (!source_is_dir)
        return 1;

    size_t source_len = strlen(source);
    int start = worker->fact_count;
    for (int i = 0; i < start; ++i) {
        const char *path = worker->facts[i].path;
        if (strncmp(path, source, source_len) != 0 ||
            (path[source_len] != '\0' && path[source_len] != '/'))
            continue;
        const char *relative = path + source_len;
        while (*relative == '/')
            relative++;
        if (*relative == '\0')
            continue;
        Path target;
        if (path_join(dest_root.text, relative, &target) != PATH_OK)
            return 0;
        fact_stat(worker, target.text);
    }
    return 1;
}

static void release_local_reservations(Worker *worker)
{
    reserve_release(worker->reserve, RESERVE_LANE_LOCAL);
}

static ReserveKind step_source_kind(FsOpStepKind kind)
{
    switch (kind) {
    case FSOP_STEP_RENAME:
    case FSOP_STEP_DELETE_FILE:
    case FSOP_STEP_DELETE_DIR:
        return RESERVE_TREE;
    default:
        return RESERVE_READ;
    }
}

static ReserveKind step_dest_kind(FsOpStepKind kind)
{
    return kind == FSOP_STEP_RENAME ? RESERVE_TREE : RESERVE_WRITE;
}

/* Reserve every path the plan will touch against the other lanes. 1 when the
 * plan holds the paths, 0 when a blocker was found, -1 when the table filled. */
static int reserve_local_plan(Worker *worker, const FsOpPlan *plan,
                              char *blocker, size_t cap)
{
    release_local_reservations(worker);

    for (int i = 0; i < plan->step_count; ++i) {
        const FsOpStep *step = &plan->steps[i];
        FsOpStepKind kind = (FsOpStepKind)step->kind;
        if (kind != FSOP_STEP_MKDIR) {
            const char *src = fsop_plan_step_source(plan, i);
            if (src[0] != '\0' &&
                reserve_conflict(worker->reserve, step_source_kind(kind), src,
                                 blocker, cap))
                return 0;
        }
        if (kind != FSOP_STEP_DELETE_FILE && kind != FSOP_STEP_DELETE_DIR) {
            const char *dst = fsop_plan_step_dest(plan, i);
            if (dst[0] != '\0' &&
                reserve_conflict(worker->reserve, step_dest_kind(kind), dst,
                                 blocker, cap))
                return 0;
        }
    }

    for (int i = 0; i < plan->step_count; ++i) {
        const FsOpStep *step = &plan->steps[i];
        FsOpStepKind kind = (FsOpStepKind)step->kind;
        if (kind != FSOP_STEP_MKDIR) {
            const char *src = fsop_plan_step_source(plan, i);
            if (src[0] != '\0' &&
                !reserve_add(worker->reserve, RESERVE_LANE_LOCAL,
                             step_source_kind(kind), src)) {
                release_local_reservations(worker);
                return -1;
            }
        }
        if (kind != FSOP_STEP_DELETE_FILE && kind != FSOP_STEP_DELETE_DIR) {
            const char *dst = fsop_plan_step_dest(plan, i);
            if (dst[0] != '\0' &&
                !reserve_add(worker->reserve, RESERVE_LANE_LOCAL,
                             step_dest_kind(kind), dst)) {
                release_local_reservations(worker);
                return -1;
            }
        }
    }
    return 1;
}

static void request_path_busy(Worker *worker, const UiCommand *command,
                              FsOpPlan *plan, const char *blocker)
{
    HandoffConfirm request;
    memset(&request, 0, sizeof(request));
    request.kind = UI_CONFIRM_PATH_BUSY;
    request.valid = 1;
    fmt_copy(request.context, sizeof(request.context), "PATH IN USE");
    request.item_count = 1;
    fmt_copy(request.items[0].name, sizeof(request.items[0].name), blocker);
    request.pane = local_view(command);
    confirm_request(worker, &request);
    worker->busy_command = *command;
    worker->busy_plan = plan;
    fmt_copy(worker->busy_blocker, sizeof(worker->busy_blocker), blocker);
    worker->busy_waiting = 0;
    worker->pending.valid = 1;
    worker->pending.kind = PENDING_BUSY;
}

/* The network lane reuses the PATH BUSY prompt. The item stays claimed and
 * head-of-line; WAIT retries, SKIP drops it, CANCEL clears the queue. */
static void request_network_path_busy(Worker *worker, const XferItem *item,
                                      const char *blocker)
{
    HandoffConfirm request;
    memset(&request, 0, sizeof(request));
    request.kind = UI_CONFIRM_PATH_BUSY;
    request.pane = UI_PANE_REMOTE;
    request.valid = 1;
    fmt_copy(request.context, sizeof(request.context), "PATH IN USE");
    request.item_count = 1;
    fmt_copy(request.items[0].name, sizeof(request.items[0].name), blocker);
    confirm_request(worker, &request);

    worker->network_waiting = 0;
    worker->pending.valid = 1;
    worker->pending.kind = PENDING_REMOTE_WAIT;
    worker->pending.item = *item;
}

static void emit_local_progress(Worker *worker)
{
    const FsOpProgress *progress = fsop_run_progress(worker->local_run);
    if (progress == NULL || progress->bytes_done == worker->local_run_last_bytes)
        return;
    worker->local_run_last_bytes = progress->bytes_done;

    uint64_t elapsed = now_ms(worker) - worker->local_run_started_ms;
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_PROGRESS);
    event.direction = action_direction(worker->local_run_command.action);
    event.bytes = progress->bytes_done;
    event.total = progress->bytes_total;
    event.total_known = progress->total_known;
    event.item_index = worker->local_run_command.batch_index + 1;
    event.item_total = worker->local_run_command.batch_total > 0
                           ? worker->local_run_command.batch_total
                           : 1;
    event.elapsed_ms = elapsed;
    event.elapsed_known = elapsed > 0;
    if (elapsed > 0) {
        event.rate = progress->bytes_done * 1000u / elapsed;
        event.rate_known = 1;
    }
    emit(worker, &event);
}

/* Did the run leave a changed path behind? A success did. A delete that
 * stopped part way did, because deletes are never rolled back. A copied or
 * moved item that rolled back cleanly did not; one whose rollback failed did
 * and reports the rollback stage (KTD7). */
static int run_changed_paths(FsOpRunResult result, FsOpKind kind,
                             const FsOpProgress *progress)
{
    if (result == FSOP_RUN_OK)
        return 1;
    if (progress->stage == OP_STAGE_ROLLBACK)
        return 1;
    return kind == FSOP_DELETE && progress->files_done > 0;
}

/* KTD7: name the exact parent folders this item changed. A copy and a move
 * change the destination folder; a mkdir and a rename change the parent of
 * their target; a delete changes the removed path's parent. Ancestors (and
 * descendants) are not part of the set. */
static void record_local_changes(Worker *worker, const UiCommand *command,
                                 FsOpRunResult result,
                                 const FsOpProgress *progress)
{
    if (command == NULL || progress == NULL)
        return;

    FsOpKind kind;
    switch (command->action) {
    case UI_ACTION_COPY:
        kind = FSOP_COPY;
        break;
    case UI_ACTION_MOVE:
        kind = FSOP_MOVE;
        break;
    case UI_ACTION_DELETE:
        kind = FSOP_DELETE;
        break;
    case UI_ACTION_NEW_FOLDER:
        kind = FSOP_MKDIR;
        break;
    case UI_ACTION_RENAME:
        kind = FSOP_RENAME;
        break;
    default:
        return;
    }
    if (!run_changed_paths(result, kind, progress))
        return;

    switch (kind) {
    case FSOP_COPY:
        changed_dir_add(worker, command->path2);
        break;
    case FSOP_MOVE:
        changed_dir_add_parent(worker, command->path);
        changed_dir_add(worker, command->path2);
        break;
    case FSOP_DELETE:
        changed_dir_add_parent(worker, command->path);
        break;
    case FSOP_MKDIR:
        changed_dir_add_parent(worker, command->path2);
        break;
    case FSOP_RENAME:
        changed_dir_add_parent(worker, command->path);
        changed_dir_add_parent(worker, command->path2);
        break;
    }
}

/* Emit the terminal exactly once and release the run, the plan, and the
 * command. Mirrors the blocking tail the old run_plan had. */
static void finish_local_run(Worker *worker, FsOpRunResult result)
{
    const FsOpProgress *live = fsop_run_progress(worker->local_run);
    FsOpProgress progress = *live;
    UiCommand command = worker->local_run_command;
    FsOpPlan *plan = worker->local_plan;
    XferDirection direction = action_direction(command.action);
    LocalBatch *batch = &worker->local_batch;
    UiPane issuing = local_view(&command);
    int item_index = batch->active ? batch->index + 1 : 1;
    int item_total = batch->active ? batch->total : 1;

    if (progress.sync_skipped)
        log_detail(worker, LOGEV_LOG, "sync skipped");
    record_local_changes(worker, &command, result, &progress);
    local_batch_record(worker, result, plan, &progress);

    if (result == FSOP_RUN_OK) {
        log_event(worker, LOGEV_TRANSFER_DONE, NULL, NULL, command.path,
                  "done", "app", progress.bytes_done, progress.bytes_total);
    } else {
        emit_error(worker, issuing, progress.message, progress.cause,
                   progress.stage);
        log_event(worker, LOGEV_TRANSFER_FAIL, NULL, NULL, command.path,
                  op_stage_name(progress.stage), op_cause_name(progress.cause),
                  progress.bytes_done, progress.bytes_total);
    }

    HandoffEvent done;
    event_init(&done, HANDOFF_EVT_TRANSFER_DONE);
    done.ok = result == FSOP_RUN_OK;
    done.bytes = progress.bytes_done;
    done.total = progress.bytes_total;
    done.total_known = progress.total_known;
    done.cause = progress.cause;
    done.stage = progress.stage;
    done.item_index = item_index;
    done.item_total = item_total;
    done.direction = direction;
    fmt_copy(done.path, sizeof(done.path), command.path);
    if (progress.notice[0] != '\0') {
        log_detail(worker, LOGEV_LOG, progress.notice);
        fmt_copy(done.detail, sizeof(done.detail), progress.notice);
        done.notice = 1;
    }
    emit(worker, &done);

    if (worker->local_batch_cancel_requested) {
        worker->local_batch_cancel_requested = 0;
        local_batch_cancel(worker);
    } else if (command.batch_last || !batch->active ||
               batch->index + 1 >= batch->total) {
        local_batch_complete(worker);
    }

    release_local_reservations(worker);
    fsop_run_destroy(worker->local_run);
    worker->local_run = NULL;
    worker->local_plan = NULL;
    worker->local_cancel_requested = 0;
    worker->local_cancel_seen = 0;
    free(plan);
}

/* Advance the local job for this worker step, then let the client, server, and
 * queue tick again on the next frame. */
static void local_run_pump(Worker *worker)
{
    if (worker->local_run == NULL)
        return;

    uint64_t start = now_ms(worker);
    FsOpRunResult result = FSOP_RUN_FAILED;
    for (int i = 0; i < WORKER_LOCAL_STEPS_MAX; ++i) {
        result = fsop_run_step(worker->local_run, worker->ports.fs,
                               WORKER_LOCAL_BYTES_PER_STEP,
                               local_cancel_requested, worker);
        if (fsop_run_finished(worker->local_run))
            break;
        if (now_ms(worker) - start >= WORKER_LOCAL_SLICE_MS)
            break;
    }

    if (fsop_run_finished(worker->local_run)) {
        finish_local_run(worker, result);
        return;
    }
    emit_local_progress(worker);
}

/* A WAIT item is head-of-line: retry its reservation every worker step. */
static void busy_retry(Worker *worker)
{
    if (worker->busy_plan == NULL || !worker->busy_waiting)
        return;
    if (worker->pending.valid || worker->local_run != NULL)
        return;

    char blocker[PATH_MAX_LEN];
    int held =
        reserve_local_plan(worker, worker->busy_plan, blocker, sizeof(blocker));
    if (held == 0)
        return;
    if (held < 0) {
        emit_error(worker, local_view(&worker->busy_command),
                   "TOO MANY FILES TO TRACK", OP_CAUSE_APP, OP_STAGE_PLAN);
        free(worker->busy_plan);
        worker->busy_plan = NULL;
        worker->busy_waiting = 0;
        return;
    }

    /* D73: the wait may have changed the destination. Rebuild the plan from
     * the command so a file that appeared during the wait asks again instead
     * of being overwritten silently. */
    FsOpPlan *plan = worker->busy_plan;
    UiCommand command = worker->busy_command;
    worker->busy_plan = NULL;
    worker->busy_waiting = 0;
    free(plan);
    release_local_reservations(worker);
    execute_action(worker, &command, FSOP_CONFLICT_ASK);
}

/* Start a local plan as a resumable run. On success the run or the waiting
 * prompt owns `plan`; on 0 the caller frees it. */
static int start_local_plan(Worker *worker, const UiCommand *command,
                            FsOpPlan *plan)
{
    char blocker[PATH_MAX_LEN];
    int held = reserve_local_plan(worker, plan, blocker, sizeof(blocker));
    if (held < 0) {
        emit_error(worker, local_view(command), "TOO MANY FILES TO TRACK",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return 0;
    }
    if (held == 0) {
        request_path_busy(worker, command, plan, blocker);
        return 1;
    }
    if (!run_plan_begin(worker, command, plan)) {
        release_local_reservations(worker);
        return 0;
    }
    return 1;
}

static int run_plan_begin(Worker *worker, const UiCommand *command,
                          FsOpPlan *plan)
{
    if (worker->local_run != NULL)
        return 0; /* the lane is already running; never replace it */

    FsOpRun *run = fsop_run_create(plan);
    if (run == NULL) {
        emit_error(worker, local_view(command), "OUT OF MEMORY",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return 0;
    }

    worker->local_run = run;
    worker->local_plan = plan;
    worker->local_run_command = *command;
    worker->local_run_started_ms = now_ms(worker);
    worker->local_run_last_bytes = 0;
    worker->local_cancel_requested = 0;
    worker->local_cancel_seen = 0;
    worker->local_batch_cancel_requested = 0;

    LocalBatch *batch = &worker->local_batch;
    int item_index = batch->active ? batch->index + 1 : 1;
    int item_total = batch->active ? batch->total : 1;

    HandoffEvent started;
    event_init(&started, HANDOFF_EVT_TRANSFER_STARTED);
    fmt_copy(started.detail, sizeof(started.detail), last_name(command->path));
    started.item_index = item_index;
    started.item_total = item_total;
    started.total = plan->total_bytes;
    started.total_known = plan->total_known;
    fmt_copy(started.path, sizeof(started.path), command->path);
    started.op[0] = '\0';
    started.direction = action_direction(command->action);
    emit(worker, &started);
    return 1;
}

/* True when the source already lives in `folder`. The copy-name candidates are
 * statted only for this case: the planner must see an existing `A copy.TXT`
 * before it can choose `A copy 2.TXT` (R17, KTD6). */
static int source_in_folder(const char *source, const char *folder)
{
    Path parent;
    if (path_parent(source, &parent) != PATH_OK)
        return 0;
    return path_ci_equal(parent.text, folder);
}

static void add_copy_candidates(Worker *worker, const char *source,
                                const char *destination, int source_is_dir)
{
    for (int n = 0; n < 100; ++n) {
        char candidate[PATH_NAME_MAX + 16];
        if (!fsop_copy_name(source, source_is_dir, n, candidate,
                            sizeof(candidate)))
            return;

        Path target;
        if (path_join(destination, candidate, &target) != PATH_OK)
            return;
        fact_stat(worker, target.text);
        if (worker->facts_truncated)
            return;
    }
}

static void drain_pending_actions(Worker *worker)
{
    while (!worker->pending.valid && worker->pending_command_count > 0) {
        UiCommand command = worker->pending_commands[0];
        memmove(&worker->pending_commands[0], &worker->pending_commands[1],
                (size_t)(worker->pending_command_count - 1) *
                    sizeof(UiCommand));
        worker->pending_command_count--;
        handle_action(worker, &command);
    }
}

static void queue_pending_action(Worker *worker, const UiCommand *command)
{
    if (worker->pending_command_count >=
        (int)(sizeof(worker->pending_commands) /
              sizeof(worker->pending_commands[0]))) {
        emit_error(worker, local_view(command), "ACTION QUEUE FULL",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return;
    }
    worker->pending_commands[worker->pending_command_count++] = *command;
}

static void execute_action(Worker *worker, const UiCommand *command,
                           FsOpConflict conflict)
{
    const char *source = command->path;
    const char *destination = command->path2;
    UiPane issuing = local_view(command);

    FsOpKind kind;
    switch (command->action) {
    case UI_ACTION_COPY:
        kind = FSOP_COPY;
        break;
    case UI_ACTION_MOVE:
        kind = FSOP_MOVE;
        break;
    case UI_ACTION_DELETE:
        kind = FSOP_DELETE;
        break;
    case UI_ACTION_NEW_FOLDER:
        kind = FSOP_MKDIR;
        break;
    case UI_ACTION_RENAME:
        kind = FSOP_RENAME;
        break;
    default:
        emit_error(worker, issuing, "ACTION NOT SUPPORTED", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }

    facts_reset(worker);
    if (kind == FSOP_MKDIR) {
        /* plan_mkdir reads the destination fact and nothing else. Scanning the
         * current folder's whole tree here was both wasted work and, under the
         * b40 frame, a stack-overflow trigger on ur0:data. */
        fact_stat_canonical(worker, destination);
    } else if (kind == FSOP_RENAME) {
        /* plan_rename reads the source and destination facts; a folder source
         * does not need its children. */
        fact_stat_canonical(worker, source);
        fact_stat_canonical(worker, destination);
    } else {
        fact_tree(worker, source, 0);
    }
    if (worker->fact_count == 0 || worker->facts_truncated) {
        emit_error(worker, issuing,
                   worker->facts_truncated ? "PLAN TOO LARGE"
                                           : "SOURCE NOT FOUND",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return;
    }

    FsEntry source_entry;
    memset(&source_entry, 0, sizeof(source_entry));
    int source_is_dir = 0;
    if (worker->ports.fs->stat(worker->ports.fs->ctx, source,
                               &source_entry) == FS_OK)
        source_is_dir = source_entry.type == FS_ENTRY_DIR;

    if ((kind == FSOP_COPY || kind == FSOP_MOVE) && destination != NULL &&
        destination[0] != '\0') {
        if (!build_dest_facts(worker, source, destination, source_is_dir)) {
            emit_error(worker, issuing, "PLAN TOO LARGE", OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
        if (kind == FSOP_COPY &&
            source_in_folder(source, destination))
            add_copy_candidates(worker, source, destination, source_is_dir);
        if (worker->facts_truncated) {
            emit_error(worker, issuing, "PLAN TOO LARGE", OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
    }

    const char *sources[1] = {source};
    FsOpFacts facts;
    facts.items = worker->facts;
    facts.count = worker->fact_count;

    FsOpRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.sources = sources;
    request.source_count = 1;
    request.destination = destination;
    request.facts = &facts;
    request.conflict = conflict;

    FsOpPlan *plan = calloc(1, sizeof(FsOpPlan));
    if (plan == NULL) {
        emit_error(worker, issuing, "OUT OF MEMORY", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }

    int built = fsop_plan_build(plan, &request);
    if (!built || !plan->ok) {
        emit_error(worker, issuing,
                   plan->reason[0] != '\0' ? plan->reason : "PLAN REFUSED",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        free(plan);
        return;
    }

    if (plan->needs_conflict) {
        request_local_conflict(worker, command, plan);
        free(plan);
        return;
    }

    if (plan->needs_duplicate) {
        /* The same-folder copy: no step exists until SAVE DUPLICATES answers
         * (R16, KTD6). The item list names what the prompt can show. */
        worker->prompt_count = 0;
        int named = plan->duplicate.named;
        if (named > WORKER_CONFIRM_ITEMS)
            named = WORKER_CONFIRM_ITEMS;
        for (int i = 0; i < named; ++i)
            prompt_item_add(worker, fsop_plan_duplicate_source(plan, i),
                            source_entry.size, source_entry.size_known, 0);
        request_prompt(worker, UI_CONFIRM_DUPLICATE, issuing, destination,
                       named, 0);
        worker->pending.valid = 1;
        worker->pending.kind = PENDING_LOCAL_CONFLICT;
        worker->pending.command = *command;
        worker->pending.conflict = FSOP_CONFLICT_ASK;
        free(plan);
        return;
    }

    if (!start_local_plan(worker, command, plan))
        free(plan);
}

static FtpClientResult remote_action_begin(Worker *worker,
                                           const UiCommand *command)
{
    char target[PATH_MAX_LEN];

    if (command->action == UI_ACTION_NEW_FOLDER ||
        command->action == UI_ACTION_RENAME) {
        if (command->text[0] == '\0' || strchr(command->text, '/') != NULL ||
            strchr(command->text, '\\') != NULL)
            return FTP_CLIENT_INVALID;
    }

    switch (command->action) {
    case UI_ACTION_NEW_FOLDER:
        if (!remote_join(command->path2, command->text, target,
                         sizeof(target)))
            return FTP_CLIENT_INVALID;
        return ftp_client_mkdir(worker->client, target);

    case UI_ACTION_RENAME: {
        char parent[PATH_MAX_LEN];
        if (!remote_parent(command->path, parent, sizeof(parent)) ||
            !remote_join(parent, command->text, target, sizeof(target)))
            return FTP_CLIENT_INVALID;
        return ftp_client_rename(worker->client, command->path, target);
    }

    case UI_ACTION_DELETE:
        if (command->source_type == UI_ROW_DIR)
            return ftp_client_rmdir(worker->client, command->path);
        return ftp_client_delete(worker->client, command->path);

    default:
        return FTP_CLIENT_INVALID;
    }
}

static void pump_remote_actions(Worker *worker)
{
    while (worker->remote_action_count > 0) {
        if (!worker->connected) {
            worker->remote_action_count = 0;
            return;
        }
        if (ftp_client_busy(worker->client))
            return;

        UiCommand command = worker->remote_actions[0];
        memmove(&worker->remote_actions[0], &worker->remote_actions[1],
                (size_t)(worker->remote_action_count - 1) *
                    sizeof(UiCommand));
        worker->remote_action_count--;

        FtpClientResult result = remote_action_begin(worker, &command);
        if (result == FTP_CLIENT_OK)
            return; /* one FTP command at a time; OP_DONE pumps the next */
        if (result == FTP_CLIENT_BUSY) {
            memmove(&worker->remote_actions[1], &worker->remote_actions[0],
                    (size_t)worker->remote_action_count * sizeof(UiCommand));
            worker->remote_actions[0] = command;
            worker->remote_action_count++;
            return;
        }
        emit_error(worker, UI_PANE_REMOTE,
                   result == FTP_CLIENT_INVALID ? "INVALID NAME"
                                                : "NOT CONNECTED",
                   OP_CAUSE_APP, OP_STAGE_COMMAND);
    }
}

static void queue_remote_action(Worker *worker, const UiCommand *command)
{
    if (worker->remote_action_count >=
        (int)(sizeof(worker->remote_actions) /
              sizeof(worker->remote_actions[0]))) {
        emit_error(worker, UI_PANE_REMOTE, "ACTION QUEUE FULL", OP_CAUSE_APP,
                   OP_STAGE_COMMAND);
        return;
    }
    worker->remote_actions[worker->remote_action_count++] = *command;
    pump_remote_actions(worker);
}

static void batch_reset(Worker *worker)
{
    worker->batch_total = 0;
    worker->batch_done = 0;
    worker->batch_conflict_decided = 0;
    worker->batch_conflict = XFER_CONFLICT_NONE;
    /* A probe belongs to one batch; a result that arrives after its batch
     * ended must not decide the next one (KTD3). */
    upload_preflight_clear(worker);
}

/*
 * Every path that ends a remote session drops the queued batch with it. A
 * batch cannot resume across a new login, and keeping an OVERWRITE ALL answer
 * would silently replace files on the next site (D66).
 */
static void reset_remote_session(Worker *worker)
{
    worker->connected = 0;
    worker->refresh_remote = 0;
    worker->remote_action_count = 0;
    /* A walk cannot survive the session it was reading (R3): it stops with the
     * plan unfinished, and the caller discards it. */
    walk_stop(worker, "SESSION LOST");
    worker->remote_perm_known = 0;
    worker->remote_perm_mask = 0;
    worker->remote_perm_pending = 0;
    worker->remote_perm_active = 0;
    /* A new session starts unknown, so neither the dead session's mark nor a
     * reconnect to a same-looking root can dim the new folder (R18). */
    remote_perm_emit(worker);
    worker->upload_stat_active = 0;
    worker->upload_stat_ready = 0;
    worker->upload_stat_failed = 0;
    worker->upload_stat_used_size = 0;
    worker->upload_stat_disabled = 0;
    worker->remote_row_count = 0;
    remote_listing_abort(worker);
    xfer_init(&worker->queue);
    batch_reset(worker);
}

static void handle_disconnect(Worker *worker)
{
    if (worker->transfer_active) {
        emit_error(worker, UI_PANE_REMOTE, "TRANSFER IN PROGRESS",
                   OP_CAUSE_APP, OP_STAGE_CONNECT);
        return;
    }

    (void)ftp_client_disconnect(worker->client);
    reset_remote_session(worker);
    log_event(worker, LOGEV_CONNECT,
              worker->connecting_valid ? worker->connecting_site.name : NULL,
              worker->connecting_valid ? worker->connecting_site.host : NULL,
              NULL, "disconnect", "app", 0, 0);
}

static void handle_action(Worker *worker, const UiCommand *command)
{
    UiPane issuing = local_view(command);
    if (command->action == UI_ACTION_UPLOAD ||
        command->action == UI_ACTION_DOWNLOAD) {
        UiCommand transfer = *command;
        transfer.type = UI_CMD_TRANSFER;
        transfer.pane = command->action == UI_ACTION_UPLOAD
                            ? issuing
                            : ui_pane_remote_view();
        handle_transfer(worker, &transfer);
        return;
    }

    if (command->pane == ui_pane_remote_view()) {
        if (command->action == UI_ACTION_DISCONNECT) {
            handle_disconnect(worker);
            return;
        }
        if (command->action == UI_ACTION_NEW_FOLDER ||
            command->action == UI_ACTION_RENAME ||
            command->action == UI_ACTION_DELETE) {
            queue_remote_action(worker, command);
            return;
        }
    }

    if (command->action != UI_ACTION_COPY &&
        command->action != UI_ACTION_MOVE &&
        command->action != UI_ACTION_DELETE &&
        command->action != UI_ACTION_NEW_FOLDER &&
        command->action != UI_ACTION_RENAME) {
        emit_error(worker, issuing, "ACTION NOT SUPPORTED", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }

    /* One local job at a time. A second action must not overwrite the run that
     * is still copying: the old run, its handle, and its temp would leak. */
    if (worker->local_run != NULL || worker->busy_plan != NULL) {
        emit_error(worker, issuing, "LOCAL JOB IN PROGRESS", OP_CAUSE_APP,
                   OP_STAGE_TRANSFER);
        return;
    }

    local_batch_begin(worker, command, action_direction(command->action));

    if (!worker->batch_executing && command->batch_id != 0) {
        if (worker->pending.valid) {
            queue_pending_action(worker, command);
            return;
        }
        if (worker->batch_preflight_count == 0 ||
            worker->batch_preflight_id != command->batch_id) {
            worker->batch_preflight_count = 0;
            worker->batch_preflight_id = command->batch_id;
        }
        if (worker->batch_preflight_count < UI_CLIP_MAX)
            worker->batch_preflight[worker->batch_preflight_count++] =
                *command;
        if (!command->batch_last)
            return;
        local_batch_preflight(worker);
        return;
    }

    if (worker->pending.valid) {
        queue_pending_action(worker, command);
        return;
    }

    if (command->action == UI_ACTION_NEW_FOLDER ||
        command->action == UI_ACTION_RENAME) {
        if (command->text[0] == '\0') {
            emit_error(worker, issuing, "NAME REQUIRED", OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
        Path target;
        if (command->action == UI_ACTION_NEW_FOLDER) {
            const char *dir = command->path2[0] != '\0' ? command->path2
                                                         : command->path;
            if (path_join(dir, command->text, &target) != PATH_OK) {
                emit_error(worker, issuing, "PATH TOO LONG", OP_CAUSE_APP,
                           OP_STAGE_PLAN);
                return;
            }
        } else {
            Path parent;
            if (path_parent(command->path, &parent) != PATH_OK ||
                path_join(parent.text, command->text, &target) != PATH_OK) {
                emit_error(worker, issuing, "PATH TOO LONG", OP_CAUSE_APP,
                           OP_STAGE_PLAN);
                return;
            }
        }
        UiCommand adjusted = *command;
        fmt_copy(adjusted.path2, sizeof(adjusted.path2), target.text);
        execute_action(worker, &adjusted,
                       command->value == 1
                           ? FSOP_CONFLICT_OVERWRITE
                           : (worker->local_conflict_set
                                  ? worker->local_conflict
                                  : FSOP_CONFLICT_ASK));
        return;
    }

    execute_action(worker, command,
                   command->value == 1
                       ? FSOP_CONFLICT_OVERWRITE
                       : (worker->local_conflict_set
                              ? worker->local_conflict
                              : FSOP_CONFLICT_ASK));
}

static void handle_save_site(Worker *worker, const UiCommand *command)
{
    int ok;
    if (command->index < 0)
        ok = site_book_add(&worker->book, &command->site);
    else
        ok = site_book_update(&worker->book, (unsigned)command->index,
                              &command->site);

    if (!ok) {
        HandoffEvent event;
        event_init(&event, HANDOFF_EVT_SITE_BOOK);
        event.ok = 0;
        fmt_copy(event.detail, sizeof(event.detail),
                 command->index < 0 ? "SITE BOOK FULL" : "SITE NOT FOUND");
        emit(worker, &event);
        return;
    }

    save_sites(worker);
    worker->site_dirty = 1;
    publish_site_book(worker);
}

static void handle_delete_site(Worker *worker, const UiCommand *command)
{
    if (!site_book_remove(&worker->book, (unsigned)command->index)) {
        HandoffEvent event;
        event_init(&event, HANDOFF_EVT_SITE_BOOK);
        event.ok = 0;
        fmt_copy(event.detail, sizeof(event.detail), "SITE NOT FOUND");
        emit(worker, &event);
        return;
    }
    if (worker->last_site_index == command->index)
        worker->last_site_index = -1;
    save_sites(worker);
    worker->site_dirty = 1;
    publish_site_book(worker);
}

static void handle_connect(Worker *worker, const UiCommand *command)
{
    const Site *site = site_book_at(&worker->book, (unsigned)command->index);
    if (site == NULL) {
        emit_error(worker, UI_PANE_REMOTE, "SITE NOT FOUND", OP_CAUSE_APP,
                   OP_STAGE_CONNECT);
        return;
    }

    /* One predicate for every client command that would arm a passive data
     * connection; a queued item must not be dropped by a session reset. */
    if (worker->transfer_active || xfer_busy(&worker->queue) ||
        ftp_server_busy(worker->server)) {
        emit_error(worker, UI_PANE_REMOTE, "TRANSFER IN PROGRESS",
                   OP_CAUSE_APP, OP_STAGE_CONNECT);
        return;
    }

    /* A connect attempt replaces the current session before it dials (KTD4):
     * the old control connection is torn down and the worker's session state
     * is reset, so a failure leaves no session behind and the UI can show the
     * reason without a stale live session under it (R13). This also fixes a
     * stale half-open session and a refused connect. */
    if (ftp_client_state(worker->client) != FTP_CLIENT_IDLE) {
        (void)ftp_client_disconnect(worker->client);
    }
    reset_remote_session(worker);

    worker->connecting_site = *site;
    worker->connecting_valid = 1;
    worker->last_site_index = command->index;

    FtpClientLogin login;
    memset(&login, 0, sizeof(login));
    login.host = worker->connecting_site.host;
    login.port = worker->connecting_site.port;
    login.anonymous = worker->connecting_site.anonymous;
    login.username = worker->connecting_site.username;
    login.password = worker->connecting_site.password;

    FtpClientResult result = ftp_client_connect(worker->client, &login);
    if (result != FTP_CLIENT_OK) {
        emit_error(worker, UI_PANE_REMOTE, "CONNECT REFUSED", OP_CAUSE_APP,
                   OP_STAGE_CONNECT);
    }
}

static void handle_transfer(Worker *worker, const UiCommand *command)
{
    if (ui_pane_is_local(command->pane) && !worker->connected) {
        emit_error(worker, local_view(command), "NOT CONNECTED",
                   OP_CAUSE_APP, OP_STAGE_CONNECT);
        return;
    }
    if (command->pane == ui_pane_remote_view() && !worker->connected) {
        emit_error(worker, ui_pane_remote_view(), "NOT CONNECTED",
                   OP_CAUSE_APP, OP_STAGE_CONNECT);
        return;
    }
    enqueue_transfer(worker, command);
}

static int local_destination_exists(Worker *worker, const XferItem *item)
{
    if (item->direction != XFER_DIR_DOWNLOAD)
        return 0;
    FsEntry entry;
    return worker->ports.fs->stat(worker->ports.fs->ctx, item->destination.text,
                                  &entry) == FS_OK;
}

static int local_destination_is_dir(Worker *worker, const XferItem *item)
{
    if (item->direction != XFER_DIR_DOWNLOAD)
        return 0;
    FsEntry entry;
    return worker->ports.fs->stat(worker->ports.fs->ctx, item->destination.text,
                                  &entry) == FS_OK &&
           entry.type == FS_ENTRY_DIR;
}

static void batch_finish(Worker *worker)
{
    if (worker->batch_total > 0 && worker->batch_done >= worker->batch_total)
        batch_reset(worker);
}

static void start_transfer_item(Worker *worker, const XferItem *item)
{
    /* A queued item waits for a held local path; it does not prompt. The
     * queue keeps the item active and retries on the next step. */
    ReserveKind kind = item->direction == XFER_DIR_DOWNLOAD ? RESERVE_WRITE
                                                            : RESERVE_READ;
    const char *local = item->direction == XFER_DIR_DOWNLOAD
                            ? item->destination.text
                            : item->source.text;
    char blocker[PATH_MAX_LEN];
    if (reserve_conflict(worker->reserve, kind, local, blocker,
                         sizeof(blocker))) {
        if (!worker->network_waiting)
            request_network_path_busy(worker, item, blocker);
        return;
    }
    worker->network_waiting = 0;
    if (!reserve_add(worker->reserve, RESERVE_LANE_NETWORK, kind, local)) {
        emit_error(worker, UI_PANE_REMOTE, "TOO MANY FILES TO TRACK", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        xfer_release(&worker->queue, NULL);
        worker->batch_done++;
        batch_finish(worker);
        return;
    }

    FtpClientResult result;
    worker->transfer_start_ms = now_ms(worker);
    worker->transfer_last_bytes = 0;
    worker->transfer_cancelled = 0;

    if (item->direction == XFER_DIR_DOWNLOAD) {
        FtpClientTransfer transfer;
        transfer.remote_path = item->source.text;
        transfer.local_path = item->destination.text;
        transfer.total = item->total;
        transfer.total_known = item->total_known;
        result = ftp_client_download(worker->client, &transfer, NULL);
    } else {
        FtpClientTransfer transfer;
        transfer.remote_path = item->destination.text;
        transfer.local_path = item->source.text;
        transfer.total = item->total;
        transfer.total_known = item->total_known;
        result = ftp_client_upload(worker->client, &transfer);
    }

    if (result != FTP_CLIENT_OK) {
        int local_failed = result == FTP_CLIENT_LOCAL_FAILED;
        OpCause cause = local_failed ? OP_CAUSE_STORAGE : OP_CAUSE_APP;
        const char *detail = local_failed ? ftp_client_last_detail(worker->client)
                                          : "TRANSFER REFUSED";
        XferItem next;
        memset(&next, 0, sizeof(next));
        xfer_release(&worker->queue, &next);
        reserve_release(worker->reserve, RESERVE_LANE_NETWORK);
        worker->batch_done++;
        emit_transfer_done(worker, item->direction, item->source.text, 0, 0, 0,
                           item->total, item->total_known, cause,
                           OP_STAGE_TRANSFER, 0, detail);
        emit_error(worker,
                   item->direction == XFER_DIR_DOWNLOAD ? UI_PANE_LOCAL
                                                        : UI_PANE_REMOTE,
                   detail, cause, OP_STAGE_TRANSFER);
        if (item->direction == XFER_DIR_DOWNLOAD)
            worker_list_local(worker, UI_PANE_LOCAL,
                              worker->local_path[UI_PANE_LOCAL]);
        batch_finish(worker);
        return;
    }

    /* A terminal the client reported inside the call already released the
     * item; latching now would recreate the wedge. */
    if (!xfer_busy(&worker->queue))
        return;

    worker->transfer_active = 1;
    log_event(worker, LOGEV_TRANSFER_START,
              worker->connecting_valid ? worker->connecting_site.name : NULL,
              worker->connecting_valid ? worker->connecting_site.host : NULL,
              item->source.text, "start", "app", item->total,
              item->total_known ? item->total : 0);
    HandoffEvent started;
    event_init(&started, HANDOFF_EVT_TRANSFER_STARTED);
    started.pane = item->direction == XFER_DIR_DOWNLOAD ? UI_PANE_REMOTE
                                                        : UI_PANE_LOCAL;
    started.direction = item->direction;
    started.total = item->total;
    started.total_known = item->total_known;
    started.item_index = worker->batch_done + 1;
    started.item_total = worker->batch_total > 0 ? worker->batch_total : 1;
    started.rate_known = 0;
    fmt_copy(started.detail, sizeof(started.detail),
             last_name(item->source.text));
    fmt_copy(started.path, sizeof(started.path), item->source.text);
    emit(worker, &started);
}

/* Start this item's preflight and give its answer an identity. The token is
 * captured here, not when the answer arrives: `batch_reset` moves the token on
 * while the probe flies, and the answer must still say which item it was asked
 * for (KTD3). */
/* A probe belongs to one item and one batch.  Dropping it moves the token on
 * and clears the refresh latches together, so an answer that outlives its item
 * matches nothing, even when the next item reuses the same destination path
 * (KTD3).  One helper because three sites drop a preflight and a missed latch
 * stalls the lane. */
static void upload_preflight_clear(Worker *worker)
{
    worker->upload_stat_active = 0;
    worker->upload_stat_ready = 0;
    worker->upload_stat_token++;
    worker->upload_stat_refresh_used = 0;
    worker->upload_stat_refresh_active = 0;
    worker->upload_stat_refresh_done = 0;
}

static void upload_stat_begin(Worker *worker, const char *destination,
                              int use_size)
{
    upload_preflight_clear(worker);
    worker->upload_stat_answer_token = worker->upload_stat_token;
    worker->upload_stat_active = 1;
    worker->upload_stat_used_size = use_size;
    fmt_copy(worker->upload_stat_path, sizeof(worker->upload_stat_path),
             destination);
}

/* R7: refresh the destination's own parent, once, and only while the pane
 * still shows that directory. KTD4: this is the ordinary listing path, so the
 * rows land in the pane's own slot and the next pump decides from them; it must
 * not set refresh_remote, which would ask for the listing again forever.
 * KTD5: LISTING_BEGIN runs inside ftp_client_list, so `collecting` says now
 * whether a slot was taken; waiting for a completion that never publishes
 * would wait forever. */
static UploadRefresh upload_stat_refresh(Worker *worker)
{
    char parent[PATH_MAX_LEN];
    if (!remote_parent(worker->upload_stat_path, parent, sizeof(parent)) ||
        strcmp(worker->remote_path, parent) != 0)
        return UPLOAD_REFRESH_UNTYPED;

    worker->listing_pane = ui_pane_remote_view();
    FtpClientResult result = ftp_client_list(worker->client, parent);
    if (result == FTP_CLIENT_BUSY)
        return UPLOAD_REFRESH_BUSY;
    if (result != FTP_CLIENT_OK)
        return UPLOAD_REFRESH_UNTYPED;
    return worker->collecting ? UPLOAD_REFRESH_STARTED
                              : UPLOAD_REFRESH_UNCOLLECTED;
}

static void pump_queue(Worker *worker)
{
    /*
     * Iterate instead of calling back into this function after a skipped or
     * failed item. Each frame carries an XferItem, and a 64-item SKIP batch
     * would otherwise need more than the 128 KiB worker stack (D57, D66).
     */
    for (;;) {
        if (worker->transfer_active || worker->pending.valid)
            return;
        if (ftp_server_busy(worker->server))
            return;
        if (ftp_client_busy(worker->client))
            return;

        XferItem item;
        int already_active = xfer_busy(&worker->queue);
        if (already_active) {
            if (!xfer_active(&worker->queue, &item))
                return;
        } else if (!xfer_claim(&worker->queue, &item)) {
            return;
        }

        if (item.direction == XFER_DIR_DOWNLOAD) {
            if (local_destination_is_dir(worker, &item)) {
                xfer_release(&worker->queue, NULL);
                worker->batch_done++;
                emit_transfer_done(worker, item.direction, item.source.text, 0, 0,
                                   0, item.total, item.total_known,
                                   OP_CAUSE_STORAGE, OP_STAGE_TRANSFER, 0,
                                   "DESTINATION IS A FOLDER");
                emit_error(worker, UI_PANE_LOCAL, "DESTINATION IS A FOLDER",
                           OP_CAUSE_STORAGE, OP_STAGE_TRANSFER);
                batch_finish(worker);
                continue;
            }
            if (local_destination_exists(worker, &item) &&
                !worker->batch_conflict_decided) {
                request_transfer_conflict(worker, &item);
                return;
            }
            if (local_destination_exists(worker, &item) &&
                worker->batch_conflict == XFER_CONFLICT_SKIP_EXISTING) {
                xfer_release(&worker->queue, NULL);
                worker->batch_done++;
                batch_finish(worker);
                continue;
            }
            start_transfer_item(worker, &item);
            return;
        }

        /* Uploads: check the remote destination before the STOR. Prefer
         * MLST; a server without it (or one that refuses MLST) falls back to
         * SIZE. When neither answers, the transfer starts as before. R41 asks
         * once per batch and applies the answer to every file. */
        if (worker->upload_stat_refresh_active)
            return; /* the refresh listing owns the lane until it ends */

        /* An answer decides this item only when it was asked for this item
         * (KTD3). The refreshed rows are the answer's second half, so a
         * finished refresh re-enters the same decision (KTD4). */
        int answering = 0;
        if (worker->upload_stat_refresh_done) {
            worker->upload_stat_refresh_done = 0;
            answering = worker->upload_stat_answer_token ==
                        worker->upload_stat_token;
        } else if (worker->upload_stat_ready) {
            worker->upload_stat_ready = 0;
            answering = worker->upload_stat_answer_token ==
                        worker->upload_stat_token;
        }

        if (answering) {
            if (worker->upload_stat_failed) {
                emit_error(worker, UI_PANE_REMOTE, "REMOTE CHECK FAILED",
                           worker->upload_stat_cause,
                           worker->upload_stat_stage);
                xfer_release(&worker->queue, NULL);
                worker->batch_done++;
                batch_finish(worker);
                continue;
            }
            if (worker->upload_stat_unsupported &&
                !worker->upload_stat_used_size) {
                /* MLST was refused; ask SIZE before giving up (b37). This is
                 * still the same item, so it keeps the same token. */
                FtpClientResult size =
                    ftp_client_size(worker->client, worker->upload_stat_path);
                if (size == FTP_CLIENT_OK) {
                    worker->upload_stat_used_size = 1;
                    worker->upload_stat_active = 1;
                    return;
                }
                if (size == FTP_CLIENT_BUSY)
                    return;
                emit_error(worker, UI_PANE_REMOTE, "NOT CONNECTED",
                           OP_CAUSE_APP, OP_STAGE_CONNECT);
                xfer_release(&worker->queue, NULL);
                worker->batch_done++;
                batch_finish(worker);
                continue;
            }
            if (worker->upload_stat_unsupported) {
                /* SIZE is refused too: no probe exists on this server. */
                worker->upload_stat_disabled = 1;
            }
            if (worker->upload_stat_unsupported || !worker->upload_stat_exists) {
                start_transfer_item(worker, &item);
                return;
            }
            /* SIZE says the destination exists but cannot say what it is. The
             * pane's rows type it when they belong to its parent; a row that
             * says "file" needs no refresh, and a missing row costs one. */
            if (!worker->upload_stat_is_dir && worker->upload_stat_used_size) {
                int row_type =
                    remote_listing_type(worker, worker->upload_stat_path);
                if (row_type == UI_ROW_DIR)
                    worker->upload_stat_is_dir = 1;
                else if (row_type < 0 && !worker->upload_stat_refresh_used) {
                    UploadRefresh refresh = upload_stat_refresh(worker);
                    if (refresh == UPLOAD_REFRESH_BUSY)
                        return; /* keepalive holds the slot; retry next step */
                    worker->upload_stat_refresh_used = 1;
                    if (refresh == UPLOAD_REFRESH_STARTED) {
                        worker->upload_stat_refresh_active = 1;
                        return; /* the rows decide it when the listing ends */
                    }
                    if (refresh == UPLOAD_REFRESH_UNCOLLECTED) {
                        /* The listing runs but will never be collected; decide
                         * once the client is idle again (R9). */
                        worker->upload_stat_refresh_done = 1;
                        return;
                    }
                }
            }
            if (worker->upload_stat_is_dir) {
                emit_error(worker, UI_PANE_REMOTE, "DESTINATION IS A FOLDER",
                           OP_CAUSE_APP, OP_STAGE_TRANSFER);
                xfer_release(&worker->queue, NULL);
                worker->batch_done++;
                batch_finish(worker);
                continue;
            }
            if (worker->batch_conflict == XFER_CONFLICT_SKIP_EXISTING) {
                xfer_release(&worker->queue, NULL);
                worker->batch_done++;
                batch_finish(worker);
                continue;
            }
            if (!worker->batch_conflict_decided) {
                request_transfer_conflict(worker, &item);
                return;
            }
            start_transfer_item(worker, &item);
            return;
        }

        if (worker->batch_conflict != XFER_CONFLICT_OVERWRITE_ALL &&
            !worker->upload_stat_disabled) {
            int use_size = worker->upload_stat_used_size ||
                           !ftp_client_has_mlsd(worker->client);
            FtpClientResult check =
                use_size ? ftp_client_size(worker->client,
                                           item.destination.text)
                         : ftp_client_mlst(worker->client,
                                           item.destination.text);
            if (check == FTP_CLIENT_OK) {
                upload_stat_begin(worker, item.destination.text, use_size);
                return;
            }
            if (check == FTP_CLIENT_BUSY)
                return; /* a keepalive is in flight; retry on the next step */
            emit_error(worker, UI_PANE_REMOTE, "NOT CONNECTED", OP_CAUSE_APP,
                       OP_STAGE_CONNECT);
            xfer_release(&worker->queue, NULL);
            worker->batch_done++;
            batch_finish(worker);
            continue;
        }

        start_transfer_item(worker, &item);
        return;
    }
}

static void default_local_dir(Worker *worker, char *out, size_t cap)
{
    FsEntry entry;
    if (worker->ports.fs->stat(worker->ports.fs->ctx, "ux0:data", &entry) ==
            FS_OK &&
        entry.type == FS_ENTRY_DIR) {
        fmt_copy(out, cap, "ux0:data");
        return;
    }
    for (int v = 0; v < VOLUME_COUNT; ++v) {
        const char *name = path_volume_name((Volume)v);
        if (worker->ports.fs->stat(worker->ports.fs->ctx, name, &entry) ==
                FS_OK &&
            entry.type == FS_ENTRY_DIR) {
            fmt_copy(out, cap, name);
            return;
        }
    }
    fmt_copy(out, cap, "ux0:data");
}

static void enqueue_transfer(Worker *worker, const UiCommand *command)
{
    XferItem item;
    memset(&item, 0, sizeof(item));

    /* R22/R23 are deliberately deferred (D65): a folder is never sent as one
     * RETR/STOR. Refuse it here as the last line of defence. */
    if (command->source_type == UI_ROW_DIR) {
        emit_error(worker, command->pane, "FOLDER TRANSFER NOT SUPPORTED",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return;
    }

    if (ui_pane_is_local(command->pane)) {
        UiPane issuing = local_view(command);
        item.direction = XFER_DIR_UPLOAD;
        Path source;
        if (path_canonicalize(command->path, &source) != PATH_OK) {
            emit_error(worker, issuing, "INVALID SOURCE", OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
        const char *name = last_name(source.text);
        if (!path_name_is_safe(name)) {
            emit_error(worker, issuing, "UNSAFE NAME", OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
        fmt_copy(item.source.text, sizeof(item.source.text), source.text);
        char remote[PATH_MAX_LEN];
        if (!remote_join(command->path2, name, remote, sizeof(remote))) {
            emit_error(worker, issuing, "REMOTE PATH TOO LONG",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
            return;
        }
        fmt_copy(item.destination.text, sizeof(item.destination.text), remote);
        FsEntry entry;
        if (worker->ports.fs->stat(worker->ports.fs->ctx, source.text,
                                   &entry) == FS_OK) {
            if (entry.type == FS_ENTRY_DIR) {
                emit_error(worker, issuing,
                           "FOLDER TRANSFER NOT SUPPORTED", OP_CAUSE_APP,
                           OP_STAGE_PLAN);
                return;
            }
            item.total = entry.size;
            item.total_known = entry.size_known;
        }
    } else {
        item.direction = XFER_DIR_DOWNLOAD;
        fmt_copy(item.source.text, sizeof(item.source.text), command->path);
        const char *name = remote_last_name(command->path);
        if (!path_name_is_safe(name)) {
            emit_error(worker, UI_PANE_REMOTE, "UNSAFE REMOTE NAME",
                       OP_CAUSE_SERVER, OP_STAGE_PLAN);
            return;
        }
        char fallback[PATH_MAX_LEN];
        const char *destination_dir = command->path2;
        if (destination_dir[0] == '\0' ||
            strcmp(destination_dir, "*") == 0) {
            default_local_dir(worker, fallback, sizeof(fallback));
            destination_dir = fallback;
        }
        Path dest;
        if (path_join(destination_dir, name, &dest) != PATH_OK) {
            emit_error(worker, UI_PANE_REMOTE, "LOCAL PATH TOO LONG",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
            return;
        }
        /* Every download entry point shares this refusal: the ACTIONS row
         * dims earlier, but Cross and a marked set reach the queue directly
         * (R2). A read-only destination must fail before an item exists. */
        PathResult writable = path_check_write(dest.text, 0, NULL);
        if (writable != PATH_OK) {
            const char *reason = "INVALID PATH";
            if (writable == PATH_READ_ONLY)
                reason = "READ-ONLY MOUNT";
            else if (writable == PATH_PROTECTED)
                reason = "PROTECTED PATH";
            else if (writable == PATH_DENIED)
                reason = "DENIED PATH";
            emit_error(worker, UI_PANE_REMOTE, reason, OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
        fmt_copy(item.destination.text, sizeof(item.destination.text),
                 dest.text);
        item.total = command->total;
        item.total_known = command->total_known;
    }

    if (!xfer_enqueue(&worker->queue, &item)) {
        emit_error(worker,
                   ui_pane_is_local(command->pane) ? local_view(command)
                                                   : ui_pane_remote_view(),
                   "QUEUE FULL", OP_CAUSE_APP, OP_STAGE_PLAN);
        return;
    }
    worker->batch_total++;
    log_event(worker, LOGEV_QUEUE, NULL, NULL, item.source.text, "enqueue",
              "app", (uint64_t)xfer_depth(&worker->queue), 0);
    pump_queue(worker);
}

static void client_listing_end(Worker *worker, const FtpClientEvent *event)
{
    /* The folder's own cdir/pdir row is not published, but its perm facts are
     * the current folder's capability (R18). Only a successful listing of the
     * folder the session sits in may stamp it; a preflight of another folder
     * says nothing about the pane's folder. */
    if (event->ok && event->perm_known && event->path != NULL &&
        strcmp(event->path, ftp_client_cwd_text(worker->client)) == 0)
        remote_perm_set(worker, event->perm_mask);
    if (!worker->collecting)
        return;
    remote_listing_end(worker, event->ok);
    /* A preflight refresh ends here, whatever its result: the same decision
     * runs again with the rows just published, and an empty or failed listing
     * leaves the destination untyped (R8, R9). The latch is separate from
     * upload_stat_active, which clears when the SIZE answer arrives, or a
     * cancel would be refused only while the probe flies. */
    if (worker->upload_stat_refresh_active) {
        worker->upload_stat_refresh_active = 0;
        worker->upload_stat_refresh_done = 1;
    }
}

static void client_operation_done(Worker *worker, const FtpClientEvent *event)
{
    if (event->operation == FTP_CLIENT_OP_CWD) {
        if (event->ok) {
            fmt_copy(worker->remote_path, sizeof(worker->remote_path),
                     ftp_client_cwd_text(worker->client));
            /* A new folder is unknown until its own facts arrive. The client
             * is still inside its operation event; the listing and the MLST
             * start from worker_step once the slot is released. */
            remote_perm_clear(worker, 1);
            worker->remote_perm_pending = 1;
            worker->refresh_remote = 1;
        } else {
            emit_error(worker, UI_PANE_REMOTE,
                       event->detail != NULL ? event->detail : "CWD FAILED",
                       event->cause, event->stage);
        }
        return;
    }

    if (event->operation == FTP_CLIENT_OP_STAT) {
        if (worker->remote_perm_active) {
            worker->remote_perm_active = 0;
            /* A 550 or an absent perm never marks the folder (R18). */
            if (event->ok && event->exists && event->perm_known)
                remote_perm_set(worker, event->perm_mask);
            return;
        }
        if (!worker->upload_stat_active)
            return;
        /* The answer keeps the token the request was started under, so a batch
         * reset in between cannot make it look like the next item's. */
        worker->upload_stat_active = 0;
        worker->upload_stat_ready = 1;
        worker->upload_stat_failed = !event->ok;
        worker->upload_stat_cause = event->cause;
        worker->upload_stat_stage = event->stage;
        worker->upload_stat_exists = event->exists;
        worker->upload_stat_is_dir = event->is_dir;
        worker->upload_stat_unsupported = event->unsupported;
        return;
    }

    if (event->operation == FTP_CLIENT_OP_MKDIR ||
        event->operation == FTP_CLIENT_OP_RENAME ||
        event->operation == FTP_CLIENT_OP_DELETE ||
        event->operation == FTP_CLIENT_OP_RMDIR) {
        if (!event->ok)
            emit_error(worker, UI_PANE_REMOTE,
                       event->detail != NULL ? event->detail : "REMOTE FAILED",
                       event->cause, event->stage);
        if (worker->remote_action_count == 0)
            worker->refresh_remote = 1;
        pump_remote_actions(worker);
        return;
    }

    if (event->operation != FTP_CLIENT_OP_DOWNLOAD &&
        event->operation != FTP_CLIENT_OP_UPLOAD)
        return;

    /* Queue ownership, not the lane latch: a synchronous terminal must never
     * be dropped while an item is still claimed. */
    if (!xfer_busy(&worker->queue))
        return;

    int ok = event->ok;
    int cancelled = worker->transfer_cancelled;
    XferItem finished;
    memset(&finished, 0, sizeof(finished));
    (void)xfer_active(&worker->queue, &finished);

    xfer_release(&worker->queue, NULL);
    reserve_release(worker->reserve, RESERVE_LANE_NETWORK);
    worker->transfer_active = 0;
    worker->transfer_cancelled = 0;
    worker->batch_done++;

    if (cancelled) {
        HandoffEvent cancelled_event;
        event_init(&cancelled_event, HANDOFF_EVT_TRANSFER_CANCELLED);
        cancelled_event.direction = finished.direction;
        cancelled_event.direction_known = 1;
        emit(worker, &cancelled_event);
    } else if (ok) {
        emit_transfer_done(worker, finished.direction, finished.source.text, 1, 0,
                           event->bytes, event->total, event->total_known,
                           OP_CAUSE_APP, OP_STAGE_TRANSFER,
                           worker->transfer_start_ms, NULL);
        log_event(worker, LOGEV_TRANSFER_DONE,
                  worker->connecting_valid ? worker->connecting_site.name : NULL,
                  worker->connecting_valid ? worker->connecting_site.host : NULL,
                  finished.source.text, "done", "app", event->bytes,
                  event->total);
        if (finished.direction == XFER_DIR_DOWNLOAD)
            refresh_local_after_download(worker, &finished);
        else
            worker->refresh_remote = 1;
    } else {
        emit_transfer_done(worker, finished.direction, finished.source.text, 0,
                           event->file_kept, event->bytes, event->total, event->total_known,
                           event->cause, event->stage,
                           worker->transfer_start_ms, event->detail);
        emit_error(worker,
                   event->operation == FTP_CLIENT_OP_UPLOAD ? UI_PANE_LOCAL
                                                            : UI_PANE_REMOTE,
                   event->detail != NULL ? event->detail : "TRANSFER FAILED",
                   event->cause, event->stage);
        log_event(worker, LOGEV_TRANSFER_FAIL,
                  worker->connecting_valid ? worker->connecting_site.name : NULL,
                  worker->connecting_valid ? worker->connecting_site.host : NULL,
                  finished.source.text, op_stage_name(event->stage),
                  op_cause_name(event->cause), event->bytes, event->total);
    }

    batch_finish(worker);
    /* worker_step pumps every step; pumping here would nest a second transfer
     * inside the client call that produced this event. */
}

static void client_event(void *ctx, const FtpClientEvent *event)
{
    Worker *worker = ctx;
    if (worker == NULL || event == NULL)
        return;

    switch (event->kind) {
    case FTP_CLIENT_EVENT_STATE:
        if (event->state == FTP_CLIENT_CONNECTING) {
            if (worker->ports.log != NULL) {
                char address[16];
                format_ip(event->peer.addr, address, sizeof(address));
                LogRecord record;
                logev_init(&record, LOGEV_CONNECT);
                if (worker->connecting_valid) {
                    record.site = worker->connecting_site.name;
                    record.host = worker->connecting_site.host;
                }
                record.path = address[0] != '\0' ? address : NULL;
                record.port = (int)event->peer.port;
                record.stage = "begin";
                record.cls = "app";
                (void)logev_emit(worker->ports.log, now_ms(worker), &record);
            }
        } else if (event->state == FTP_CLIENT_READY) {
            if (!worker->connected) {
                worker->connected = 1;
                /* The login folder's own capability is unknown until the
                 * probe answers; the first listing may already have supplied
                 * it from its cdir row. */
                worker->remote_perm_pending = 1;
                emit_session(worker, 1, NULL);
                if (worker->connecting_valid) {
                    log_event(worker, LOGEV_CONNECT,
                              worker->connecting_site.name,
                              worker->connecting_site.host, NULL, "ready",
                              "app", 0, 0);
                    log_event(worker, LOGEV_LOGIN,
                              worker->connecting_site.name,
                              worker->connecting_site.host, NULL, "login",
                              "app", 0, 0);
                }
            }
        } else if (event->state == FTP_CLIENT_LOST ||
                   event->state == FTP_CLIENT_FAILED ||
                   event->state == FTP_CLIENT_STALLED) {
            worker->connected = 0;
            if (worker->ports.log != NULL) {
                LogRecord record;
                logev_init(&record, LOGEV_CONNECT_FAIL);
                if (worker->connecting_valid) {
                    record.site = worker->connecting_site.name;
                    record.host = worker->connecting_site.host;
                }
                char address[16];
                format_ip(event->peer.addr, address, sizeof(address));
                record.path = address[0] != '\0' ? address : NULL;
                record.port = (int)event->peer.port;
                record.stage = op_stage_name(event->stage);
                record.cls = op_cause_name(event->cause);
                record.detail = event->detail;
                record.value = (int)event->net;
                (void)logev_emit(worker->ports.log, now_ms(worker), &record);
            }
            emit_session(worker, 0,
                         event->detail != NULL ? event->detail
                                               : "CONTROL CONNECTION LOST");
            if (worker->transfer_active) {
                XferItem item;
                memset(&item, 0, sizeof(item));
                (void)xfer_active(&worker->queue, &item);
                xfer_release(&worker->queue, NULL);
                reserve_release(worker->reserve, RESERVE_LANE_NETWORK);
                worker->transfer_active = 0;
                /* The queue cannot survive the session, so the one terminal
                 * names what else died with it instead of clearing silently. */
                char detail[128];
                int remaining = worker->batch_total - worker->batch_done - 1;
                const char *text = event->detail != NULL ? event->detail
                                                         : "CONNECTION LOST";
                if (remaining > 0) {
                    snprintf(detail, sizeof(detail),
                             remaining == 1
                                 ? "%s - 1 ITEM CANCELLED"
                                 : "%s - %d ITEMS CANCELLED",
                             text, remaining);
                    text = detail;
                }
                emit_transfer_done(worker, item.direction, item.source.text, 0,
                                   event->file_kept, event->bytes, event->total,
                                   event->total_known, event->cause,
                                   event->stage, worker->transfer_start_ms,
                                   text);
                if (event->file_kept &&
                    item.direction == XFER_DIR_DOWNLOAD)
                    refresh_local_after_download(worker, &item);
            }
            reset_remote_session(worker);
        }
        break;
    case FTP_CLIENT_EVENT_LISTING_BEGIN:
        /* U2: the walk's private collector runs first, so its rows never take
         * a pane slot (KTD3). */
        if (walk_listing_begin(worker, event))
            break;
        remote_listing_begin(worker, worker->listing_pane, event->path);
        break;
    case FTP_CLIENT_EVENT_LISTING_ROW:
        if (walk_listing_row(worker, event))
            break;
        remote_listing_row(worker, event->row);
        break;
    case FTP_CLIENT_EVENT_LISTING_END:
        if (walk_listing_end(worker, event))
            break;
        client_listing_end(worker, event);
        break;
    case FTP_CLIENT_EVENT_PROGRESS: {
        if (!worker->transfer_active)
            break;
        uint64_t elapsed = now_ms(worker) - worker->transfer_start_ms;
        HandoffEvent progress;
        event_init(&progress, HANDOFF_EVT_PROGRESS);
        progress.bytes = event->bytes;
        progress.total = event->total;
        progress.total_known = event->total_known;
        progress.item_index = worker->batch_done + 1;
        progress.item_total = worker->batch_total > 0 ? worker->batch_total : 1;
        progress.elapsed_ms = elapsed;
        progress.elapsed_known = elapsed > 0;
        if (elapsed > 0) {
            progress.rate = event->bytes * 1000u / elapsed;
            progress.rate_known = 1;
        }
        emit(worker, &progress);
        break;
    }
    case FTP_CLIENT_EVENT_OPERATION_DONE:
        client_operation_done(worker, event);
        break;
    default:
        break;
    }
}

static const char *server_op_name(const char *command)
{
    if (strcmp(command, "RETR") == 0)
        return "DOWNLOAD";
    if (strcmp(command, "STOR") == 0)
        return "UPLOAD";
    return "BROWSING";
}

/* D84 diagnostic: the FTP command/reply and data milestones go to the session
 * log, so a console-only upload failure has a trace. PASS is masked by the
 * server before this callback runs. */
static void server_trace_line(void *ctx, const char *line)
{
    Worker *worker = ctx;
    if (worker == NULL || line == NULL)
        return;
    LogRecord record;
    logev_init(&record, LOGEV_LOG);
    record.detail = line;
    (void)logev_emit(worker->ports.log, now_ms(worker), &record);
}

static void server_event(void *ctx, const FtpServerEvent *event)
{
    Worker *worker = ctx;
    if (worker == NULL || event == NULL)
        return;

    UiServerRun run = UI_SERVER_OFF;
    if (event->state == FTP_SERVER_BINDING ||
        event->state == FTP_SERVER_STOPPING)
        run = UI_SERVER_STARTING;
    else if (event->state == FTP_SERVER_ON ||
             event->state == FTP_SERVER_SESSION ||
             event->state == FTP_SERVER_REFUSING)
        run = UI_SERVER_ON;

    switch (event->kind) {
    case FTP_SERVER_EVENT_STATE:
        emit_server_state(worker, run, event->clients);
        if (run == UI_SERVER_OFF)
            worker->server_transfer_active = 0;
        break;
    case FTP_SERVER_EVENT_CLIENT: {
        HandoffEvent client;
        event_init(&client, HANDOFF_EVT_SERVER_CLIENT);
        client.ok = event->clients > 0;
        client.clients = event->clients;
        char peer[32] = "--";
        if (event->clients > 0 && event->peer.addr != 0)
            format_peer(event->peer, peer, sizeof(peer));
        fmt_copy(client.detail, sizeof(client.detail), peer);
        fmt_copy(client.op, sizeof(client.op),
                 server_op_name(event->op != NULL ? event->op : ""));
        fmt_copy(client.path, sizeof(client.path),
                 event->path != NULL ? event->path : "");
        client.bytes = event->bytes;
        client.total = event->total;
        client.total_known = event->total_known;
        emit(worker, &client);

        LogRecord record;
        logev_init(&record, LOGEV_SERVER_CLIENT);
        record.detail = peer;
        record.path = event->path != NULL ? event->path : NULL;
        record.clients = event->clients;
        record.bytes = event->bytes;
        record.total = event->total;
        (void)logev_emit(worker->ports.log, now_ms(worker), &record);
        break;
    }
    case FTP_SERVER_EVENT_PROGRESS: {
        if (!worker->server_transfer_active) {
            worker->server_transfer_active = 1;
            worker->server_transfer_start_ms = now_ms(worker);
            const char *command = event->op;
            worker->server_direction =
                command != NULL && strcmp(command, "STOR") == 0
                                           ? XFER_DIR_SERVER_UPLOAD
                                           : XFER_DIR_SERVER_DOWNLOAD;
            fmt_copy(worker->server_name, sizeof(worker->server_name),
                     event->path != NULL ? last_name(event->path) : "SERVER");
            HandoffEvent started;
            event_init(&started, HANDOFF_EVT_TRANSFER_STARTED);
            started.direction = worker->server_direction;
            started.total = event->total;
            started.total_known = event->total_known;
            fmt_copy(started.detail, sizeof(started.detail),
                     worker->server_name);
            emit(worker, &started);
        }
        HandoffEvent progress;
        event_init(&progress, HANDOFF_EVT_PROGRESS);
        progress.bytes = event->bytes;
        progress.total = event->total;
        progress.total_known = event->total_known;
        emit(worker, &progress);
        break;
    }
    case FTP_SERVER_EVENT_TRANSFER_DONE:
        worker->server_transfer_active = 0;
        emit_transfer_done(worker, worker->server_direction,
                           worker->server_name, event->ok, 0, event->bytes,
                           event->total, event->total_known, event->cause,
                           event->stage, worker->server_transfer_start_ms,
                           event->detail);
        log_event(worker, event->ok ? LOGEV_TRANSFER_DONE : LOGEV_TRANSFER_FAIL,
                  NULL, NULL, worker->server_name,
                  op_stage_name(event->stage), op_cause_name(event->cause),
                  event->bytes, event->total);
        break;
    case FTP_SERVER_EVENT_ERROR:
        emit_error(worker, UI_PANE_REMOTE,
                   event->detail != NULL ? event->detail : "SERVER ERROR",
                   event->cause, event->stage);
        emit_server_state(worker, run, event->clients);
        break;
    default:
        break;
    }
}

static void cancel_local_lane(Worker *worker)
{
    if (worker->local_run != NULL) {
        worker->local_cancel_requested = 1;
        worker->local_batch_cancel_requested = 1;
        return;
    }
    if (worker->local_cancel_seen) {
        /* The executor already saw this cancel and stopped. */
        worker->local_cancel_seen = 0;
        return;
    }
    if (worker->busy_plan != NULL) {
        free(worker->busy_plan);
        worker->busy_plan = NULL;
        worker->busy_waiting = 0;
    }
    if (worker->local_batch.active)
        local_batch_cancel(worker);
}

static void cancel_network_transfer(Worker *worker)
{
    /* The probe and its refresh are two commands but one preflight; a cancel
     * between them would leave the item undecided, so both latches refuse. */
    if (worker->upload_stat_active || worker->upload_stat_refresh_active) {
        emit_error(worker, UI_PANE_REMOTE, "REMOTE CHECK IN PROGRESS",
                   OP_CAUSE_APP, OP_STAGE_COMMAND);
        return;
    }
    if (worker->transfer_active) {
        worker->transfer_cancelled = 1;
        if (ftp_client_cancel(worker->client) != FTP_CLIENT_OK)
            emit_error(worker, UI_PANE_REMOTE, "CANCEL UNAVAILABLE",
                       OP_CAUSE_APP, OP_STAGE_TRANSFER);
        return;
    }
    if (xfer_busy(&worker->queue)) {
        /* A preflight answer that outlived its item must not decide the next
         * one (the client op, if still active, is ignored on arrival). */
        worker->upload_stat_ready = 0;
        upload_preflight_clear(worker);
        (void)xfer_cancel_active(&worker->queue, NULL);
        worker->batch_done++;
        batch_finish(worker);
        HandoffEvent cancelled;
        event_init(&cancelled, HANDOFF_EVT_TRANSFER_CANCELLED);
        emit(worker, &cancelled);
        pump_queue(worker);
    }
}

/* The single-lane confirm means the lane that is running. */
static void cancel_current_transfer(Worker *worker)
{
    if (worker->local_run != NULL || worker->local_batch.active ||
        worker->busy_plan != NULL) {
        cancel_local_lane(worker);
        return;
    }
    cancel_network_transfer(worker);
}

static void handle_command(Worker *worker, const UiCommand *command)
{
    switch (command->type) {
    case UI_CMD_OPEN_PATH:
        if (ui_pane_is_local(command->pane)) {
            worker_list_local(worker, (UiPane)command->pane, command->path);
        } else if (!worker->connected) {
            emit_error(worker, ui_pane_remote_view(), "NOT CONNECTED",
                       OP_CAUSE_APP, OP_STAGE_COMMAND);
        } else {
            FtpClientResult result =
                ftp_client_cwd(worker->client, command->path);
            if (result != FTP_CLIENT_OK)
                emit_error(worker, ui_pane_remote_view(),
                           result == FTP_CLIENT_BUSY ? "BUSY - TRY AGAIN"
                                                     : "NOT CONNECTED",
                           OP_CAUSE_APP, OP_STAGE_COMMAND);
        }
        break;
    case UI_CMD_GO_PARENT:
        if (ui_pane_is_local(command->pane)) {
            UiPane pane = (UiPane)command->pane;
            const char *path = worker->local_path[local_index(pane)];
            if (strcmp(path, "*") == 0) {
                worker_list_root(worker, pane);
            } else if (path_is_volume_root(path)) {
                worker_list_root(worker, pane);
            } else {
                Path parent;
                if (path_parent(path, &parent) == PATH_OK)
                    worker_list_local(worker, pane, parent.text);
                else
                    worker_list_local(worker, pane, path);
            }
        } else if (!worker->connected) {
            emit_error(worker, ui_pane_remote_view(), "NOT CONNECTED",
                       OP_CAUSE_APP, OP_STAGE_COMMAND);
        } else {
            char parent[PATH_MAX_LEN];
            const char *cwd = ftp_client_cwd_text(worker->client);
            if (remote_parent(cwd, parent, sizeof(parent))) {
                FtpClientResult result =
                    ftp_client_cwd(worker->client, parent);
                if (result != FTP_CLIENT_OK)
                    emit_error(worker, ui_pane_remote_view(),
                               result == FTP_CLIENT_BUSY ? "BUSY - TRY AGAIN"
                                                         : "NOT CONNECTED",
                               OP_CAUSE_APP, OP_STAGE_COMMAND);
            }
        }
        break;
    case UI_CMD_REFRESH:
        if (ui_pane_is_local(command->pane)) {
            UiPane pane = (UiPane)command->pane;
            worker_list_local(worker, pane,
                              worker->local_path[local_index(pane)]);
        } else if (!worker->connected)
            emit_error(worker, ui_pane_remote_view(), "NOT CONNECTED",
                       OP_CAUSE_APP, OP_STAGE_COMMAND);
        else {
            /* R18: a refresh recomputes the folder's capability. */
            remote_perm_clear(worker, 1);
            worker->remote_perm_pending = 1;
            if (worker->transfer_active || xfer_busy(&worker->queue) ||
                ftp_server_busy(worker->server) ||
                worker_walk_active(worker))
                /* Latch it; worker_step re-issues once the lanes are idle. */
                worker->refresh_remote = 1;
            else if (remote_list(worker, ui_pane_remote_view()) !=
                     FTP_CLIENT_OK)
                emit_error(worker, UI_PANE_REMOTE, "BUSY - TRY AGAIN",
                           OP_CAUSE_APP, OP_STAGE_COMMAND);
        }
        break;
    case UI_CMD_CONNECT_SITE:
        handle_connect(worker, command);
        break;
    case UI_CMD_RECONNECT:
        if (worker->last_site_index >= 0) {
            UiCommand reconnect = *command;
            reconnect.index = worker->last_site_index;
            handle_connect(worker, &reconnect);
        } else {
            emit_error(worker, UI_PANE_REMOTE, "SITE NOT FOUND",
                       OP_CAUSE_APP, OP_STAGE_CONNECT);
        }
        break;
    case UI_CMD_TRANSFER:
        handle_transfer(worker, command);
        break;
    case UI_CMD_CANCEL_TRANSFER:
        cancel_current_transfer(worker);
        break;
    case UI_CMD_CANCEL_NETWORK:
        cancel_network_transfer(worker);
        break;
    case UI_CMD_CANCEL_LOCAL:
        cancel_local_lane(worker);
        break;
    case UI_CMD_CLEAR_QUEUE:
        worker->upload_stat_ready = 0;
        worker->upload_stat_active = 0;
        xfer_clear_queued(&worker->queue);
        if (worker->batch_total > 0)
            worker->batch_total = worker->batch_done > 0 ? worker->batch_done : 0;
        batch_finish(worker);
        break;
    case UI_CMD_EDIT_SITE: {
        const Site *site = site_book_at(&worker->book,
                                        (unsigned)command->index);
        if (site != NULL) {
            HandoffEvent event;
            event_init(&event, HANDOFF_EVT_EDIT_SITE);
            event.site = *site;
            emit(worker, &event);
        }
        break;
    }
    case UI_CMD_SAVE_SITE:
        handle_save_site(worker, command);
        break;
    case UI_CMD_DELETE_SITE:
        handle_delete_site(worker, command);
        break;
    case UI_CMD_RUN_ACTION:
        handle_action(worker, command);
        break;
    case UI_CMD_CONFIRM:
        if (worker->pending.valid &&
            command->prompt_id == worker->pending.prompt_id) {
            PendingWork pending = worker->pending;
            worker->pending.valid = 0;
            switch (pending.kind) {
            case PENDING_TRANSFER:
                if (command->choice == UI_CHOICE_CANCEL) {
                    /* CANCEL ends the batch, not just the item in the prompt. */
                    xfer_init(&worker->queue);
                    batch_reset(worker);
                    HandoffEvent cancelled;
                    event_init(&cancelled, HANDOFF_EVT_TRANSFER_CANCELLED);
                    emit(worker, &cancelled);
                } else if (command->choice == UI_CHOICE_OVERWRITE) {
                    worker->batch_conflict_decided = 1;
                    worker->batch_conflict = XFER_CONFLICT_OVERWRITE_ALL;
                    /* Let pump_queue re-check the client; a keepalive may be
                     * in flight while the prompt is open. */
                    pump_queue(worker);
                } else if (command->choice == UI_CHOICE_SKIP) {
                    worker->batch_conflict_decided = 1;
                    worker->batch_conflict = XFER_CONFLICT_SKIP_EXISTING;
                    xfer_release(&worker->queue, NULL);
                    worker->batch_done++;
                    batch_finish(worker);
                    pump_queue(worker);
                }
                break;
            case PENDING_BATCH_CONFLICT:
                if (command->choice == UI_CHOICE_CANCEL) {
                    worker->local_conflict_set = 0;
                    local_batch_cancel(worker);
                } else {
                    worker->local_conflict =
                        command->choice == UI_CHOICE_OVERWRITE
                            ? FSOP_CONFLICT_OVERWRITE
                            : command->choice == UI_CHOICE_SAVE_DUPLICATE
                                  ? FSOP_CONFLICT_DUPLICATE
                                  : FSOP_CONFLICT_SKIP;
                    worker->local_conflict_set = 1;
                    local_batch_execute(worker);
                }
                break;
            case PENDING_LOCAL_CONFLICT: {
                /* The prompt freed the plan, so the answer rebuilds it with the
                 * chosen decision. A refusal is a failed item, never a silent
                 * drop: no run and no new prompt means the planner said no. */
                if (command->choice == UI_CHOICE_CANCEL) {
                    worker->local_conflict_set = 0;
                    local_batch_cancel(worker);
                } else {
                    FsOpConflict conflict;
                    if (command->choice == UI_CHOICE_OVERWRITE)
                        conflict = FSOP_CONFLICT_OVERWRITE;
                    else if (command->choice == UI_CHOICE_SAVE_DUPLICATE)
                        conflict = FSOP_CONFLICT_DUPLICATE;
                    else
                        conflict = FSOP_CONFLICT_SKIP;
                    worker->local_conflict = conflict;
                    worker->local_conflict_set = 1;
                    execute_action(worker, &pending.command, conflict);
                    if (worker->local_run == NULL && !worker->pending.valid &&
                        worker->busy_plan == NULL &&
                        worker->local_batch.active)
                        worker->local_batch.failed++;
                }
                break;
            }
            case PENDING_REMOTE_WAIT:
                {
                XferItem item = pending.item;
                if (command->choice == UI_CHOICE_WAIT) {
                    worker->network_waiting = 1;
                } else if (command->choice == UI_CHOICE_SKIP) {
                    xfer_release(&worker->queue, NULL);
                    worker->batch_done++;
                    batch_finish(worker);
                    pump_queue(worker);
                } else {
                    xfer_init(&worker->queue);
                    batch_reset(worker);
                    worker->network_waiting = 0;
                    HandoffEvent cancelled;
                    event_init(&cancelled, HANDOFF_EVT_TRANSFER_CANCELLED);
                    cancelled.direction = item.direction;
                    cancelled.direction_known = 1;
                    emit(worker, &cancelled);
                }
                }
                break;
            case PENDING_BUSY:
                if (command->choice == UI_CHOICE_WAIT) {
                    worker->busy_waiting = 1;
                } else if (command->choice == UI_CHOICE_SKIP) {
                    FsOpPlan *plan = worker->busy_plan;
                    UiCommand command_copy = worker->busy_command;
                    worker->busy_plan = NULL;
                    worker->busy_waiting = 0;
                    free(plan);
                    local_batch_skip_item(worker, &command_copy);
                } else {
                    free(worker->busy_plan);
                    worker->busy_plan = NULL;
                    worker->busy_waiting = 0;
                    local_batch_cancel(worker);
                }
                break;
            case PENDING_NONE:
                break;
            }
        } else if (command->confirm == UI_CONFIRM_CANCEL_XFER &&
                   command->choice == UI_CHOICE_CANCEL) {
            cancel_current_transfer(worker);
        } else if (command->confirm == UI_CONFIRM_DELETE_SITE &&
                   command->choice == UI_CHOICE_DELETE) {
            handle_delete_site(worker, command);
        } else if (command->confirm == UI_CONFIRM_STOP_SERVER &&
                   command->choice == UI_CHOICE_STOP) {
            ftp_server_stop(worker->server);
            worker->server_transfer_active = 0;
            emit_server_state(worker, UI_SERVER_OFF, 0);
        }
        break;
    case UI_CMD_START_SERVER: {
        NetLink link;
        memset(&link, 0, sizeof(link));
        if (worker->ports.net->link(worker->ports.net->ctx, &link) != NET_OK ||
            !link.up) {
            emit_error(worker, UI_PANE_REMOTE, "NO NETWORK - CONNECT TO WI-FI",
                       OP_CAUSE_NETWORK, OP_STAGE_CONNECT);
            emit_server_state(worker, UI_SERVER_OFF, 0);
            break;
        }
        FtpServerResult result = ftp_server_start(
            worker->server, (uint16_t)worker->settings.server_port);
        if (result == FTP_SERVER_NO_NETWORK) {
            emit_error(worker, UI_PANE_REMOTE, "NO NETWORK - CONNECT TO WI-FI",
                       OP_CAUSE_NETWORK, OP_STAGE_CONNECT);
            emit_server_state(worker, UI_SERVER_OFF, 0);
        } else if (result == FTP_SERVER_BIND_FAILED) {
            char text[96];
            snprintf(text, sizeof(text),
                     "PORT %d BUSY - ANOTHER SERVER MAY USE IT",
                     worker->settings.server_port);
            emit_error(worker, UI_PANE_REMOTE, text, OP_CAUSE_APP,
                       OP_STAGE_CONNECT);
            emit_server_state(worker, UI_SERVER_OFF, 0);
        } else if (result == FTP_SERVER_INVALID) {
            /* The editor and the settings loader refuse this range, so the
             * answer is a backstop: say why, and leave the server OFF. */
            char text[96];
            snprintf(text, sizeof(text), "PORT %d MUST BE %d-%d",
                     (int)worker->settings.server_port,
                     WORKER_SERVER_PORT_MIN, WORKER_SERVER_PORT_MAX);
            emit_error(worker, UI_PANE_REMOTE, text, OP_CAUSE_APP,
                       OP_STAGE_CONNECT);
            emit_server_state(worker, UI_SERVER_OFF, 0);
        } else {
            /* ftp_server_start already emitted BINDING and ON. Do not emit a
             * second STARTING here: it would arrive after ON and overwrite it,
             * including the address. */
            if (worker->ports.log != NULL) {
                LogRecord record;
                logev_init(&record, LOGEV_SERVER_START);
                char address[32];
                address[0] = '\0';
                NetAddress lan = ftp_server_lan_address(worker->server);
                if (lan.addr != 0) {
                    snprintf(address, sizeof(address), "%u.%u.%u.%u",
                             (unsigned)((lan.addr >> 24) & 0xFF),
                             (unsigned)((lan.addr >> 16) & 0xFF),
                             (unsigned)((lan.addr >> 8) & 0xFF),
                             (unsigned)(lan.addr & 0xFF));
                    record.host = address;
                }
                record.port = (int)ftp_server_control_port(worker->server);
                record.detail = "start";
                record.cls = "app";
                (void)logev_emit(worker->ports.log, now_ms(worker), &record);
            }
        }
        break;
    }
    case UI_CMD_STOP_SERVER:
        ftp_server_stop(worker->server);
        worker->server_transfer_active = 0;
        emit_server_state(worker, UI_SERVER_OFF, 0);
        log_event(worker, LOGEV_SERVER_STOP, NULL, NULL, NULL, "stop", "app", 0,
                  0);
        break;
    case UI_CMD_EDIT_PORT:
        if (command->value < WORKER_SERVER_PORT_MIN ||
            command->value > WORKER_SERVER_PORT_MAX) {
            char text[64];
            snprintf(text, sizeof(text), "PORT MUST BE %d-%d",
                     WORKER_SERVER_PORT_MIN, WORKER_SERVER_PORT_MAX);
            emit_error(worker, UI_PANE_REMOTE, text, OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            break;
        }
        if (ftp_server_state(worker->server) != FTP_SERVER_OFF) {
            emit_error(worker, UI_PANE_REMOTE, "STOP THE SERVER FIRST",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
            break;
        }
        worker->settings.server_port = (uint16_t)command->value;
        worker->settings_dirty = 1;
        emit_server_state(worker, UI_SERVER_OFF, 0);
        break;
    case UI_CMD_QUIT:
        log_detail(worker, LOGEV_APP_STOP, "quit");
        __atomic_store_n(&worker->done, 1, __ATOMIC_RELEASE);
        ftp_server_stop(worker->server);
        if (worker->transfer_active)
            (void)ftp_client_cancel(worker->client);
        (void)ftp_client_quit(worker->client);
        save_settings(worker);
        break;
    case UI_CMD_DISMISS_ERROR:
        worker->last_error[0] = '\0';
        break;
    default:
        break;
    }
}

/* --- lifecycle --------------------------------------------------------- */

Worker *worker_create(const WorkerPorts *ports, Handoff *handoff)
{
    if (ports == NULL || ports->fs == NULL || ports->net == NULL ||
        ports->clock == NULL || handoff == NULL)
        return NULL;

    Worker *worker = calloc(1, sizeof(*worker));
    if (worker == NULL)
        return NULL;

    worker->ports = *ports;
    worker->handoff = handoff;
    worker->reserve = reserve_create();
    if (worker->reserve == NULL) {
        /* U5: a failed allocation refuses both lanes with one record instead of
         * running unguarded or looking like a crash. */
        if (ports->log != NULL) {
            LogRecord record;
            logev_init(&record, LOGEV_LOG);
            record.detail = "worker: reservation allocation failed";
            record.cls = "app";
            (void)logev_emit(ports->log,
                             ports->clock->now_ms(ports->clock->ctx), &record);
        }
        free(worker);
        return NULL;
    }
    worker->last_site_index = -1;
    worker->collect_pane = ui_pane_remote_view();
    worker->listing_pane = ui_pane_remote_view();
    worker->rows = calloc((size_t)UI_PANE_COUNT * HANDOFF_LIST_GENS *
                              WORKER_LIST_CAP,
                          sizeof(UiRow));
    if (worker->rows == NULL) {
        reserve_destroy(worker->reserve);
        free(worker);
        return NULL;
    }

    worker->client = ftp_client_create(ports->net, ports->fs, ports->clock,
                                       client_event, worker);
    worker->server = ftp_server_create(ports->net, ports->fs, ports->clock,
                                       server_event, worker);
    if (worker->server != NULL) {
        ftp_server_set_guard(worker->server, server_path_guard, worker);
        ftp_server_set_trace(worker->server, server_trace_line, worker);
    }
    if (worker->client == NULL || worker->server == NULL) {
        ftp_client_destroy(worker->client);
        ftp_server_destroy(worker->server);
        free(worker->rows);
        reserve_destroy(worker->reserve);
        free(worker);
        return NULL;
    }

    xfer_init(&worker->queue);
    fmt_copy(worker->store_dir, sizeof(worker->store_dir), ports->store_dir != NULL ? ports->store_dir : STORE_DIR);
    return worker;
}

void worker_destroy(Worker *worker)
{
    if (worker != NULL) {
        fsop_run_abort(worker->local_run, worker->ports.fs);
        fsop_run_destroy(worker->local_run);
        free(worker->busy_plan);
        free(worker->local_plan);
        worker->local_run = NULL;
        worker->local_plan = NULL;
    }
    if (worker == NULL)
        return;
    ftp_client_destroy(worker->client);
    ftp_server_destroy(worker->server);
    reserve_destroy(worker->reserve);
    free(worker->rows);
    free(worker);
}

static void pick_local_path(Worker *worker)
{
    const char *candidates[4] = {worker->settings.last_local, "ux0:data", "ux0:",
                                 NULL};
    FsEntry entry;
    for (int i = 0; i < 4; ++i) {
        const char *path = candidates[i];
        if (path == NULL || path[0] == '\0')
            continue;
        if (worker->ports.fs->stat(worker->ports.fs->ctx, path, &entry) ==
                FS_OK &&
            entry.type == FS_ENTRY_DIR) {
            fmt_copy(worker->local_path[UI_PANE_LOCAL],
                     sizeof(worker->local_path[UI_PANE_LOCAL]), path);
            return;
        }
    }

    for (int v = 0; v < VOLUME_COUNT; ++v) {
        const char *name = path_volume_name((Volume)v);
        if (worker->ports.fs->stat(worker->ports.fs->ctx, name, &entry) ==
                FS_OK &&
            entry.type == FS_ENTRY_DIR) {
            fmt_copy(worker->local_path[UI_PANE_LOCAL],
                     sizeof(worker->local_path[UI_PANE_LOCAL]), name);
            return;
        }
    }
    snprintf(worker->local_path[UI_PANE_LOCAL],
             sizeof(worker->local_path[UI_PANE_LOCAL]), "ux0:data");
}

void worker_start(Worker *worker)
{
    if (worker == NULL)
        return;

    (void)worker->ports.fs->mkdir(worker->ports.fs->ctx, worker->store_dir);

    settings_init(&worker->settings);
    site_book_init(&worker->book);

    StoreResult sites = store_load_sites(worker->ports.fs, worker->store_dir,
                                         &worker->book);
    if (sites != STORE_OK && sites != STORE_PARTIAL) {
        site_book_init(&worker->book);
    }
    if (site_book_count(&worker->book) == 0)
        site_book_seed_example(&worker->book);

    if (store_load_settings(worker->ports.fs, worker->store_dir,
                            &worker->settings) != STORE_OK) {
        settings_init(&worker->settings);
    }
    if (worker->settings.server_port < WORKER_SERVER_PORT_MIN ||
        worker->settings.server_port > WORKER_SERVER_PORT_MAX)
        worker->settings.server_port = 1337;

    pick_local_path(worker);
    /* Both local views open at ALL DRIVES. The right local view is listed at
     * start too, so its first switch already has rows (KTD1). */
    for (int n = 0; n < local_view_count(); ++n) {
        UiPane view = local_view_at(n);
        fmt_copy(worker->local_path[view], sizeof(worker->local_path[view]),
                 "*");
    }
    snprintf(worker->settings.last_local,
             sizeof(worker->settings.last_local), "%s",
             worker->local_path[UI_PANE_LOCAL]);

    worker->ready = 1;
    worker->initial_list = 1;
    worker->site_dirty = 1;
    worker->settings_dirty = 1;

    if (worker->ports.log != NULL) {
        LogRecord record;
        logev_init(&record, LOGEV_APP_START);
        record.version = VFTPSHELL_VERSION;
        record.build = VFTPSHELL_BUILD;
        record.detail = "ready";
        record.port = worker->settings.server_port;
        record.clients = (int)site_book_count(&worker->book);
        (void)logev_emit(worker->ports.log, now_ms(worker), &record);
    }
}

void worker_step(Worker *worker)
{
    if (worker == NULL || !worker->ready)
        return;

    drain_ui_log(worker);

    for (int i = 0; i < WORKER_CMD_BUDGET; ++i) {
        UiCommand command;
        if (!handoff_command_pop(worker->handoff, &command))
            break;
        log_command_received(worker, &command);
        handle_command(worker, &command);
    }

    if (__atomic_load_n(&worker->done, __ATOMIC_ACQUIRE))
        return;

    if (worker->initial_list) {
        worker->initial_list = 0;
        for (int n = 0; n < local_view_count(); ++n) {
            UiPane view = local_view_at(n);
            worker_list_local(worker, view, worker->local_path[view]);
        }
    }

    ftp_client_tick(worker->client,
                    worker->local_run != NULL ? 0 : WORKER_NET_WAIT_MS);
    ftp_server_tick(worker->server,
                    worker->local_run != NULL ? 0 : WORKER_NET_WAIT_MS);
    /* The folder probe goes first after a connect, a navigation, or a refresh,
     * so the pane learns the write facts before the new rows are acted on. */
    remote_perm_pump(worker);
    /* U2: a running walk owns the listing stream; a latched pane refresh waits
     * for its terminal instead of publishing rows over it. */
    worker_walk_pump(worker);
    if (worker->refresh_remote && worker->connected &&
        !worker_walk_active(worker) && !worker->transfer_active &&
        !xfer_busy(&worker->queue) && !ftp_server_busy(worker->server) &&
        !ftp_client_busy(worker->client)) {
        worker->refresh_remote = 0;
        if (remote_list(worker, ui_pane_remote_view()) != FTP_CLIENT_OK)
            emit_error(worker, UI_PANE_REMOTE, "LIST FAILED", OP_CAUSE_APP,
                       OP_STAGE_COMMAND);
    }
    pump_remote_actions(worker);
    pump_queue(worker);
    drain_pending_actions(worker);
    busy_retry(worker);
    local_run_pump(worker);
    local_batch_advance(worker);
    local_batch_flush(worker);
    publish_site_book(worker);
    save_settings(worker);
}

int worker_quit(const Worker *worker)
{
    return worker != NULL &&
           __atomic_load_n(&worker->done, __ATOMIC_ACQUIRE) != 0;
}

const Settings *worker_settings(const Worker *worker)
{
    return worker != NULL ? &worker->settings : NULL;
}

const SiteBook *worker_sites(const Worker *worker)
{
    return worker != NULL ? &worker->book : NULL;
}
