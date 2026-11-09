/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_APP_WORKER_H
#define VFTP_APP_WORKER_H

#include "core/ftp/client.h"
#include "core/ftp/server.h"
#include "core/handoff.h"
#include "core/settings.h"
#include "core/site.h"
#include "core/xfer.h"
#include "platform/clock.h"
#include "platform/fs.h"
#include "platform/log.h"
#include "platform/net.h"

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

int worker_ready(const Worker *worker);
int worker_quit(const Worker *worker);

/* Valid after worker_start and before the worker thread runs. The app copies
 * what it needs; nothing else reads these across threads. */
const Settings *worker_settings(const Worker *worker);
const SiteBook *worker_sites(const Worker *worker);

#endif
