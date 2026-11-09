/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_APP_APP_H
#define VFTP_APP_APP_H

#include "app/worker.h"
#include "core/handoff.h"
#include "core/uistate.h"
#include "platform/clock.h"
#include "platform/fs.h"
#include "platform/ime.h"
#include "platform/input.h"
#include "platform/log.h"
#include "platform/net.h"

/*
 * The frame loop and the one writer of UiState (outside the reducer). The app
 * polls input, decodes repeat, calls the reducer, pushes commands across the
 * handoff, drains events, and draws. It never opens a socket, touches a file,
 * or waits on the worker.
 *
 * `worker_thread` is 1 in the product. The host integration test passes 0 and
 * calls app_tick, which then drives worker_step inline; the same code paths
 * run either way.
 */

typedef struct {
    NetPort *net;
    FsPort *fs;
    InputPort *input;
    TextInputPort *ime;
    ClockPort *clock;
    LogPort *log;
} AppPorts;

typedef struct App App;

App *app_create(const AppPorts *ports, int worker_thread);
int app_start(App *app);
/* One frame. Returns 0 when the app should exit. */
int app_tick(App *app);
void app_destroy(App *app);

/* Test and composition-root inspection. */
UiState *app_state(App *app);
Handoff *app_handoff(App *app);
Worker *app_worker(App *app);

#endif
