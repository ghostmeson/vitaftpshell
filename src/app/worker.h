/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_APP_WORKER_H
#define VFTP_APP_WORKER_H

#include "core/ftp/client.h"
#include "core/ftp/server.h"
#include "core/handoff.h"
#include "core/settings.h"
#include "core/site.h"
#include "core/xfer.h"
#include "core/xfer_plan.h"
#include "platform/clock.h"
#include "platform/fs.h"
#include "platform/log.h"
#include "platform/net.h"

/* The worker's step cadence; it sleeps this long between worker_step calls. */
#define WORKER_TICK_MS 8
/* The recursive folder scan's fixed depth. The build gate reads this header,
 * so the constant and the check cannot drift (U8). */
#define WORKER_REMOTE_DEPTH 32

/*
 * The one worker (D38). It owns the FTP client, the FTP server, the transfer
 * queue, the local executor, the site book, the settings, and the log. It
 * reads commands from the handoff and publishes events and listing
 * generations. It never draws and never reads UiState.
 *
 * `worker_step` is public so the host integration test can drive the real
 * worker without a thread. The app's threaded mode calls the same function in
 * a loop.
 */

#define WORKER_LIST_CAP 2000
/* U2: the roots one walk may list. A folder transfer starts from one pane's
 * marked set, whose ceiling is the clipboard cap. */
#define WORKER_WALK_ROOTS UI_CLIP_MAX
/* KTD8: every view's listing arena must fit inside this named ceiling:
 * UI_PANE_COUNT * HANDOFF_LIST_GENS * WORKER_LIST_CAP * sizeof(UiRow). */
#define WORKER_LISTING_ARENA_BUDGET_BYTES (6u * 1024u * 1024u)
#define WORKER_FACTS_MAX 1024

typedef struct {
    NetPort *net;
    FsPort *fs;
    ClockPort *clock;
    LogPort *log;
    const char *store_dir; /* NULL means STORE_DIR */
} WorkerPorts;

typedef struct Worker Worker;

Worker *worker_create(const WorkerPorts *ports, Handoff *handoff);
void worker_destroy(Worker *worker);

/* Load the book, settings, and local path; seed the example site if empty. */
void worker_start(Worker *worker);

/* One bounded slice: commands, engine ticks, queue pump, published facts. */
void worker_step(Worker *worker);

int worker_quit(const Worker *worker);

/*
 * U2: a job's private remote walk (KTD3). The worker borrows the plan — the
 * caller creates, seeds, and frees it — and fills it from listings of its own:
 * no pane row, path, or generation is ever written. `roots` are the absolute
 * remote folders that the plan's first `root_count` root entries stand for, in
 * the same order, and both the array and the strings must stay valid until the
 * walk ends or is cancelled. A download plan is walked from its sources; an
 * upload plan needs the local side and is refused here until the job owns it
 * (U3).
 *
 * `begin` returns 1 when the walk owns the listing stream. It only prepares:
 * the first folder is listed from `worker_step` once the client is READY, so
 * a walk begun during a connect waits for it. A second begin refuses while one
 * is running. `done` is 1 only when the walk reached the end and
 * `xfer_plan_finalize` accepted the plan; a cancel or a lost session stops the
 * walk with the plan unfinished and `done` 0, for the caller to discard.
 * `worker_walk_plan` returns the plan last given to a begin, while the caller
 * still owns it. Nothing here publishes a pane row, a path, or a generation.
 */
int worker_walk_begin(Worker *worker, XferPlan *plan,
                      const char *const *roots, int root_count);
void worker_walk_cancel(Worker *worker);
int worker_walk_active(const Worker *worker);
int worker_walk_done(const Worker *worker);
XferPlan *worker_walk_plan(Worker *worker);
/* Folders the walk has listed to the end, for the dock's SCANNING line (R3). */
int worker_walk_folders(const Worker *worker);
/* The last refusal or stop reason, "" before one (for the job's terminal). */
const char *worker_walk_refusal(const Worker *worker);

/* Valid after worker_start and before the worker thread runs. The app copies
 * what it needs; nothing else reads these across threads. */
const Settings *worker_settings(const Worker *worker);
const SiteBook *worker_sites(const Worker *worker);

#endif
