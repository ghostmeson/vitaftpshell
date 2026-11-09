/* SPDX-License-Identifier: GPL-3.0-only */

#include "app/worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cause.h"
#include "core/fmt.h"
#include "core/fsop_plan.h"
#include "core/fsop_run.h"
#include "core/logev.h"
#include "core/path.h"
#include "core/store.h"

#define WORKER_CMD_BUDGET 8
#define WORKER_REMOTE_DEPTH 32
#define WORKER_CONFIRM_ITEMS HANDOFF_CONFIRM_ITEMS

typedef struct {
    int valid;
    int kind; /* 1 transfer, 2 local action */
    UiCommand command;
    XferItem item;
    FsOpConflict conflict;
} PendingWork;

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

    char local_path[PATH_MAX_LEN];
    char remote_path[PATH_MAX_LEN];
    char store_dir[PATH_MAX_LEN];

    Site connecting_site;
    int connecting_valid;
    int last_site_index;
    int connected;
    int initial_list;
    int pending_list_after_cwd;
    int list_after_ready;

    UiRow *rows; /* [pane][generation][WORKER_LIST_CAP] */

    int collecting;
    UiPane collect_pane;
    int collect_slot;
    int collect_count;
    int collect_truncated;

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
    int transfer_active;
    int transfer_cancelled;
    uint64_t transfer_start_ms;
    uint64_t transfer_last_bytes;

    int server_transfer_active;
    XferDirection server_direction;
    char server_name[PATH_NAME_MAX];
    uint64_t server_transfer_start_ms;

    char last_error[160];
};

static void handle_action(Worker *worker, const UiCommand *command);
static void handle_transfer(Worker *worker, const UiCommand *command);
static void enqueue_transfer(Worker *worker, const UiCommand *command);
static void pump_queue(Worker *worker);
static void start_transfer_item(Worker *worker, const XferItem *item);

static uint64_t now_ms(Worker *worker)
{
    return worker->ports.clock->now_ms(worker->ports.clock->ctx);
}

static UiRow *rows_for(Worker *worker, UiPane pane, int slot)
{
    return &worker->rows[((int)pane * HANDOFF_LIST_GENS + slot) *
                          WORKER_LIST_CAP];
}

static void event_init(HandoffEvent *event, HandoffEventKind kind)
{
    memset(event, 0, sizeof(*event));
    event->kind = kind;
}

static void emit(Worker *worker, const HandoffEvent *event)
{
    handoff_event_push(worker->handoff, event);
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
        snprintf(event.address, sizeof(event.address), "%u.%u.%u.%u",
                 (unsigned)((lan.addr >> 24) & 0xFF),
                 (unsigned)((lan.addr >> 16) & 0xFF),
                 (unsigned)((lan.addr >> 8) & 0xFF),
                 (unsigned)(lan.addr & 0xFF));
        event.address_known = 1;
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
                             int free_known)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_LISTING_END);
    event.pane = pane;
    event.ok = ok;
    event.truncated = truncated;
    event.free_bytes = free_bytes;
    event.free_known = free_known;
    fmt_copy(event.path, sizeof(event.path), path != NULL ? path : "");
    emit(worker, &event);
}

static void emit_transfer_done(Worker *worker, XferDirection direction,
                               const char *name, int ok, uint64_t bytes,
                               uint64_t total, int total_known,
                               OpCause cause, OpStage stage, uint64_t start_ms)
{
    HandoffEvent event;
    event_init(&event, HANDOFF_EVT_TRANSFER_DONE);
    event.ok = ok;
    event.bytes = bytes;
    event.total = total;
    event.total_known = total_known;
    event.cause = cause;
    event.stage = stage;
    event.item_index = worker->batch_done + 1;
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
    emit(worker, &event);
    (void)direction;
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

static void worker_list_local(Worker *worker, const char *path)
{
    Path canonical;
    if (path == NULL || path_canonicalize(path, &canonical) != PATH_OK) {
        emit_error(worker, UI_PANE_LOCAL, "INVALID PATH", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }

    fmt_copy(worker->local_path, sizeof(worker->local_path), canonical.text);
    emit_listing_begin(worker, UI_PANE_LOCAL, canonical.text);

    int slot = handoff_listing_begin(worker->handoff, UI_PANE_LOCAL);
    if (slot < 0) {
        emit_listing_end(worker, UI_PANE_LOCAL, canonical.text, 0, 0, 0, 0);
        return;
    }

    UiRow *rows = rows_for(worker, UI_PANE_LOCAL, slot);
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
        handoff_listing_publish(worker->handoff, UI_PANE_LOCAL, slot, rows,
                                count);
        emit_listing_end(worker, UI_PANE_LOCAL, canonical.text, 0, 0, 0, 0);
        return;
    }

    FsEntry entry;
    while (worker->ports.fs->list_next(worker->ports.fs->ctx, dir, &entry) ==
           FS_OK) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;
        if (count >= WORKER_LIST_CAP) {
            truncated = 1;
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
        row->risk = (uint8_t)row_risk(canonical.text, entry.name);
    }
    worker->ports.fs->list_close(worker->ports.fs->ctx, dir);

    int start = count > 0 && rows[0].type == UI_ROW_UP ? 1 : 0;
    sort_rows(rows, count, start);

    handoff_listing_publish(worker->handoff, UI_PANE_LOCAL, slot, rows, count);

    FsFreeSpace space;
    memset(&space, 0, sizeof(space));
    int free_known = 0;
    uint64_t free_bytes = 0;
    if (worker->ports.fs->free_space(worker->ports.fs->ctx, canonical.text,
                                     &space) == FS_OK &&
        space.known) {
        free_bytes = space.bytes;
        free_known = 1;
    }

    emit_listing_end(worker, UI_PANE_LOCAL, canonical.text, 1, truncated,
                     free_bytes, free_known);
    worker->settings_dirty = 1;
    fmt_copy(worker->settings.last_local, sizeof(worker->settings.last_local), canonical.text);
}

/* --- remote listing ---------------------------------------------------- */

static void remote_listing_begin(Worker *worker, const char *path)
{
    int slot = handoff_listing_begin(worker->handoff, UI_PANE_REMOTE);
    if (slot < 0) {
        emit_listing_end(worker, UI_PANE_REMOTE, path, 0, 0, 0, 0);
        return;
    }
    worker->collecting = 1;
    worker->collect_pane = UI_PANE_REMOTE;
    worker->collect_slot = slot;
    worker->collect_count = 0;
    worker->collect_truncated = 0;
    fmt_copy(worker->remote_path, sizeof(worker->remote_path), path != NULL ? path : "/");
    emit_listing_begin(worker, UI_PANE_REMOTE, worker->remote_path);
}

static void remote_listing_row(Worker *worker, const ListingEntry *entry)
{
    if (!worker->collecting || entry == NULL)
        return;
    if (worker->collect_count >= WORKER_LIST_CAP) {
        worker->collect_truncated = 1;
        return;
    }
    UiRow *rows = rows_for(worker, UI_PANE_REMOTE, worker->collect_slot);
    UiRow *row = &rows[worker->collect_count++];
    memset(row, 0, sizeof(*row));
    fmt_copy(row->name, sizeof(row->name), entry->name);
    row->type = entry->type == LISTING_DIR
                    ? UI_ROW_DIR
                    : (entry->type == LISTING_FILE ? UI_ROW_FILE : UI_ROW_FILE);
    row->size = entry->size;
    row->size_known = entry->size_known;
}

static void remote_listing_end(Worker *worker, int ok)
{
    if (!worker->collecting)
        return;

    UiRow *rows = rows_for(worker, UI_PANE_REMOTE, worker->collect_slot);
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
    handoff_listing_publish(worker->handoff, UI_PANE_REMOTE,
                            worker->collect_slot, rows, count);
    emit_listing_end(worker, UI_PANE_REMOTE, worker->remote_path, ok,
                     worker->collect_truncated, 0, 0);
    worker->collecting = 0;
    worker->collect_count = 0;
}

static FtpClientResult remote_list(Worker *worker)
{
    if (worker->client == NULL ||
        ftp_client_state(worker->client) != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    return ftp_client_list(worker->client,
                           ftp_client_cwd_text(worker->client));
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
    if (result == STORE_OK)
        worker->settings_dirty = 0;
}

/* --- confirmations ----------------------------------------------------- */

static void request_local_conflict(Worker *worker, const UiCommand *command,
                                   const FsOpPlan *plan)
{
    HandoffConfirm request;
    memset(&request, 0, sizeof(request));
    request.kind = UI_CONFIRM_CONFLICT;
    request.valid = 1;
    fmt_copy(request.context, sizeof(request.context), command->action == UI_ACTION_MOVE ? "MOVE" : "COPY");
    request.item_count = plan->existing_count;
    request.existing_count = plan->existing_count;
    if (request.item_count > WORKER_CONFIRM_ITEMS)
        request.item_count = WORKER_CONFIRM_ITEMS;
    for (int i = 0; i < request.item_count; ++i) {
        fmt_copy(request.items[i].name, sizeof(request.items[i].name), i < plan->step_count ? fsop_plan_step_dest(plan, i) : "-");
        request.items[i].size_known = 0;
    }
    handoff_confirm_request(worker->handoff, &request);

    worker->pending.valid = 1;
    worker->pending.kind = 2;
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
    handoff_confirm_request(worker->handoff, &request);

    worker->pending.valid = 1;
    worker->pending.kind = 1;
    worker->pending.item = *item;
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

static void run_plan(Worker *worker, const UiCommand *command,
                     const FsOpPlan *plan)
{
    XferDirection direction = XFER_DIR_LOCAL_COPY;
    if (command->action == UI_ACTION_MOVE)
        direction = XFER_DIR_LOCAL_MOVE;
    else if (command->action == UI_ACTION_DELETE)
        direction = XFER_DIR_LOCAL_MOVE;

    HandoffEvent started;
    event_init(&started, HANDOFF_EVT_TRANSFER_STARTED);
    fmt_copy(started.detail, sizeof(started.detail), last_name(command->path));
    started.item_index = 1;
    started.item_total = 1;
    started.total = plan->total_bytes;
    started.total_known = plan->total_known;
    fmt_copy(started.path, sizeof(started.path), command->path);
    started.op[0] = '\0';
    started.direction = direction;
    emit(worker, &started);

    FsOpProgress progress;
    FsOpRunResult result = fsop_run(worker->ports.fs, plan, &progress, NULL,
                                    NULL);

    if (result == FSOP_RUN_OK) {
        log_event(worker, LOGEV_TRANSFER_DONE, NULL, NULL, command->path,
                  "done", "app", progress.bytes_done, progress.bytes_total);
    } else {
        emit_error(worker, UI_PANE_LOCAL, progress.message,
                   progress.cause, progress.stage);
        log_event(worker, LOGEV_TRANSFER_FAIL, NULL, NULL, command->path,
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
    done.item_index = 1;
    done.item_total = 1;
    fmt_copy(done.path, sizeof(done.path), command->path);
    emit(worker, &done);

    worker_list_local(worker, worker->local_path);
}

static int same_folder_path(const char *a, const char *b)
{
    Path pa;
    Path pb;
    if (path_parent(a, &pa) != PATH_OK || path_parent(b, &pb) != PATH_OK)
        return 0;
    return strcmp(pa.text, pb.text) == 0;
}

static void add_copy_candidates(Worker *worker, const char *source,
                                const char *destination, int source_is_dir)
{
    const char *name = last_name(source);
    const char *dot = NULL;
    if (!source_is_dir) {
        for (const char *p = name; *p != '\0'; ++p) {
            if (*p == '.' && p != name)
                dot = p;
        }
    }
    size_t stem_len = dot != NULL ? (size_t)(dot - name) : strlen(name);
    const char *suffix = dot != NULL ? dot : "";

    for (int n = 0; n < 100; ++n) {
        char candidate[PATH_NAME_MAX + 16];
        if (n == 0)
            snprintf(candidate, sizeof(candidate), "%.*s copy%s",
                     (int)stem_len, name, suffix);
        else
            snprintf(candidate, sizeof(candidate), "%.*s copy %d%s",
                     (int)stem_len, name, n + 1, suffix);

        Path target;
        if (path_join(destination, candidate, &target) != PATH_OK)
            return;
        fact_stat(worker, target.text);
        if (worker->facts_truncated)
            return;
    }
}

static void request_high_risk(Worker *worker, const UiCommand *command,
                              const FsOpPlan *plan, FsOpConflict conflict)
{
    HandoffConfirm request;
    memset(&request, 0, sizeof(request));
    request.kind = UI_CONFIRM_HIGH_RISK;
    request.valid = 1;
    request.high_risk = 1;
    fmt_copy(request.risk_path, sizeof(request.risk_path), plan->risk_path.text);
    handoff_confirm_request(worker->handoff, &request);

    worker->pending.valid = 1;
    worker->pending.kind = 2;
    worker->pending.command = *command;
    worker->pending.conflict = conflict;
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
        emit_error(worker, UI_PANE_LOCAL, "ACTION QUEUE FULL", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }
    worker->pending_commands[worker->pending_command_count++] = *command;
}

static void execute_action(Worker *worker, const UiCommand *command,
                           FsOpConflict conflict, int approved)
{
    const char *source = command->path;
    const char *destination = command->path2;

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
        emit_error(worker, UI_PANE_LOCAL, "ACTION NOT SUPPORTED",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return;
    }

    facts_reset(worker);
    fact_tree(worker, source, 0);
    if (worker->fact_count == 0 || worker->facts_truncated) {
        emit_error(worker, UI_PANE_LOCAL,
                   worker->facts_truncated ? "PLAN TOO LARGE"
                                           : "SOURCE NOT FOUND",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return;
    }

    FsEntry source_entry;
    int source_is_dir = 0;
    if (worker->ports.fs->stat(worker->ports.fs->ctx, source,
                               &source_entry) == FS_OK)
        source_is_dir = source_entry.type == FS_ENTRY_DIR;

    if ((kind == FSOP_COPY || kind == FSOP_MOVE) && destination != NULL &&
        destination[0] != '\0') {
        if (!build_dest_facts(worker, source, destination, source_is_dir)) {
            emit_error(worker, UI_PANE_LOCAL, "PLAN TOO LARGE",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
            return;
        }
        if (kind == FSOP_COPY &&
            same_folder_path(source, destination))
            add_copy_candidates(worker, source, destination, source_is_dir);
        if (worker->facts_truncated) {
            emit_error(worker, UI_PANE_LOCAL, "PLAN TOO LARGE",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
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
        emit_error(worker, UI_PANE_LOCAL, "OUT OF MEMORY", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }

    int built = fsop_plan_build(plan, &request);
    if (!built || !plan->ok) {
        emit_error(worker, UI_PANE_LOCAL,
                   plan->reason[0] != '\0' ? plan->reason : "PLAN REFUSED",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        free(plan);
        return;
    }

    if (plan->high_risk && !approved) {
        request_high_risk(worker, command, plan, conflict);
        free(plan);
        return;
    }

    if (plan->needs_conflict) {
        request_local_conflict(worker, command, plan);
        free(plan);
        return;
    }

    run_plan(worker, command, plan);
    free(plan);
}

static void handle_action(Worker *worker, const UiCommand *command)
{
    if (command->action == UI_ACTION_UPLOAD ||
        command->action == UI_ACTION_DOWNLOAD) {
        UiCommand transfer = *command;
        transfer.type = UI_CMD_TRANSFER;
        transfer.pane = command->action == UI_ACTION_UPLOAD ? UI_PANE_LOCAL
                                                            : UI_PANE_REMOTE;
        handle_transfer(worker, &transfer);
        return;
    }

    if (command->action != UI_ACTION_COPY &&
        command->action != UI_ACTION_MOVE &&
        command->action != UI_ACTION_DELETE &&
        command->action != UI_ACTION_NEW_FOLDER &&
        command->action != UI_ACTION_RENAME) {
        emit_error(worker, UI_PANE_LOCAL, "ACTION NOT SUPPORTED",
                   OP_CAUSE_APP, OP_STAGE_PLAN);
        return;
    }

    if (worker->transfer_active || xfer_busy(&worker->queue) ||
        ftp_server_busy(worker->server)) {
        emit_error(worker, UI_PANE_LOCAL, "TRANSFER IN PROGRESS",
                   OP_CAUSE_APP, OP_STAGE_TRANSFER);
        return;
    }

    if (worker->pending.valid) {
        queue_pending_action(worker, command);
        return;
    }

    if (command->action == UI_ACTION_NEW_FOLDER ||
        command->action == UI_ACTION_RENAME) {
        if (command->text[0] == '\0') {
            emit_error(worker, UI_PANE_LOCAL, "NAME REQUIRED", OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
        Path target;
        if (command->action == UI_ACTION_NEW_FOLDER) {
            const char *dir = command->path2[0] != '\0' ? command->path2
                                                         : command->path;
            if (path_join(dir, command->text, &target) != PATH_OK) {
                emit_error(worker, UI_PANE_LOCAL, "PATH TOO LONG",
                           OP_CAUSE_APP, OP_STAGE_PLAN);
                return;
            }
        } else {
            Path parent;
            if (path_parent(command->path, &parent) != PATH_OK ||
                path_join(parent.text, command->text, &target) != PATH_OK) {
                emit_error(worker, UI_PANE_LOCAL, "PATH TOO LONG",
                           OP_CAUSE_APP, OP_STAGE_PLAN);
                return;
            }
        }
        UiCommand adjusted = *command;
        fmt_copy(adjusted.path2, sizeof(adjusted.path2), target.text);
        execute_action(worker, &adjusted, FSOP_CONFLICT_ASK,
                       command->value == 1);
        return;
    }

    execute_action(worker, command,
                   command->value == 1 ? FSOP_CONFLICT_OVERWRITE
                                       : FSOP_CONFLICT_ASK,
                   command->value == 1);
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

    worker->connecting_site = *site;
    worker->connecting_valid = 1;
    worker->last_site_index = command->index;
    worker->pending_list_after_cwd = 1;

    FtpClientLogin login;
    memset(&login, 0, sizeof(login));
    login.host = worker->connecting_site.host;
    login.port = worker->connecting_site.port;
    login.anonymous = worker->connecting_site.anonymous;
    login.username = worker->connecting_site.username;
    login.password = worker->connecting_site.password;

    FtpClientResult result = ftp_client_connect(worker->client, &login);
    if (result != FTP_CLIENT_OK) {
        worker->pending_list_after_cwd = 0;
        emit_error(worker, UI_PANE_REMOTE, "CONNECT REFUSED", OP_CAUSE_APP,
                   OP_STAGE_CONNECT);
    }
}

static void handle_transfer(Worker *worker, const UiCommand *command)
{
    if (command->pane == UI_PANE_LOCAL && !worker->connected) {
        emit_error(worker, UI_PANE_LOCAL, "NOT CONNECTED", OP_CAUSE_APP,
                   OP_STAGE_CONNECT);
        return;
    }
    if (command->pane == UI_PANE_REMOTE && !worker->connected) {
        emit_error(worker, UI_PANE_REMOTE, "NOT CONNECTED", OP_CAUSE_APP,
                   OP_STAGE_CONNECT);
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

static void batch_finish(Worker *worker)
{
    if (worker->batch_total > 0 && worker->batch_done >= worker->batch_total) {
        worker->batch_total = 0;
        worker->batch_done = 0;
        worker->batch_conflict_decided = 0;
        worker->batch_conflict = XFER_CONFLICT_NONE;
    }
}

static void start_transfer_item(Worker *worker, const XferItem *item)
{
    FtpClientResult result;
    worker->transfer_start_ms = now_ms(worker);
    worker->transfer_last_bytes = 0;
    worker->transfer_cancelled = 0;

    if (item->direction == XFER_DIR_DOWNLOAD) {
        FtpClientTransfer transfer;
        transfer.remote_path = item->source.text;
        transfer.local_path = item->destination.text;
        transfer.risk_approved = 0;
        result = ftp_client_download(worker->client, &transfer, NULL);
    } else {
        FtpClientTransfer transfer;
        transfer.remote_path = item->destination.text;
        transfer.local_path = item->source.text;
        transfer.risk_approved = 0;
        result = ftp_client_upload(worker->client, &transfer);
    }

    if (result != FTP_CLIENT_OK) {
        XferItem next;
        memset(&next, 0, sizeof(next));
        xfer_release(&worker->queue, XFER_ITEM_FAILED, OP_CAUSE_APP,
                     OP_STAGE_TRANSFER, &next);
        emit_error(worker, UI_PANE_REMOTE, "TRANSFER REFUSED", OP_CAUSE_APP,
                   OP_STAGE_TRANSFER);
        worker->batch_done++;
        batch_finish(worker);
        return;
    }

    worker->transfer_active = 1;
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

static void pump_queue(Worker *worker)
{
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

    if (local_destination_exists(worker, &item) &&
        !worker->batch_conflict_decided) {
        request_transfer_conflict(worker, &item);
        return;
    }

    if (local_destination_exists(worker, &item) &&
        worker->batch_conflict == XFER_CONFLICT_SKIP_EXISTING) {
        xfer_release(&worker->queue, XFER_ITEM_DONE, OP_CAUSE_APP,
                     OP_STAGE_TRANSFER, NULL);
        worker->batch_done++;
        batch_finish(worker);
        pump_queue(worker);
        return;
    }

    start_transfer_item(worker, &item);
}

static void enqueue_transfer(Worker *worker, const UiCommand *command)
{
    XferItem item;
    memset(&item, 0, sizeof(item));

    if (command->pane == UI_PANE_LOCAL) {
        item.direction = XFER_DIR_UPLOAD;
        Path source;
        if (path_canonicalize(command->path, &source) != PATH_OK) {
            emit_error(worker, UI_PANE_LOCAL, "INVALID SOURCE", OP_CAUSE_APP,
                       OP_STAGE_PLAN);
            return;
        }
        fmt_copy(item.source.text, sizeof(item.source.text), source.text);
        char remote[PATH_MAX_LEN];
        if (!remote_join(command->path2, last_name(source.text), remote,
                         sizeof(remote))) {
            emit_error(worker, UI_PANE_LOCAL, "REMOTE PATH TOO LONG",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
            return;
        }
        fmt_copy(item.destination.text, sizeof(item.destination.text), remote);
        FsEntry entry;
        if (worker->ports.fs->stat(worker->ports.fs->ctx, source.text,
                                   &entry) == FS_OK) {
            item.total = entry.size;
            item.total_known = entry.size_known;
        }
    } else {
        item.direction = XFER_DIR_DOWNLOAD;
        fmt_copy(item.source.text, sizeof(item.source.text), command->path);
        Path dest;
        if (path_join(command->path2, last_name(command->path), &dest) !=
            PATH_OK) {
            emit_error(worker, UI_PANE_REMOTE, "LOCAL PATH TOO LONG",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
            return;
        }
        fmt_copy(item.destination.text, sizeof(item.destination.text),
                 dest.text);
    }

    if (!xfer_enqueue(&worker->queue, &item)) {
        emit_error(worker, UI_PANE_REMOTE, "QUEUE FULL", OP_CAUSE_APP,
                   OP_STAGE_PLAN);
        return;
    }
    worker->batch_total++;
    pump_queue(worker);
}

static void client_listing_end(Worker *worker, const FtpClientEvent *event)
{
    if (!worker->collecting)
        return;
    remote_listing_end(worker, event->ok);
}

static void client_operation_done(Worker *worker, const FtpClientEvent *event)
{
    if (event->operation == FTP_CLIENT_OP_CWD) {
        if (event->ok) {
            fmt_copy(worker->remote_path, sizeof(worker->remote_path),
                     ftp_client_cwd_text(worker->client));
            FtpClientResult result = remote_list(worker);
            if (result != FTP_CLIENT_OK)
                emit_error(worker, UI_PANE_REMOTE, "LIST FAILED",
                           OP_CAUSE_APP, OP_STAGE_FIRST_LISTING);
        } else {
            emit_error(worker, UI_PANE_REMOTE,
                       event->detail != NULL ? event->detail : "CWD FAILED",
                       event->cause, event->stage);
        }
        return;
    }

    if (event->operation == FTP_CLIENT_OP_MKDIR ||
        event->operation == FTP_CLIENT_OP_RENAME ||
        event->operation == FTP_CLIENT_OP_DELETE ||
        event->operation == FTP_CLIENT_OP_RMDIR) {
        if (event->ok)
            (void)remote_list(worker);
        else
            emit_error(worker, UI_PANE_REMOTE,
                       event->detail != NULL ? event->detail : "REMOTE FAILED",
                       event->cause, event->stage);
        return;
    }

    if (event->operation != FTP_CLIENT_OP_DOWNLOAD &&
        event->operation != FTP_CLIENT_OP_UPLOAD)
        return;

    if (!worker->transfer_active)
        return;

    int ok = event->ok;
    int cancelled = worker->transfer_cancelled;
    XferItem finished;
    memset(&finished, 0, sizeof(finished));
    (void)xfer_active(&worker->queue, &finished);

    XferItemState outcome = XFER_ITEM_DONE;
    if (cancelled)
        outcome = XFER_ITEM_CANCELLED;
    else if (!ok)
        outcome = XFER_ITEM_FAILED;

    xfer_release(&worker->queue, outcome, event->cause, event->stage, NULL);
    worker->transfer_active = 0;
    worker->transfer_cancelled = 0;
    worker->batch_done++;

    if (cancelled) {
        HandoffEvent cancelled_event;
        event_init(&cancelled_event, HANDOFF_EVT_TRANSFER_CANCELLED);
        emit(worker, &cancelled_event);
    } else if (ok) {
        emit_transfer_done(worker, finished.direction, finished.source.text, 1,
                           event->bytes, event->total, event->total_known,
                           OP_CAUSE_APP, OP_STAGE_TRANSFER,
                           worker->transfer_start_ms);
        log_event(worker, LOGEV_TRANSFER_DONE,
                  worker->connecting_valid ? worker->connecting_site.name : NULL,
                  worker->connecting_valid ? worker->connecting_site.host : NULL,
                  finished.source.text, "done", "app", event->bytes,
                  event->total);
        if (finished.direction == XFER_DIR_DOWNLOAD) {
            Path parent;
            if (path_parent(finished.destination.text, &parent) == PATH_OK)
                worker_list_local(worker, parent.text);
        } else {
            (void)remote_list(worker);
        }
    } else {
        emit_transfer_done(worker, finished.direction, finished.source.text, 0,
                           event->bytes, event->total, event->total_known,
                           event->cause, event->stage,
                           worker->transfer_start_ms);
        emit_error(worker, UI_PANE_REMOTE,
                   event->detail != NULL ? event->detail : "TRANSFER FAILED",
                   event->cause, event->stage);
        log_event(worker, LOGEV_TRANSFER_FAIL,
                  worker->connecting_valid ? worker->connecting_site.name : NULL,
                  worker->connecting_valid ? worker->connecting_site.host : NULL,
                  finished.source.text, op_stage_name(event->stage),
                  op_cause_name(event->cause), event->bytes, event->total);
    }

    batch_finish(worker);
    pump_queue(worker);
}

static void client_event(void *ctx, const FtpClientEvent *event)
{
    Worker *worker = ctx;
    if (worker == NULL || event == NULL)
        return;

    switch (event->kind) {
    case FTP_CLIENT_EVENT_STATE:
        if (event->state == FTP_CLIENT_READY) {
            worker->connected = 1;
            emit_session(worker, 1, NULL);
            if (worker->connecting_valid)
                log_event(worker, LOGEV_CONNECT,
                          worker->connecting_site.name,
                          worker->connecting_site.host, NULL, "ready", "app",
                          0, 0);
            if (worker->pending_list_after_cwd) {
                worker->pending_list_after_cwd = 0;
                worker->list_after_ready = 1;
            }
        } else if (event->state == FTP_CLIENT_LOST ||
                   event->state == FTP_CLIENT_FAILED) {
            worker->connected = 0;
            emit_session(worker, 0,
                         event->detail != NULL ? event->detail
                                               : "CONTROL CONNECTION LOST");
            if (worker->transfer_active) {
                XferItem item;
                memset(&item, 0, sizeof(item));
                (void)xfer_active(&worker->queue, &item);
                xfer_release(&worker->queue, XFER_ITEM_FAILED,
                             event->cause, event->stage, NULL);
                worker->transfer_active = 0;
                emit_transfer_done(worker, item.direction, item.source.text, 0,
                                   event->bytes, event->total,
                                   event->total_known, event->cause,
                                   event->stage, worker->transfer_start_ms);
            }
            xfer_init(&worker->queue);
            worker->batch_total = 0;
            worker->batch_done = 0;
            worker->batch_conflict_decided = 0;
            worker->batch_conflict = XFER_CONFLICT_NONE;
        }
        break;
    case FTP_CLIENT_EVENT_LISTING_BEGIN:
        remote_listing_begin(worker, event->path);
        break;
    case FTP_CLIENT_EVENT_LISTING_ROW:
        remote_listing_row(worker, event->row);
        break;
    case FTP_CLIENT_EVENT_LISTING_END:
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
        if (event->clients > 0) {
            NetAddress address = ftp_server_peer(worker->server);
            if (address.addr != 0)
                format_peer(address, peer, sizeof(peer));
        }
        fmt_copy(client.detail, sizeof(client.detail), peer);
        fmt_copy(client.op, sizeof(client.op),
                 server_op_name(ftp_server_last_command(worker->server)));
        fmt_copy(client.path, sizeof(client.path),
                 event->path != NULL ? event->path : "");
        client.bytes = event->bytes;
        client.total = event->total;
        client.total_known = event->total_known;
        emit(worker, &client);
        break;
    }
    case FTP_SERVER_EVENT_PROGRESS: {
        if (!worker->server_transfer_active) {
            worker->server_transfer_active = 1;
            worker->server_transfer_start_ms = now_ms(worker);
            const char *command = ftp_server_last_command(worker->server);
            worker->server_direction = strcmp(command, "STOR") == 0
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
                           worker->server_name, event->ok, event->bytes,
                           event->total, event->total_known, event->cause,
                           event->stage, worker->server_transfer_start_ms);
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

static void cancel_current_transfer(Worker *worker)
{
    if (worker->transfer_active) {
        worker->transfer_cancelled = 1;
        if (ftp_client_cancel(worker->client) != FTP_CLIENT_OK)
            emit_error(worker, UI_PANE_REMOTE, "CANCEL UNAVAILABLE",
                       OP_CAUSE_APP, OP_STAGE_TRANSFER);
        return;
    }
    if (xfer_busy(&worker->queue)) {
        (void)xfer_cancel_active(&worker->queue, OP_CAUSE_USER,
                                 OP_STAGE_TRANSFER, NULL);
        worker->batch_done++;
        batch_finish(worker);
        HandoffEvent cancelled;
        event_init(&cancelled, HANDOFF_EVT_TRANSFER_CANCELLED);
        emit(worker, &cancelled);
        pump_queue(worker);
    }
}

static void handle_command(Worker *worker, const UiCommand *command)
{
    switch (command->type) {
    case UI_CMD_OPEN_PATH:
        if (command->pane == UI_PANE_LOCAL) {
            worker_list_local(worker, command->path);
        } else if (worker->connected) {
            if (ftp_client_cwd(worker->client, command->path) != FTP_CLIENT_OK)
                emit_error(worker, UI_PANE_REMOTE, "NOT READY", OP_CAUSE_APP,
                           OP_STAGE_COMMAND);
        }
        break;
    case UI_CMD_GO_PARENT:
        if (command->pane == UI_PANE_LOCAL) {
            Path parent;
            if (path_parent(worker->local_path, &parent) == PATH_OK)
                worker_list_local(worker, parent.text);
            else
                worker_list_local(worker, worker->local_path);
        } else if (worker->connected) {
            char parent[PATH_MAX_LEN];
            const char *cwd = ftp_client_cwd_text(worker->client);
            if (remote_parent(cwd, parent, sizeof(parent)))
                (void)ftp_client_cwd(worker->client, parent);
        }
        break;
    case UI_CMD_REFRESH:
        if (command->pane == UI_PANE_LOCAL)
            worker_list_local(worker, worker->local_path);
        else if (worker->connected)
            (void)remote_list(worker);
        break;
    case UI_CMD_CONNECT_SITE:
        handle_connect(worker, command);
        break;
    case UI_CMD_RECONNECT:
        if (worker->last_site_index >= 0) {
            UiCommand reconnect = *command;
            reconnect.index = worker->last_site_index;
            handle_connect(worker, &reconnect);
        }
        break;
    case UI_CMD_TRANSFER:
        handle_transfer(worker, command);
        break;
    case UI_CMD_CANCEL_TRANSFER:
        cancel_current_transfer(worker);
        break;
    case UI_CMD_CLEAR_QUEUE:
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
        if (worker->pending.valid) {
            PendingWork pending = worker->pending;
            worker->pending.valid = 0;
            if (pending.kind == 1) {
                if (command->choice == UI_CHOICE_CANCEL)
                    cancel_current_transfer(worker);
                else if (command->choice == UI_CHOICE_OVERWRITE) {
                    worker->batch_conflict_decided = 1;
                    worker->batch_conflict = XFER_CONFLICT_OVERWRITE_ALL;
                    start_transfer_item(worker, &pending.item);
                } else if (command->choice == UI_CHOICE_SKIP) {
                    worker->batch_conflict_decided = 1;
                    worker->batch_conflict = XFER_CONFLICT_SKIP_EXISTING;
                    xfer_release(&worker->queue, XFER_ITEM_DONE,
                                 OP_CAUSE_APP, OP_STAGE_TRANSFER, NULL);
                    worker->batch_done++;
                    batch_finish(worker);
                    pump_queue(worker);
                }
            } else if (pending.kind == 2) {
                if (command->confirm == UI_CONFIRM_HIGH_RISK) {
                    if (command->choice == UI_CHOICE_CONTINUE)
                        execute_action(worker, &pending.command,
                                       pending.conflict, 1);
                } else if (command->choice == UI_CHOICE_OVERWRITE) {
                    execute_action(worker, &pending.command,
                                   FSOP_CONFLICT_OVERWRITE, 0);
                } else if (command->choice == UI_CHOICE_SKIP) {
                    execute_action(worker, &pending.command,
                                   FSOP_CONFLICT_SKIP, 0);
                }
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
        } else {
            emit_server_state(worker, UI_SERVER_STARTING, 0);
            log_event(worker, LOGEV_SERVER_START, NULL, NULL, NULL, "start",
                      "app", 0, 0);
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
        if (command->value < 1024 || command->value > 65534) {
            emit_error(worker, UI_PANE_REMOTE, "PORT MUST BE 1024-65534",
                       OP_CAUSE_APP, OP_STAGE_PLAN);
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
    worker->last_site_index = -1;
    worker->rows = calloc((size_t)UI_PANE_COUNT * HANDOFF_LIST_GENS *
                              WORKER_LIST_CAP,
                          sizeof(UiRow));
    if (worker->rows == NULL) {
        free(worker);
        return NULL;
    }

    worker->client = ftp_client_create(ports->net, ports->fs, ports->clock,
                                       client_event, worker);
    worker->server = ftp_server_create(ports->net, ports->fs, ports->clock,
                                       server_event, worker);
    if (worker->client == NULL || worker->server == NULL) {
        ftp_client_destroy(worker->client);
        ftp_server_destroy(worker->server);
        free(worker->rows);
        free(worker);
        return NULL;
    }

    xfer_init(&worker->queue);
    fmt_copy(worker->store_dir, sizeof(worker->store_dir), ports->store_dir != NULL ? ports->store_dir : STORE_DIR);
    return worker;
}

void worker_destroy(Worker *worker)
{
    if (worker == NULL)
        return;
    ftp_client_destroy(worker->client);
    ftp_server_destroy(worker->server);
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
            fmt_copy(worker->local_path, sizeof(worker->local_path), path);
            return;
        }
    }

    const char *volumes[3] = {"ux0:", "ur0:", "uma0:"};
    for (int i = 0; i < 3; ++i) {
        if (worker->ports.fs->stat(worker->ports.fs->ctx, volumes[i],
                                   &entry) == FS_OK &&
            entry.type == FS_ENTRY_DIR) {
            fmt_copy(worker->local_path, sizeof(worker->local_path), volumes[i]);
            return;
        }
    }
    snprintf(worker->local_path, sizeof(worker->local_path), "ux0:data");
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
    if (worker->settings.server_port < 1024 ||
        worker->settings.server_port > 65534)
        worker->settings.server_port = 1337;

    pick_local_path(worker);
    snprintf(worker->settings.last_local,
             sizeof(worker->settings.last_local), "%s", worker->local_path);

    worker->ready = 1;
    worker->initial_list = 1;
    worker->site_dirty = 1;
    worker->settings_dirty = 1;
}

void worker_step(Worker *worker)
{
    if (worker == NULL || !worker->ready)
        return;

    for (int i = 0; i < WORKER_CMD_BUDGET; ++i) {
        UiCommand command;
        if (!handoff_command_pop(worker->handoff, &command))
            break;
        handle_command(worker, &command);
    }

    if (__atomic_load_n(&worker->done, __ATOMIC_ACQUIRE))
        return;

    if (worker->initial_list) {
        worker->initial_list = 0;
        worker_list_local(worker, worker->local_path);
    }

    ftp_server_set_external_busy(
        worker->server, worker->transfer_active || xfer_busy(&worker->queue) ||
                            worker->pending.valid);
    ftp_client_tick(worker->client);
    ftp_server_tick(worker->server);
    if (worker->list_after_ready && worker->connected &&
        !ftp_client_busy(worker->client)) {
        worker->list_after_ready = 0;
        (void)remote_list(worker);
    }
    pump_queue(worker);
    drain_pending_actions(worker);
    publish_site_book(worker);
    save_settings(worker);
}

int worker_ready(const Worker *worker)
{
    return worker != NULL && worker->ready;
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
