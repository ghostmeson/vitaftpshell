/* SPDX-License-Identifier: GPL-3.0-only */

#include "app/app.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/fmt.h"
#include "core/input.h"
#include "gfx/draw.h"
#include "ui/render.h"

#define APP_MARK_CAP 64
#define APP_EVENT_BUDGET 64
#define APP_COMMAND_MAX 64

struct App {
    AppPorts ports;
    Handoff handoff;
    Worker *worker;
    UiState state;
    UiMark local_marks[APP_MARK_CAP];
    UiMark remote_marks[APP_MARK_CAP];
    InputRepeat repeat;

    int worker_thread;
    int thread_started;
    pthread_t thread;
    int stop;

    int ime_active;

    int listing_held[UI_PANE_COUNT];
    int listing_truncated[UI_PANE_COUNT];
    uint32_t listing_generation[UI_PANE_COUNT];

    HandoffConfirm confirm;
    UiConfirmItem confirm_items[HANDOFF_CONFIRM_ITEMS];
};

static void feed(App *app, const UiStateEvent *event);

static void *worker_main(void *arg)
{
    App *app = arg;
    while (!__atomic_load_n(&app->stop, __ATOMIC_ACQUIRE)) {
        worker_step(app->worker);
        app->ports.clock->sleep_ms(app->ports.clock->ctx, 8);
    }
    return NULL;
}

static void fill_sites(App *app)
{
    const SiteBook *book = worker_sites(app->worker);
    if (book == NULL)
        return;

    app->state.sites.count = (int)site_book_count(book);
    if (app->state.sites.count > UI_SITES_MAX)
        app->state.sites.count = UI_SITES_MAX;
    for (int i = 0; i < app->state.sites.count; ++i) {
        const Site *site = site_book_at(book, (unsigned)i);
        UiSiteRow *row = &app->state.sites.rows[i];
        memset(row, 0, sizeof(*row));
        snprintf(row->name, sizeof(row->name), "%s", site->name);
        snprintf(row->host, sizeof(row->host), "%s", site->host);
        row->port = site->port;
        row->anonymous = site->anonymous;
        row->password_set = site->password[0] != '\0';
        snprintf(row->username, sizeof(row->username), "%s", site->username);
    }
}

static void open_ime(App *app, const UiCommand *command)
{
    if (app->ime_active) {
        app->ports.ime->end(app->ports.ime->ctx);
        app->ime_active = 0;
    }

    ImeRequest request;
    request.title = command->title;
    request.initial = command->text;
    request.password = command->index;
    request.max_length = command->value > 0 ? (size_t)command->value : 255;

    ImeBeginResult result =
        app->ports.ime->begin(app->ports.ime->ctx, &request);
    if (result == IME_BEGIN_RUNNING) {
        app->ime_active = 1;
        return;
    }

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_IME_UNAVAILABLE;
    feed(app, &event);
}

static void app_command(App *app, const UiCommand *command)
{
    if (command->type == UI_CMD_NONE)
        return;
    if (command->type == UI_CMD_OPEN_IME) {
        open_ime(app, command);
        return;
    }
    if (!handoff_command_push(&app->handoff, command)) {
        UiStateEvent event;
        memset(&event, 0, sizeof(event));
        event.kind = UI_EVT_ERROR;
        event.pane = -1;
        snprintf(event.text, sizeof(event.text), "COMMAND QUEUE FULL");
        feed(app, &event);
    }
}

static void feed(App *app, const UiStateEvent *event)
{
    UiCommand commands[APP_COMMAND_MAX];
    int count = ui_state_handle(&app->state, event, commands, APP_COMMAND_MAX);
    for (int i = 0; i < count; ++i)
        app_command(app, &commands[i]);
}

static void drain_ime(App *app)
{
    if (!app->ime_active)
        return;

    ImeResponse response;
    memset(&response, 0, sizeof(response));
    ImePollResult result = app->ports.ime->poll(app->ports.ime->ctx, &response);
    if (result == IME_POLL_RUNNING)
        return;

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_IME_RESULT;
    event.ok = result == IME_POLL_DONE;
    if (event.ok)
        snprintf(event.text, sizeof(event.text), "%s", response.text);
    app->ports.ime->end(app->ports.ime->ctx);
    app->ime_active = 0;
    feed(app, &event);
}

static UiPane pane_for_direction(XferDirection direction)
{
    switch (direction) {
    case XFER_DIR_UPLOAD:
    case XFER_DIR_SERVER_UPLOAD:
    case XFER_DIR_LOCAL_COPY:
    case XFER_DIR_LOCAL_MOVE:
        return UI_PANE_LOCAL;
    default:
        return UI_PANE_REMOTE;
    }
}

static void drain_events(App *app)
{
    for (int i = 0; i < APP_EVENT_BUDGET; ++i) {
        HandoffEvent event;
        if (!handoff_event_pop(&app->handoff, &event))
            break;
        UiStateEvent ui;
        memset(&ui, 0, sizeof(ui));

        switch (event.kind) {
        case HANDOFF_EVT_PROGRESS:
            ui.kind = UI_EVT_TRANSFER_PROGRESS;
            ui.bytes = event.bytes;
            ui.total = event.total;
            ui.total_known = event.total_known;
            ui.rate = event.rate;
            ui.rate_known = event.rate_known;
            ui.elapsed_ms = event.elapsed_ms;
            ui.elapsed_known = event.elapsed_known;
            ui.item_index = event.item_index;
            ui.item_total = event.item_total;
            feed(app, &ui);
            break;
        case HANDOFF_EVT_TRANSFER_STARTED:
            ui.kind = UI_EVT_TRANSFER_STARTED;
            ui.pane = pane_for_direction(event.direction);
            ui.direction = (int)event.direction;
            ui.direction_known = 1;
            ui.total = event.total;
            ui.total_known = event.total_known;
            ui.item_index = event.item_index;
            ui.item_total = event.item_total;
            snprintf(ui.text, sizeof(ui.text), "%s", event.detail);
            feed(app, &ui);
            break;
        case HANDOFF_EVT_TRANSFER_DONE:
            ui.kind = event.ok ? UI_EVT_TRANSFER_DONE
                               : UI_EVT_TRANSFER_FAILED;
            ui.bytes = event.bytes;
            ui.total = event.total;
            ui.total_known = event.total_known;
            ui.rate = event.rate;
            ui.rate_known = event.rate_known;
            ui.elapsed_ms = event.elapsed_ms;
            ui.elapsed_known = event.elapsed_known;
            ui.item_index = event.item_index;
            ui.item_total = event.item_total;
            fmt_copy(ui.text, sizeof(ui.text),
                     event.detail[0] != '\0' ? event.detail : event.path);
            feed(app, &ui);
            break;
        case HANDOFF_EVT_TRANSFER_CANCELLED:
            ui.kind = UI_EVT_TRANSFER_CANCELLED;
            feed(app, &ui);
            break;
        case HANDOFF_EVT_SERVER_STATE:
            ui.kind = UI_EVT_SERVER_STATE;
            ui.value = event.value;
            ui.clients = event.clients;
            ui.port = event.port;
            ui.address_known = event.address_known;
            snprintf(ui.address, sizeof(ui.address), "%s", event.address);
            feed(app, &ui);
            break;
        case HANDOFF_EVT_SERVER_CLIENT:
            ui.kind = UI_EVT_SERVER_CLIENT;
            ui.ok = event.ok;
            ui.clients = event.clients;
            fmt_copy(ui.address, sizeof(ui.address), event.detail);
            fmt_copy(ui.op, sizeof(ui.op), event.op);
            fmt_copy(ui.path, sizeof(ui.path), event.path);
            ui.bytes = event.bytes;
            ui.total = event.total;
            ui.total_known = event.total_known;
            feed(app, &ui);
            break;
        case HANDOFF_EVT_SESSION:
            if (event.connected) {
                ui.kind = UI_EVT_SESSION_OK;
                ui.pane = UI_PANE_REMOTE;
                fmt_copy(ui.site, sizeof(ui.site), event.detail);
                fmt_copy(ui.host, sizeof(ui.host), event.path);
                ui.port = event.port;
                feed(app, &ui);
            } else {
                ui.kind = UI_EVT_SESSION_LOST;
                ui.pane = UI_PANE_REMOTE;
                fmt_copy(ui.text, sizeof(ui.text), event.detail);
                feed(app, &ui);

                UiStateEvent sites;
                memset(&sites, 0, sizeof(sites));
                sites.kind = UI_EVT_SITES;
                sites.ok = 0;
                sites.save_unavailable = app->state.sites.save_unavailable;
                snprintf(sites.text, sizeof(sites.text), "%s", event.detail);
                feed(app, &sites);
            }
            break;
        case HANDOFF_EVT_LISTING_BEGIN:
            ui.kind = UI_EVT_LISTING_BEGIN;
            ui.pane = event.pane;
            fmt_copy(ui.path, sizeof(ui.path), event.path);
            feed(app, &ui);
            break;
        case HANDOFF_EVT_LISTING_END:
            ui.kind = UI_EVT_LISTING_END;
            ui.pane = event.pane;
            ui.ok = event.ok;
            ui.truncated = event.truncated;
            if (event.pane < UI_PANE_COUNT)
                app->listing_truncated[event.pane] = event.truncated;
            snprintf(ui.text, sizeof(ui.text), "%s",
                     event.ok ? "" : "LISTING FAILED");
            feed(app, &ui);

            ui.kind = UI_EVT_PANE_INFO;
            ui.pane = event.pane;
            ui.bytes = event.free_bytes;
            ui.total_known = event.free_known;
            feed(app, &ui);
            break;
        case HANDOFF_EVT_SITE_BOOK: {
            if (event.detail[0] != '\0') {
                UiStateEvent saved;
                memset(&saved, 0, sizeof(saved));
                saved.kind = UI_EVT_SITE_SAVED;
                saved.ok = event.ok;
                snprintf(saved.text, sizeof(saved.text), "%s", event.detail);
                feed(app, &saved);
            }
            UiSiteSnapshot snapshot;
            size_t len = 0;
            uint32_t generation = 0;
            if (handoff_bulk_read(&app->handoff, &snapshot, sizeof(snapshot),
                                  &len, &generation) &&
                len == sizeof(snapshot)) {
                UiStateEvent sites;
                memset(&sites, 0, sizeof(sites));
                sites.kind = UI_EVT_SITES;
                sites.ok = 1;
                sites.site_rows = snapshot.rows;
                sites.site_count = snapshot.count;
                sites.save_unavailable = snapshot.save_unavailable;
                feed(app, &sites);
            }
            break;
        }
        case HANDOFF_EVT_EDIT_SITE:
            ui.kind = UI_EVT_EDIT_SITE;
            ui.edit_site = event.site;
            feed(app, &ui);
            break;
        case HANDOFF_EVT_ERROR:
            ui.kind = UI_EVT_ERROR;
            ui.pane = event.pane;
            snprintf(ui.text, sizeof(ui.text), "%s", event.detail);
            feed(app, &ui);
            break;
        default:
            break;
        }
    }

    HandoffConfirm confirm;
    if (handoff_confirm_take(&app->handoff, &confirm)) {
        app->confirm = confirm;
        for (int i = 0; i < confirm.item_count &&
                        i < HANDOFF_CONFIRM_ITEMS; ++i) {
            app->confirm_items[i].name = app->confirm.items[i].name;
            app->confirm_items[i].size = app->confirm.items[i].size;
            app->confirm_items[i].size_known =
                app->confirm.items[i].size_known;
        }
        UiStateEvent request;
        memset(&request, 0, sizeof(request));
        request.kind = UI_EVT_CONFIRM_REQUEST;
        request.confirm = confirm.kind;
        request.items = app->confirm_items;
        request.item_count = confirm.item_count;
        request.high_risk = confirm.high_risk;
        request.existing_count = confirm.existing_count;
        snprintf(request.risk_path, sizeof(request.risk_path), "%s",
                 confirm.risk_path);
        feed(app, &request);
    }
}

static void acquire_listings(App *app)
{
    for (int pane = 0; pane < UI_PANE_COUNT; ++pane) {
        const void *rows = NULL;
        int count = 0;
        uint32_t generation = 0;
        if (!handoff_listing_acquire(&app->handoff, (UiPane)pane, &rows,
                                     &count, &generation))
            continue;
        app->listing_held[pane] = 1;
        if (generation != app->listing_generation[pane]) {
            app->listing_generation[pane] = generation;
            ui_state_set_rows(&app->state, (UiPane)pane,
                              (const UiRow *)rows, count,
                              app->listing_truncated[pane]);
        }
    }
}

static void poll_input(App *app)
{
    if (app->ime_active)
        return;

    InputState input;
    memset(&input, 0, sizeof(input));
    app->ports.input->poll(app->ports.input->ctx, &input);

    UiInputEvent events[4];
    int count = input_repeat_poll(&app->repeat, &input,
                                  app->ports.clock->now_ms(
                                      app->ports.clock->ctx),
                                  events, 4);
    for (int i = 0; i < count; ++i) {
        UiStateEvent event;
        memset(&event, 0, sizeof(event));
        if (events[i].kind == UI_INPUT_KEY) {
            event.kind = UI_EVT_KEY;
            event.key = events[i].key;
            event.repeat = events[i].repeat;
        } else {
            event.kind = UI_EVT_STICK;
            event.stick_y = events[i].stick_y;
        }
        feed(app, &event);
    }
}

App *app_create(const AppPorts *ports, int worker_thread)
{
    if (ports == NULL || ports->net == NULL || ports->fs == NULL ||
        ports->input == NULL || ports->ime == NULL || ports->clock == NULL ||
        ports->log == NULL)
        return NULL;

    App *app = calloc(1, sizeof(*app));
    if (app == NULL)
        return NULL;

    app->ports = *ports;
    app->worker_thread = worker_thread;
    handoff_init(&app->handoff);

    WorkerPorts worker_ports;
    worker_ports.net = ports->net;
    worker_ports.fs = ports->fs;
    worker_ports.clock = ports->clock;
    worker_ports.log = ports->log;
    worker_ports.store_dir = NULL;

    app->worker = worker_create(&worker_ports, &app->handoff);
    if (app->worker == NULL) {
        free(app);
        return NULL;
    }
    worker_start(app->worker);

    ui_state_init(&app->state, app->local_marks, APP_MARK_CAP,
                  app->remote_marks, APP_MARK_CAP);
    input_repeat_init(&app->repeat);

    const Settings *settings = worker_settings(app->worker);
    if (settings != NULL) {
        if (settings->last_local[0] != '\0')
            snprintf(app->state.panes[UI_PANE_LOCAL].path,
                     sizeof(app->state.panes[UI_PANE_LOCAL].path), "%s",
                     settings->last_local);
        app->state.server.port = settings->server_port;
    }
    fill_sites(app);
    return app;
}

int app_start(App *app)
{
    if (app == NULL)
        return 0;
    if (app->worker_thread && !app->thread_started) {
        if (pthread_create(&app->thread, NULL, worker_main, app) == 0)
            app->thread_started = 1;
        else
            app->worker_thread = 0;
    }
    return 1;
}

int app_tick(App *app)
{
    if (app == NULL)
        return 0;

    if (!app->worker_thread)
        worker_step(app->worker);

    acquire_listings(app);
    drain_events(app);
    drain_ime(app);
    poll_input(app);

    draw_begin_frame();
    ui_render(&app->state);
    draw_present();

    for (int pane = 0; pane < UI_PANE_COUNT; ++pane) {
        if (app->listing_held[pane]) {
            handoff_listing_release(&app->handoff, (UiPane)pane);
            app->listing_held[pane] = 0;
        }
    }

    if (worker_quit(app->worker) || app->state.quit)
        return 0;
    return 1;
}

void app_destroy(App *app)
{
    if (app == NULL)
        return;
    if (app->thread_started) {
        __atomic_store_n(&app->stop, 1, __ATOMIC_RELEASE);
        pthread_join(app->thread, NULL);
        app->thread_started = 0;
    }
    worker_destroy(app->worker);
    free(app);
}

UiState *app_state(App *app)
{
    return app != NULL ? &app->state : NULL;
}

Handoff *app_handoff(App *app)
{
    return app != NULL ? &app->handoff : NULL;
}

Worker *app_worker(App *app)
{
    return app != NULL ? app->worker : NULL;
}
