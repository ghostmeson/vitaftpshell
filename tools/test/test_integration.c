/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "core/handoff.h"
#include "fake_clock.h"
#include "fake_fs.h"
#include "fake_ime.h"
#include "fake_input.h"
#include "fake_log.h"
#include "fake_net.h"
#include "fake_peer.h"
#include "gfx/draw.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

#define PEER_ADDR 0xC0A8012Au
#define PEER_PORT 21

/* The shared scripted peer in tools/fake/fake_peer.c. The suite scripts the
 * answers before the client connects. */
static FakePeer *peer_new(void)
{
    FakePeer *peer = fake_peer_create();
    CHECK(peer != NULL);
    return peer;
}

static App *make_app_ex(const char *seed_path, const char *seed_text,
                        const char *seed_dir, const char *seed_path2,
                        const char *seed_text2)
{
    fake_net_reset();
    fake_fs_reset();
    fake_clock_reset();
    fake_input_reset();
    fake_ime_reset();
    fake_log_reset();

    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ur0:");
    fake_fs_add_dir("ur0:tai");
    if (seed_path != NULL && seed_text != NULL)
        fake_fs_seed_file(seed_path, seed_text, strlen(seed_text));
    if (seed_path2 != NULL && seed_text2 != NULL)
        fake_fs_seed_file(seed_path2, seed_text2, strlen(seed_text2));
    if (seed_dir != NULL)
        fake_fs_add_dir(seed_dir);

    AppPorts ports;
    ports.net = fake_net_port();
    ports.fs = fake_fs_port();
    ports.input = fake_input_port();
    ports.ime = fake_ime_port();
    ports.clock = fake_clock_port();
    ports.log = fake_log_port();

    App *app = app_create(&ports, 0);
    CHECK(app != NULL);
    if (app != NULL)
        CHECK(app_start(app) == 1);
    return app;
}

static App *make_app(void)
{
    return make_app_ex(NULL, NULL, NULL, NULL, NULL);
}

static void press(App *app, uint32_t button)
{
    fake_input_set(button, 0, 0, 0);
    (void)app_tick(app);
}

static void pump(App *app, FakePeer *peer, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        (void)app_tick(app);
        if (peer != NULL)
            fake_peer_serve(peer);
    }
}

static void connect_site_listing(App *app, FakePeer *peer, const char *listing,
                                 const char *payload)
{
    fake_net_set_resolve(PEER_ADDR, 0);
    fake_net_set_link(1, 1, PEER_ADDR, 0);

    press(app, INPUT_BIT(INPUT_SELECT));
    CHECK(app_state(app)->screen == UI_SCREEN_SITES);
    press(app, INPUT_BIT(INPUT_CROSS));
    (void)app_tick(app);

    fake_peer_set_listing(peer, listing);
    fake_peer_set_payload(peer, payload);
    fake_peer_open(peer);

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
        if (remote->connected && !remote->loading && remote->row_count >= 2)
            break;
    }
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].connected == 1);
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].row_count >= 2);

    /* Leave SITES so L/R and the workspace keys apply. */
    press(app, INPUT_BIT(INPUT_CIRCLE));
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
}

static void connect_site(App *app, FakePeer *peer)
{
    static const char *listing =
        "type=file;size=5;modify=20240101000000; TEST.BIN\r\n"
        "type=file;size=100;modify=20240101000000; BIG.BIN\r\n";
    connect_site_listing(app, peer, listing, "HELLO");
}

static void select_remote(App *app)
{
    press(app, INPUT_BIT(INPUT_L1));
    CHECK(app_state(app)->active_pane == UI_PANE_REMOTE);
}

static int log_has(const char *needle)
{
    for (unsigned i = 0; i < fake_log_count(); ++i) {
        const char *line = fake_log_line(i);
        if (line != NULL && strstr(line, needle) != NULL)
            return 1;
    }
    return 0;
}

/* Count log lines mentioning `needle`. Record details are percent-encoded, so a
 * message with spaces has to be matched as the log writes it: "%20" between
 * words (see logev_format). */
static int log_count(const char *needle)
{
    int count = 0;
    for (unsigned i = 0; i < fake_log_count(); ++i) {
        const char *line = fake_log_line(i);
        if (line != NULL && strstr(line, needle) != NULL)
            count++;
    }
    return count;
}

/* The polled remote pane row, or NULL. */
static const UiRow *remote_row_named(App *app, const char *name)
{
    const UiPaneState *pane = &app_state(app)->panes[UI_PANE_REMOTE];
    for (int i = 0; i < pane->row_count; ++i) {
        if (strcmp(pane->rows[i].name, name) == 0)
            return &pane->rows[i];
    }
    return NULL;
}

static int content_is(const char *path, const char *expected)
{
    char buf[64];
    size_t n = fake_fs_read_file(path, buf, sizeof(buf));
    return n == strlen(expected) && memcmp(buf, expected, n) == 0;
}

/* A destination that is a folder, or whose parent vanished, must end as one
 * failed item and leave the lane usable for the next transfer. */
static void test_download_destination_failure(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    fake_fs_add_dir("ux0:data/TEST.BIN");
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 60; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->dock.state == UI_DOCK_FAILED)
            break;
    }

    CHECK(app_state(app)->dock.state == UI_DOCK_FAILED);
    CHECK(app_state(app)->dock.active == 0);
    CHECK(app_state(app)->dock.message[0] != '\0');
    CHECK(strstr(app_state(app)->dock.message, "FOLDER") != NULL);
    CHECK(!fake_fs_exists("ux0:data/TEST.BIN.vftp.part"));

    /* The lane is idle: the same item succeeds once the folder is gone. */
    FsPort *fs = fake_fs_port();
    CHECK(fs->rmdir(fs->ctx, "ux0:data/TEST.BIN") == FS_OK);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }
    CHECK(app_state(app)->dock.state == UI_DOCK_DONE);
    CHECK(app_state(app)->dock.active == 0);
    CHECK(content_is("ux0:data/TEST.BIN", "HELLO"));
    CHECK(!fake_fs_exists("ux0:data/TEST.BIN.vftp.part"));

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void select_local_named(App *app, const char *name);
static void enter_ux0_data(App *app);
static void start_upload(App *app, FakePeer *peer);
static void enter_local_named(App *app, const char *name, const char *path);

/* A local job starts beside a held download (D72), and a lane cancel never
 * touches the other lane. */
static void test_two_lanes_progress(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, NULL, NULL);
    if (app == NULL)
        return;
    fake_fs_add_dir("ux0:data/DEST");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    fake_peer_set_hold_data(peer, 1);
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_retr_count(peer) > 0 && app_state(app)->dock.active)
            break;
    }
    CHECK(fake_peer_retr_count(peer) > 0);
    CHECK(app_state(app)->dock.active == 1);

    /* A local copy starts while the download is live; the old guard refused it. */
    press(app, INPUT_BIT(INPUT_L1));
    enter_ux0_data(app);
    select_local_named(app, "A.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_COPY);
    press(app, INPUT_BIT(INPUT_CROSS));
    enter_local_named(app, "DEST", "ux0:data/DEST");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 40);

    CHECK(fake_fs_exists("ux0:data/DEST/A.TXT"));
    CHECK(app_state(app)->dock.active == 1);
    CHECK(app_state(app)->local_dock.state == UI_DOCK_DONE);
    CHECK(app_state(app)->local_shown == 1);
    CHECK(!log_has("TRANSFER IN PROGRESS"));

    /* Cancelling the idle local lane must not stop the download. */
    UiCommand cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.type = UI_CMD_CANCEL_LOCAL;
    CHECK(handoff_command_push(app_handoff(app), &cancel) == 1);
    pump(app, peer, 5);
    CHECK(app_state(app)->dock.active == 1);

    /* Cancel the download from the dock. */
    for (int i = 0; i < 4; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->dock.focused == 1);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 40);
    CHECK(fake_peer_abort_count(peer) > 0);
    CHECK(app_state(app)->dock.state == UI_DOCK_IDLE);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* Two cancel presses before the worker reads must both land: the network
 * cancel aborts the download and the local cancel is not an overwrite. */
static void test_two_cancel_presses(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    fake_peer_set_hold_data(peer, 1);
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_retr_count(peer) > 0 && app_state(app)->dock.active)
            break;
    }
    CHECK(fake_peer_retr_count(peer) > 0);
    CHECK(app_state(app)->dock.active == 1);

    UiCommand cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.type = UI_CMD_CANCEL_NETWORK;
    CHECK(handoff_command_push(app_handoff(app), &cancel) == 1);
    memset(&cancel, 0, sizeof(cancel));
    cancel.type = UI_CMD_CANCEL_LOCAL;
    CHECK(handoff_command_push(app_handoff(app), &cancel) == 1);

    pump(app, peer, 40);
    CHECK(fake_peer_abort_count(peer) > 0);
    CHECK(app_state(app)->dock.state == UI_DOCK_IDLE);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* D73: a colliding paste answered WAIT resumes when the blocker releases and
 * re-applies the existing-destination rule. */
static void test_wait_resume_rechecks(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, NULL, NULL);
    if (app == NULL)
        return;
    fake_fs_add_dir("ux0:data/SRC");
    fake_fs_seed_file("ux0:data/SRC/TEST.BIN", "B", 1);

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    fake_peer_set_hold_data(peer, 1);
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_retr_count(peer) > 0 && app_state(app)->dock.active)
            break;
    }
    CHECK(app_state(app)->dock.active == 1);

    /* Copy a file named TEST.BIN into the folder the download is writing. */
    press(app, INPUT_BIT(INPUT_L1));
    enter_ux0_data(app);
    enter_local_named(app, "SRC", "ux0:data/SRC");
    select_local_named(app, "TEST.BIN");
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_COPY);
    press(app, INPUT_BIT(INPUT_CROSS)); /* arm the clipboard */
    select_local_named(app, "..");
    press(app, INPUT_BIT(INPUT_CROSS)); /* back to ux0:data */
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") == 0)
            break;
    }
    CHECK(strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") == 0);
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 10);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_PATH_BUSY);
    CHECK(app_state(app)->confirm.choices[0] == UI_CHOICE_WAIT);
    press(app, INPUT_BIT(INPUT_CROSS)); /* WAIT */
    CHECK(app_state(app)->local_dock.waiting == 1);

    /* The destination appears during the wait; the resume must ask. */
    fake_fs_seed_file("ux0:data/TEST.BIN", "X", 1);

    /* Stop the download so the reservation releases. */
    UiCommand cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.type = UI_CMD_CANCEL_NETWORK;
    CHECK(handoff_command_push(app_handoff(app), &cancel) == 1);
    pump(app, peer, 40);

    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM &&
            app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT)
            break;
    }
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);
    CHECK(app_state(app)->confirm.item_count == 1);
    CHECK(strcmp(app_state(app)->confirm.items[0].name,
                 "ux0:data/TEST.BIN") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* A kept `.vftp.old` is visible and marked; the never-valid `.part` is not
 * listed at all (D81). */
static void test_aside_debris_row(void)
{
    App *app = make_app_ex("ux0:data/K.BIN.VFTP.OLD", "KEEP", NULL, NULL, NULL);
    if (app == NULL)
        return;
    fake_fs_seed_file("ux0:data/A.TXT", "A", 1);
    fake_fs_seed_file("ux0:data/K.BIN.VFTP.PART", "X", 1);

    enter_ux0_data(app);
    const UiPaneState *local = &app_state(app)->panes[UI_PANE_LOCAL];
    int aside = -1;
    int part = -1;
    for (int i = 0; i < local->row_count; ++i) {
        if (strcmp(local->rows[i].name, "K.BIN.VFTP.OLD") == 0)
            aside = i;
        if (strcmp(local->rows[i].name, "K.BIN.VFTP.PART") == 0)
            part = i;
    }
    CHECK(aside >= 0);
    CHECK(part < 0);
    if (aside >= 0)
        CHECK(local->rows[aside].debris == 1);

    app_destroy(app);
}

/* The app holds the listing slot its rows point at across frames. A refresh
 * publishes a newer generation; the app adopts it and releases the old slot in
 * the same step, so the worker can never rewrite a visible row. */
static void test_app_listing_slot_held(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    UiPaneState *pane = &app_state(app)->panes[UI_PANE_LOCAL];
    for (int i = 0; i < 80 && pane->row_count == 0; ++i)
        (void)app_tick(app);
    CHECK(pane->row_count > 0);
    if (pane->row_count == 0) {
        app_destroy(app);
        return;
    }

    Handoff *handoff = app_handoff(app);
    HandoffListings *listings = &handoff->listings[UI_PANE_LOCAL];
    int held = listings->reader_slot;
    CHECK(held >= 0);
    if (held < 0) {
        app_destroy(app);
        return;
    }
    CHECK(listings->slots[held].held == 1);
    CHECK(pane->rows == (const UiRow *)listings->slots[held].rows);
    uint32_t generation = listings->slots[held].generation;

    /* A frame with no new listing must leave the slot held; the old code
     * released it at the end of every frame and kept the row pointer. */
    (void)app_tick(app);
    CHECK(listings->slots[held].held == 1);
    CHECK(listings->reader_slot == held);

    /* A refresh publishes a newer generation; adoption moves the hold. */
    UiCommand refresh;
    memset(&refresh, 0, sizeof(refresh));
    refresh.type = UI_CMD_REFRESH;
    refresh.pane = UI_PANE_LOCAL;
    CHECK(handoff_command_push(handoff, &refresh) == 1);

    int adopted = 0;
    for (int i = 0; i < 80 && !adopted; ++i) {
        (void)app_tick(app);
        if (listings->reader_slot != held)
            adopted = 1;
    }
    CHECK(adopted);
    if (!adopted) {
        app_destroy(app);
        return;
    }

    int next = listings->reader_slot;
    CHECK(next >= 0 && next != held);
    CHECK(listings->slots[next].held == 1);
    CHECK(listings->slots[next].generation > generation);
    CHECK(listings->slots[held].held == 0);
    CHECK(pane->rows == (const UiRow *)listings->slots[next].rows);

    app_destroy(app);
}

/* A successful local rename whose aside cleanup fails keeps the operation
 * green and names the kept copy (the end-to-end notice path). */
static void test_local_rename_aside_notice(void)
{
    App *app = make_app_ex("ux0:data/OLD.TXT", "NEW", NULL, NULL, NULL);
    if (app == NULL)
        return;
    fake_fs_seed_file("ux0:data/NEW.TXT", "OLD", 3);
    fake_fs_set_rename_replaces(0);
    fake_fs_fail_remove_from(0);

    enter_ux0_data(app);
    select_local_named(app, "OLD.TXT");
    fake_ime_queue_text("NEW.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 4; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_RENAME);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT)); /* OVERWRITE */
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        if (app_state(app)->local_dock.state == UI_DOCK_DONE &&
            app_state(app)->local_dock.notice)
            break;
    }
    CHECK(app_state(app)->local_dock.state == UI_DOCK_DONE);
    CHECK(app_state(app)->local_dock.notice == 1);
    CHECK(strstr(app_state(app)->local_dock.message, "ASIDE CLEANUP FAILED") !=
          NULL);
    CHECK(content_is("ux0:data/NEW.TXT", "NEW"));
    CHECK(fake_fs_exists("ux0:data/NEW.TXT.vftp.old"));

    app_destroy(app);
}

/* A SIZE-only preflight still refuses a destination the current remote
 * listing shows as a folder. */
static void test_upload_preflight_size_folder(void)
{
    App *app = make_app_ex("ux0:data/DEST", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "DEST");

    static const char *listing =
        "type=dir;modify=20240101000000; DEST\r\n"
        "type=file;size=5;modify=20240101000000; F.BIN\r\n";
    FakePeer *peer = peer_new();
    connect_site_listing(app, peer, listing, "HELLO");
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/DEST");
    fake_peer_set_exists_size(peer, 0);

    start_upload(app, peer);
    CHECK(fake_peer_size_count(peer) == 1);
    /* The row already types the destination, so no refresh is asked for. */
    CHECK(fake_peer_listing_count(peer) == 1);
    CHECK(fake_peer_stor_count(peer) == 0);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(strstr(app_state(app)->status, "DESTINATION IS A FOLDER") != NULL);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_connect_list_download(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    /* Rows are .., BIG.BIN, TEST.BIN; select TEST.BIN. */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_SQUARE));
    CHECK(ui_state_mark_count(app_state(app), UI_PANE_REMOTE) == 1);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }

    CHECK(app_state(app)->dock.state == UI_DOCK_DONE);
    CHECK(content_is("ux0:data/TEST.BIN", "HELLO"));
    CHECK(!fake_fs_exists("ux0:data/TEST.BIN.vftp.part"));

    CHECK(log_has("app_start"));
    CHECK(log_has("screen=WORKSPACE"));
    CHECK(log_has("screen=SITES"));
    CHECK(log_has("key=SELECT"));
    CHECK(log_has("cmd=connect_site"));
    /* The worker logs the resolved IPv4 address before the connect. */
    CHECK(log_has("path=192.168.1.42"));
    CHECK(log_has("list"));
    CHECK(log_has("transfer_start"));
    CHECK(log_has("transfer_done"));

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void select_local_named(App *app, const char *name)
{
    /* Start from the top so a target above the current cursor is reachable. */
    for (int i = 0; i < 8; ++i)
        press(app, INPUT_BIT(INPUT_UP));
    for (int i = 0; i < 8; ++i) {
        const UiPaneState *pane = &app_state(app)->panes[UI_PANE_LOCAL];
        if (pane->selected >= 0 && pane->selected < pane->row_count &&
            strcmp(pane->rows[pane->selected].name, name) == 0)
            return;
        press(app, INPUT_BIT(INPUT_DOWN));
    }
}

static void enter_ux0_data(App *app)
{
    pump(app, NULL, 10);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:") == 0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 1)
            break;
    }
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") ==
                0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 2)
            break;
    }
    pump(app, NULL, 5);
}

static void enter_local_named(App *app, const char *name, const char *path)
{
    select_local_named(app, name);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, path) == 0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 1)
            break;
    }
    pump(app, NULL, 5);
}

static void test_local_file_actions(void)
{
    App *app = make_app_ex("ux0:data/OLD.TXT", "BYE", NULL, NULL, NULL);
    if (app == NULL)
        return;

    CHECK(fake_fs_exists("ux0:"));
    CHECK(fake_fs_exists("ux0:data"));
    CHECK(fake_fs_exists("ux0:data/OLD.TXT"));
    pump(app, NULL, 10);

    const UiPaneState *local = &app_state(app)->panes[UI_PANE_LOCAL];
    CHECK(strcmp(local->path, "*") == 0);
    CHECK(local->row_count >= 2);

    /* Enter ux0: from the all-drives root. */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        local = &app_state(app)->panes[UI_PANE_LOCAL];
        if (strcmp(local->path, "ux0:") == 0 && local->row_count >= 1)
            break;
    }
    CHECK(strcmp(local->path, "ux0:") == 0);
    CHECK(local->row_count >= 1);
    /* A volume root has no ".." row; data is row 0. */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        local = &app_state(app)->panes[UI_PANE_LOCAL];
        if (strcmp(local->path, "ux0:data") == 0 && local->row_count >= 2)
            break;
    }
    CHECK(strcmp(local->path, "ux0:data") == 0);
    pump(app, NULL, 5);
    select_local_named(app, "OLD.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->screen == UI_SCREEN_ACTIONS);
    CHECK(strcmp(app_state(app)->actions.path, "ux0:data/OLD.TXT") == 0);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_DELETE] == 1);
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_DELETE);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 10);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(!fake_fs_exists("ux0:data/OLD.TXT"));

    /* New folder through the IME. */
    fake_ime_queue_text("NEWDIR");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 3; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_NEW_FOLDER);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 10);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(fake_fs_exists("ux0:data/NEWDIR"));
    pump(app, NULL, 5);

    /* Rename the new folder through the IME. */
    select_local_named(app, "NEWDIR");
    fake_ime_queue_text("RENAMED");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 4; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_RENAME);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 10);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(fake_fs_exists("ux0:data/RENAMED"));
    CHECK(!fake_fs_exists("ux0:data/NEWDIR"));

    app_destroy(app);
}

static void test_local_new_folder_without_selection(void)
{
    App *app = make_app_ex(NULL, NULL, "ux0:data/EMPTY", NULL, NULL);
    if (app == NULL)
        return;

    pump(app, NULL, 10);

    /* ALL DRIVES -> ux0: -> ux0:data -> EMPTY. */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:") == 0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 1)
            break;
    }
    CHECK(strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:") == 0);

    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") ==
                0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 2)
            break;
    }
    CHECK(strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") == 0);
    pump(app, NULL, 5);

    select_local_named(app, "EMPTY");
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path,
                   "ux0:data/EMPTY") == 0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count == 1)
            break;
    }
    CHECK(strcmp(app_state(app)->panes[UI_PANE_LOCAL].path,
                 "ux0:data/EMPTY") == 0);
    CHECK(app_state(app)->panes[UI_PANE_LOCAL].row_count == 1);

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->screen == UI_SCREEN_ACTIONS);
    CHECK(app_state(app)->actions.has_selection == 0);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_NEW_FOLDER] == 1);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_RENAME] == 0);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_DELETE] == 0);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_COPY] == 0);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_MOVE] == 0);

    /* NEW FOLDER is the only live action; it still creates in this folder. */
    fake_ime_queue_text("SUB");
    for (int i = 0; i < 3; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_NEW_FOLDER);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 10);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(fake_fs_exists("ux0:data/EMPTY/SUB"));

    app_destroy(app);
}

static void test_clipboard_copy(void)
{
    App *app = make_app_ex("ux0:data/SRC.TXT", "HELLO", "ux0:data/DEST",
                           NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "SRC.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->screen == UI_SCREEN_ACTIONS);
    CHECK(app_state(app)->actions.selected == UI_ACTION_COPY);
    press(app, INPUT_BIT(INPUT_CROSS));

    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(app_state(app)->clip_mode == UI_CLIP_COPY);
    CHECK(app_state(app)->clip_count == 1);
    CHECK(strcmp(app_state(app)->clip_paths[0].text, "ux0:data/SRC.TXT") == 0);

    enter_local_named(app, "DEST", "ux0:data/DEST");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->screen == UI_SCREEN_ACTIONS);
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);

    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(app_state(app)->clip_mode == UI_CLIP_NONE);
    CHECK(fake_fs_exists("ux0:data/DEST/SRC.TXT"));
    CHECK(fake_fs_exists("ux0:data/SRC.TXT"));
    CHECK(content_is("ux0:data/DEST/SRC.TXT", "HELLO"));

    app_destroy(app);
}

static void test_clipboard_move(void)
{
    App *app = make_app_ex("ux0:data/SRC.TXT", "HELLO", "ux0:data/DEST",
                           NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "SRC.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN)); /* COPY -> MOVE */
    CHECK(app_state(app)->actions.selected == UI_ACTION_MOVE);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->clip_mode == UI_CLIP_MOVE);

    enter_local_named(app, "DEST", "ux0:data/DEST");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);

    CHECK(!fake_fs_exists("ux0:data/SRC.TXT"));
    CHECK(fake_fs_exists("ux0:data/DEST/SRC.TXT"));
    CHECK(content_is("ux0:data/DEST/SRC.TXT", "HELLO"));

    app_destroy(app);
}

static void test_delete_confirm_fresh(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, "ux0:data/B.TXT", "B");
    if (app == NULL)
        return;

    enter_ux0_data(app);

    /* Delete A and let the listing refresh prune its mark. */
    select_local_named(app, "A.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN)); /* COPY..DELETE */
    CHECK(app_state(app)->actions.selected == UI_ACTION_DELETE);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.item_count == 1);
    CHECK(strcmp(app_state(app)->confirm.items[0].name,
                 "ux0:data/A.TXT") == 0);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);
    CHECK(!fake_fs_exists("ux0:data/A.TXT"));
    CHECK(ui_state_mark_count(app_state(app), UI_PANE_LOCAL) == 0);

    /* The next delete must name B, never the removed A. */
    select_local_named(app, "B.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.item_count == 1);
    CHECK(strcmp(app_state(app)->confirm.items[0].name,
                 "ux0:data/B.TXT") == 0);
    press(app, INPUT_BIT(INPUT_CROSS)); /* focus is CANCEL */
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(fake_fs_exists("ux0:data/B.TXT"));

    app_destroy(app);
}

static void test_delete_inside_tai_and_protected(void)
{
    App *app = make_app_ex("ur0:tai/x.suprx", "X", NULL,
                           "ur0:tai/boot_config.txt", "CONFIG");
    if (app == NULL)
        return;

    pump(app, NULL, 10);
    enter_local_named(app, "ur0:", "ur0:");
    enter_local_named(app, "tai", "ur0:tai");

    /* D87: a tai file needs the ordinary delete confirmation, not the retired
     * high-risk gate. */
    select_local_named(app, "x.suprx");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_DELETE);
    CHECK(app_state(app)->confirm.item_count == 1);
    CHECK(strcmp(app_state(app)->confirm.items[0].name,
                 "ur0:tai/x.suprx") == 0);
    press(app, INPUT_BIT(INPUT_CROSS)); /* CANCEL */
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(fake_fs_exists("ur0:tai/x.suprx"));

    /* Confirm it. */
    select_local_named(app, "x.suprx");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);
    CHECK(!fake_fs_exists("ur0:tai/x.suprx"));

    /* boot_config.txt is denied before a confirmation can open. */
    select_local_named(app, "boot_config.txt");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_NONE);
    CHECK(app_state(app)->status[0] != '\0');
    CHECK(fake_fs_exists("ur0:tai/boot_config.txt"));

    app_destroy(app);
}

static void test_cancel(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    /* Select BIG.BIN directly (row 1). */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));

    fake_peer_set_hold_data(peer, 1);
    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_retr_count(peer) > 0 && app_state(app)->dock.active)
            break;
    }
    CHECK(fake_peer_retr_count(peer) > 0);
    CHECK(app_state(app)->dock.active == 1);
    CHECK(app_state(app)->dock.total == 5);
    CHECK(app_state(app)->dock.total_known == 1);

    /* Down to the last row, then into the dock, Cross, Right, Cross. */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->dock.focused == 1);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));

    pump(app, peer, 40);
    CHECK(fake_peer_abort_count(peer) > 0);
    CHECK(!fake_fs_exists("ux0:data/BIG.BIN"));
    CHECK(!fake_fs_exists("ux0:data/BIG.BIN.vftp.part"));
    CHECK(app_state(app)->dock.state == UI_DOCK_IDLE);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_save_failure(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    const SiteBook *book = worker_sites(app_worker(app));
    CHECK(book != NULL);
    CHECK(site_book_count(book) == 1);

    UiCommand save;
    memset(&save, 0, sizeof(save));
    save.type = UI_CMD_SAVE_SITE;
    save.index = 0;
    const Site *site = site_book_at(book, 0);
    if (site != NULL)
        save.site = *site;

    fake_fs_fail_op(FAKE_FS_OP_OPEN, FS_NO_SPACE);
    CHECK(handoff_command_push(app_handoff(app), &save) == 1);
    pump(app, NULL, 20);

    CHECK(app_state(app)->sites.save_unavailable == 1);
    CHECK(site_book_count(worker_sites(app_worker(app))) == 1);

    app_destroy(app);
}

static void test_shutdown_cancels(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    fake_peer_set_hold_data(peer, 1);
    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_retr_count(peer) > 0)
            break;
    }
    CHECK(fake_peer_retr_count(peer) > 0);

    UiCommand quit;
    memset(&quit, 0, sizeof(quit));
    quit.type = UI_CMD_QUIT;
    CHECK(handoff_command_push(app_handoff(app), &quit) == 1);
    CHECK(app_tick(app) == 0);

    CHECK(!fake_fs_exists("ux0:data/BIG.BIN"));
    CHECK(!fake_fs_exists("ux0:data/BIG.BIN.vftp.part"));

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_remote_navigation(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    static const char *root =
        "type=dir;size=0;modify=20240101000000; SUB\r\n"
        "type=file;size=5;modify=20240101000000; A.TXT\r\n";
    static const char *sub =
        "type=file;size=3;modify=20240101000000; IN.TXT\r\n";

    FakePeer *peer = peer_new();
    connect_site_listing(app, peer, root, NULL);
    fake_peer_set_sub_listing(peer, sub);
    select_remote(app);

    /* At the remote root there is no ".." row; SUB is row 0. */
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        UiPaneState *r = &app_state(app)->panes[UI_PANE_REMOTE];
        if (!r->loading && strcmp(r->path, "/SUB") == 0 &&
            r->row_count >= 2)
            break;
    }

    UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
    CHECK(fake_peer_cwd_count(peer) == 1);
    CHECK(remote->loading == 0);
    CHECK(strcmp(remote->path, "/SUB") == 0);
    CHECK(remote->row_count >= 2);
    CHECK(remote->rows != NULL);
    if (remote->rows != NULL)
        CHECK(strcmp(remote->rows[1].name, "IN.TXT") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_remote_mutations(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    static const char *root =
        "type=dir;size=0;modify=20240101000000; SUB\r\n"
        "type=file;size=5;modify=20240101000000; A.TXT\r\n";

    FakePeer *peer = peer_new();
    connect_site_listing(app, peer, root, NULL);
    select_remote(app);

    /* NEW FOLDER: cursor on ".." is no selection; the folder is the target. */
    fake_ime_queue_text("NEWDIR");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN)); /* DOWNLOAD -> NEW FOLDER */
    CHECK(app_state(app)->actions.selected == UI_ACTION_NEW_FOLDER);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 80);
    CHECK(fake_peer_mkd_count(peer) == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    /* RENAME the directory (row 0 at the remote root). */
    press(app, INPUT_BIT(INPUT_UP));
    press(app, INPUT_BIT(INPUT_UP));
    fake_ime_queue_text("RENAMED");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN)); /* DOWNLOAD -> NEW FOLDER */
    press(app, INPUT_BIT(INPUT_DOWN)); /* -> RENAME */
    CHECK(app_state(app)->actions.selected == UI_ACTION_RENAME);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 80);
    CHECK(fake_peer_rename_count(peer) == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    /* DELETE the directory: the command must use RMD, not the local planner. */
    press(app, INPUT_BIT(INPUT_UP));
    press(app, INPUT_BIT(INPUT_UP));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 3; ++i)
        press(app, INPUT_BIT(INPUT_DOWN)); /* -> DELETE */
    CHECK(app_state(app)->actions.selected == UI_ACTION_DELETE);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 80);
    CHECK(fake_peer_rmd_count(peer) == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_remote_disconnect_and_reconnect(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    /* DISCONNECT is the sixth remote action. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_DISCONNECT);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);

    CHECK(app_state(app)->panes[UI_PANE_REMOTE].connected == 0);
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].row_count == 0);
    CHECK(app_state(app)->site_known == 0);
    CHECK(fake_net_is_open(fake_peer_control(peer)) == 0);

    /* A connect while a session is shown as connected must tear it down and
     * start a new one instead of returning CONNECT REFUSED. */
    static const char *listing =
        "type=file;size=5;modify=20240101000000; TEST.BIN\r\n"
        "type=file;size=100;modify=20240101000000; BIG.BIN\r\n";
    UiCommand connect;
    memset(&connect, 0, sizeof(connect));
    connect.type = UI_CMD_CONNECT_SITE;
    connect.index = 0;
    CHECK(handoff_command_push(app_handoff(app), &connect) == 1);
    (void)app_tick(app);

    FakePeer *again = peer_new();
    fake_peer_set_listing(again, listing);
    fake_peer_set_payload(again, "HELLO");
    fake_peer_open(again);
    pump(app, again, 120);
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].connected == 1);
    CHECK(fake_peer_listing_count(again) >= 1);
    CHECK(fake_peer_cwd_count(again) == 0); /* the first listing needs no CWD */

    fake_peer_destroy(again);
    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_remote_unsafe_name(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    static const char *root =
        "type=file;size=5;modify=20240101000000; EVIL/NAME\r\n"
        "type=file;size=3;modify=20240101000000; OK.TXT\r\n";

    FakePeer *peer = peer_new();
    connect_site_listing(app, peer, root, NULL);
    select_remote(app);

    UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
    CHECK(remote->row_count >= 2);
    int unsafe_index = -1;
    for (int i = 0; i < remote->row_count; ++i) {
        if (strcmp(remote->rows[i].name, "EVIL/NAME") == 0)
            unsafe_index = i;
    }
    CHECK(unsafe_index >= 0);
    if (unsafe_index < 0) {
        fake_peer_destroy(peer);
        app_destroy(app);
        return;
    }
    CHECK(remote->rows[unsafe_index].unsafe == 1);

    remote->selected = unsafe_index;
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(strstr(app_state(app)->status, "UNSAFE NAME") != NULL);
    CHECK(strstr(remote->error, "UNSAFE NAME") != NULL);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_DOWNLOAD);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_DOWNLOAD] == 0);
    CHECK(strcmp(app_state(app)->actions.reason[UI_ACTION_DOWNLOAD],
                 "UNSAFE NAME") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_remote_volume_root_open(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    static const char *root =
        "type=dir;modify=20240101000000; UX0:\r\n"
        "type=dir;modify=20240101000000; UR0:\r\n";

    FakePeer *peer = peer_new();
    connect_site_listing(app, peer, root, NULL);
    select_remote(app);

    UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
    int target = -1;
    for (int i = 0; i < remote->row_count; ++i) {
        if (strcmp(remote->rows[i].name, "UX0:") == 0)
            target = i;
    }
    CHECK(target >= 0);
    if (target < 0) {
        fake_peer_destroy(peer);
        app_destroy(app);
        return;
    }

    remote->selected = target;
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_cwd_count(peer) > 0)
            break;
    }
    CHECK(fake_peer_cwd_count(peer) == 1);
    CHECK(strcmp(fake_peer_cwd(peer), "/UX0:") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_remote_refresh(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    static const char *fresh =
        "type=file;size=7;modify=20240101000000; NEW.TXT\r\n"
        "type=file;size=9;modify=20240101000000; OTHER.TXT\r\n";
    fake_peer_set_listing(peer, fresh);

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 4; ++i) /* DOWNLOAD -> REFRESH */
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_REFRESH);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        const UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
        if (!remote->loading && remote->row_count >= 2)
            break;
    }

    const UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
    CHECK(remote->row_count >= 2);
    CHECK(remote->rows != NULL);
    if (remote->rows != NULL) {
        int found = 0;
        for (int i = 0; i < remote->row_count; ++i) {
            if (strcmp(remote->rows[i].name, "NEW.TXT") == 0)
                found = 1;
        }
        CHECK(found == 1);
    }

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_upload_conflict(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    pump(app, NULL, 10);
    press(app, INPUT_BIT(INPUT_CROSS)); /* ux0: */
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:") == 0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 1)
            break;
    }
    press(app, INPUT_BIT(INPUT_CROSS)); /* ux0:data */
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") ==
                0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 2)
            break;
    }
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_exists_path(peer, "/UP.TXT");
    fake_peer_set_exists_size(peer, 5);

    /* First attempt: the remote file exists, so the batch asks. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);
    press(app, INPUT_BIT(INPUT_RIGHT)); /* SKIP */
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 40);
    CHECK(fake_peer_stor_count(peer) == 0);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    /* Second attempt: OVERWRITE. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT)); /* OVERWRITE */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 160; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_stor_count(peer) > 0 &&
            app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }
    CHECK(fake_peer_stor_count(peer) == 1);
    CHECK(strcmp(fake_peer_last_stor(peer), "/UP.TXT") == 0);

    /* Let the post-upload remote refresh settle before the next action. */
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (!app_state(app)->panes[UI_PANE_REMOTE].loading)
            break;
    }

    /* A third attempt chooses CANCEL: the batch stops, no STOR. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_CROSS)); /* CANCEL is focused first */
    pump(app, peer, 40);
    CHECK(fake_peer_stor_count(peer) == 1);
    /* CANCEL must be observed as a cancellation, not look like SKIP. */
    CHECK(app_state(app)->dock.state == UI_DOCK_IDLE);
    CHECK(strcmp(app_state(app)->status, "NETWORK CANCELLED") == 0);

    /* CANCEL cleared the batch latch: a new attempt asks again. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_CIRCLE));

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void start_upload(App *app, FakePeer *peer)
{
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 120);
}

static void test_upload_preflight_unsupported(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);

    /* A server that does not implement MLST keeps the old direct STOR. */
    start_upload(app, peer);
    CHECK(fake_peer_stor_count(peer) == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* When MLST is refused, SIZE still detects an existing destination. */
static void test_upload_preflight_size_fallback(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/UP.TXT");
    fake_peer_set_exists_size(peer, 5);

    start_upload(app, peer);
    CHECK(fake_peer_size_count(peer) == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);

    press(app, INPUT_BIT(INPUT_RIGHT)); /* SKIP */
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 40);
    CHECK(fake_peer_stor_count(peer) == 0);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_upload_preflight_directory(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_DIR);

    start_upload(app, peer);
    CHECK(fake_peer_stor_count(peer) == 0);
    CHECK(strstr(app_state(app)->status, "DESTINATION IS A FOLDER") != NULL);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_upload_preflight_failure(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_FAIL);

    start_upload(app, peer);
    CHECK(fake_peer_stor_count(peer) == 0);
    CHECK(strstr(app_state(app)->status, "REMOTE CHECK FAILED") != NULL);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* Start an upload through the actions ledger and drive the probe to its SIZE
 * answer, leaving the peer unable to answer the refresh that follows. */
static void start_upload_until_size(App *app, FakePeer *peer)
{
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_size_count(peer) == 1)
            break;
    }
    CHECK(fake_peer_size_count(peer) == 1);
    /* The answer is read on the next step; the peer then stays silent, so the
     * refresh it asks for is still in flight. */
    for (int i = 0; i < 4; ++i)
        (void)app_tick(app);
}

/* AE4: the listing published when the session opened has no DEST row; the one
 * refresh the preflight asks for types it as a folder, so the STOR is refused
 * before it is sent. */
static void test_upload_preflight_refresh_folder(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/UP.TXT");
    fake_peer_set_exists_size(peer, 5);
    CHECK(fake_peer_listing_count(peer) == 1);

    fake_peer_set_listing(peer, "type=dir;modify=20240101000000; UP.TXT\r\n");

    start_upload(app, peer);
    CHECK(fake_peer_size_count(peer) == 1);
    /* One refresh, and no listing rescheduled after it: the lanes went idle
     * with the batch over, so a second listing would be a loop (KTD4). */
    CHECK(fake_peer_listing_count(peer) == 2);
    CHECK(fake_peer_stor_count(peer) == 0);
    CHECK(strstr(app_state(app)->status, "DESTINATION IS A FOLDER") != NULL);

    /* The refreshed rows are the pane's rows, not a private copy. */
    const UiRow *row = remote_row_named(app, "UP.TXT");
    CHECK(row != NULL);
    if (row != NULL)
        CHECK(row->type == UI_ROW_DIR);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* R7: a destination the current listing already shows as a file needs no
 * refresh; the overwrite decision applies directly. */
static void test_upload_preflight_refresh_not_for_listed_file(void)
{
    App *app = make_app_ex("ux0:data/TEST.BIN", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "TEST.BIN");

    FakePeer *peer = peer_new();
    connect_site(app, peer); /* the open listing already holds TEST.BIN */
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/TEST.BIN");
    fake_peer_set_exists_size(peer, 5);

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(fake_peer_size_count(peer) == 1);
    /* The row says file; nothing to refresh. */
    CHECK(fake_peer_listing_count(peer) == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);

    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT)); /* OVERWRITE */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_stor_count(peer) > 0)
            break;
    }
    CHECK(fake_peer_stor_count(peer) == 1);
    CHECK(strcmp(fake_peer_last_stor(peer), "/TEST.BIN") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* AE5: SIZE reports no such destination, so no refresh is asked for and the
 * STOR proceeds (R7, R10). */
static void test_upload_preflight_refresh_not_needed(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/OTHER.TXT");

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_stor_count(peer) > 0)
            break;
    }
    CHECK(fake_peer_size_count(peer) == 1);
    CHECK(fake_peer_listing_count(peer) == 1); /* the open listing only */
    CHECK(fake_peer_stor_count(peer) == 1);
    CHECK(strcmp(fake_peer_last_stor(peer), "/UP.TXT") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* AE6, reachable half: a second item with the same destination name is queued
 * while the refresh is in flight. The arriving refresh decides the item it was
 * asked for; the new item runs its own probe and is decided by that (KTD3). */
static void test_upload_preflight_refresh_names_its_own_item(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/UP.TXT");
    fake_peer_set_exists_size(peer, 5);

    start_upload_until_size(app, peer);

    /* The same destination again, queued behind the item whose refresh owns
     * the lane. The UI cannot do this itself (the pane is READING), so the
     * command the ledger would emit is pushed directly. */
    UiCommand second;
    memset(&second, 0, sizeof(second));
    second.type = UI_CMD_TRANSFER;
    second.pane = UI_PANE_LOCAL;
    second.source_type = UI_ROW_FILE;
    snprintf(second.path, sizeof(second.path), "ux0:data/UP.TXT");
    snprintf(second.path2, sizeof(second.path2), "/");
    CHECK(handoff_command_push(app_handoff(app), &second) == 1);
    (void)app_tick(app);

    /* The refreshed listing refuses the first item. The second one's own SIZE
     * then says the name is gone, so it is stored instead of inheriting the
     * first item's folder answer. */
    fake_peer_set_listing(peer, "type=dir;modify=20240101000000; UP.TXT\r\n");
    fake_peer_set_exists_path(peer, "/GONE.TXT");
    pump(app, peer, 160);

    CHECK(fake_peer_size_count(peer) == 2);
    /* Open, one refresh, the post-upload refresh. */
    CHECK(fake_peer_listing_count(peer) == 3);
    CHECK(fake_peer_stor_count(peer) == 1);
    CHECK(strcmp(fake_peer_last_stor(peer), "/UP.TXT") == 0);
    CHECK(log_count("DESTINATION%20IS%20A%20FOLDER") == 1);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* R7: the refresh is asked for only while the pane shows the destination's own
 * parent. A batch queued in /SUB that runs with the pane back in / leaves the
 * destination untyped and asks for no listing. */
static void test_upload_preflight_refresh_parent_not_shown(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/SUB/UP.TXT");
    fake_peer_set_exists_size(peer, 5);

    UiCommand upload;
    memset(&upload, 0, sizeof(upload));
    upload.type = UI_CMD_TRANSFER;
    upload.pane = UI_PANE_LOCAL;
    upload.source_type = UI_ROW_FILE;
    snprintf(upload.path, sizeof(upload.path), "ux0:data/UP.TXT");
    snprintf(upload.path2, sizeof(upload.path2), "/SUB");
    CHECK(handoff_command_push(app_handoff(app), &upload) == 1);
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(fake_peer_size_count(peer) == 1);
    /* No refresh: /SUB is not what the pane shows. */
    CHECK(fake_peer_listing_count(peer) == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);

    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT)); /* OVERWRITE */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_stor_count(peer) > 0)
            break;
    }
    CHECK(fake_peer_stor_count(peer) == 1);
    CHECK(strcmp(fake_peer_last_stor(peer), "/SUB/UP.TXT") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* R9: a cancel while the refresh owns the lane is refused, and the item still
 * reaches its decision when the listing ends. */
static void test_upload_preflight_refresh_refuses_cancel(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/UP.TXT");
    fake_peer_set_exists_size(peer, 5);

    start_upload_until_size(app, peer);

    UiCommand cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.type = UI_CMD_CANCEL_NETWORK;
    CHECK(handoff_command_push(app_handoff(app), &cancel) == 1);
    (void)app_tick(app);
    CHECK(strstr(app_state(app)->status, "REMOTE CHECK IN PROGRESS") != NULL);

    fake_peer_set_listing(peer, "type=dir;modify=20240101000000; UP.TXT\r\n");
    pump(app, peer, 120);
    CHECK(fake_peer_listing_count(peer) == 2);
    CHECK(fake_peer_stor_count(peer) == 0);
    /* The refused cancel left the item in place: the refresh still decides it. */
    CHECK(strstr(app_state(app)->status, "DESTINATION IS A FOLDER") != NULL);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* R9: a refresh whose listing fails leaves the destination untyped and reports
 * one listing error; the item then proceeds on the file path. */
static void test_upload_preflight_refresh_failure(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/UP.TXT");
    fake_peer_set_exists_size(peer, 5);

    start_upload_until_size(app, peer);
    fake_peer_set_list_fail(peer, 1);

    for (int i = 0; i < 60; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    /* MLSD and its LIST fallback are both refused; the untyped destination
     * then follows the existing overwrite decision. */
    CHECK(fake_peer_list_fail_count(peer) == 2);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(strstr(app_state(app)->panes[UI_PANE_REMOTE].error,
                 "REMOTE LISTING FAILED") != NULL);
    /* One failed listing on the log: the refresh, not a retry loop. */
    CHECK(log_count("stage=read\tcls=server") == 1);

    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT)); /* OVERWRITE */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_stor_count(peer) > 0)
            break;
    }
    CHECK(fake_peer_stor_count(peer) == 1);
    CHECK(strcmp(fake_peer_last_stor(peer), "/UP.TXT") == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* R9: each item refreshes at most once. Two marked uploads each refresh their
 * own parent once and are each refused as a folder. */
static void test_upload_preflight_refresh_once_per_item(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, "ux0:data/B.TXT", "B");
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "A.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    select_local_named(app, "B.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_mlst(peer, FAKE_PEER_MLST_UNSUPPORTED);
    fake_peer_set_size_supported(peer, 1);
    fake_peer_set_exists_path(peer, "/A.TXT");
    fake_peer_set_second_exists_path(peer, "/B.TXT");
    fake_peer_set_exists_size(peer, 1);
    fake_peer_set_listing(peer, "type=dir;modify=20240101000000; A.TXT\r\n");

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 240; ++i) {
        (void)app_tick(app);
        /* The first refresh types only A; retarget the peer so the second
         * item's refresh is needed and types only B. */
        if (fake_peer_listing_count(peer) == 2)
            fake_peer_set_listing(peer,
                                  "type=dir;modify=20240101000000; B.TXT\r\n");
        fake_peer_serve(peer);
    }
    CHECK(fake_peer_size_count(peer) == 2);    /* each item asked for itself */
    /* Open, A's refresh, B's refresh. */
    CHECK(fake_peer_listing_count(peer) == 3);
    CHECK(fake_peer_stor_count(peer) == 0);
    CHECK(log_count("DESTINATION%20IS%20A%20FOLDER") == 2);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_download_conflict(void)
{
    App *app = make_app_ex("ux0:data/TEST.BIN", "OLD!!", NULL, NULL, NULL);
    if (app == NULL)
        return;

    pump(app, NULL, 10);
    FakePeer *peer = peer_new();
    connect_site(app, peer);
    select_remote(app);

    /* Rows are .., BIG.BIN, TEST.BIN. */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);

    /* SKIP keeps the local bytes and starts no RETR. */
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 60);
    CHECK(fake_peer_retr_count(peer) == 0);
    CHECK(content_is("ux0:data/TEST.BIN", "OLD!!"));

    /* OVERWRITE runs the RETR and replaces the destination. */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 160; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_retr_count(peer) > 0 &&
            app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }
    CHECK(fake_peer_retr_count(peer) == 1);
    CHECK(content_is("ux0:data/TEST.BIN", "HELLO"));

    fake_peer_destroy(peer);
    app_destroy(app);
}

/* A download that loses the final reply ends the lane; the remaining queued
 * item must be named in the one terminal instead of vanishing with the session. */
static void test_download_final_reply_loses_queue(void)
{
    App *app = make_app_ex("ux0:data/SEED.TXT", "SEED", NULL, NULL, NULL);
    if (app == NULL)
        return;

    static const char *listing =
        "type=file;size=5;modify=20240101000000; A.BIN\r\n"
        "type=file;size=5;modify=20240101000000; B.BIN\r\n";
    FakePeer *peer = peer_new();
    connect_site_listing(app, peer, listing, "HELLO");
    fake_peer_set_hold_final(peer, 1);
    select_remote(app);

    press(app, INPUT_BIT(INPUT_SQUARE)); /* A.BIN is row 0 */
    press(app, INPUT_BIT(INPUT_DOWN));   /* B.BIN */
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_DOWNLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (fake_peer_retr_count(peer) > 0 &&
            app_state(app)->dock.state == UI_DOCK_ACTIVE)
            break;
    }
    CHECK(fake_peer_retr_count(peer) == 1);

    /* Let the client read the data EOF and enter DRAINING before the clock
     * jumps; otherwise the stage deadline wins the race. */
    pump(app, peer, 10);
    fake_clock_advance(10001);
    for (int i = 0; i < 60; ++i) {
        (void)app_tick(app);
        fake_peer_serve(peer);
        if (app_state(app)->dock.state == UI_DOCK_FAILED)
            break;
    }

    CHECK(fake_peer_retr_count(peer) == 1);
    CHECK(app_state(app)->dock.state == UI_DOCK_FAILED);
    CHECK(app_state(app)->dock.file_kept == 1);
    CHECK(strstr(app_state(app)->dock.message, "FINAL REPLY MISSING") != NULL);
    CHECK(strstr(app_state(app)->dock.message, "1 ITEM CANCELLED") != NULL);
    CHECK(content_is("ux0:data/A.BIN", "HELLO"));
    CHECK(!fake_fs_exists("ux0:data/B.BIN"));

    /* The lane survives the terminal: reconnect the saved site and move one
     * more file through the same worker. */
    UiCommand reconnect;
    memset(&reconnect, 0, sizeof(reconnect));
    reconnect.type = UI_CMD_RECONNECT;
    CHECK(handoff_command_push(app_handoff(app), &reconnect) == 1);
    (void)app_tick(app);

    static const char *again_listing =
        "type=file;size=5;modify=20240101000000; C.BIN\r\n";
    FakePeer *again = peer_new();
    fake_peer_set_listing(again, again_listing);
    fake_peer_set_payload(again, "WORLD");
    fake_peer_open(again);
    pump(app, again, 120);
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].connected == 1);
    CHECK(fake_peer_listing_count(again) >= 1);

    if (app_state(app)->active_pane != UI_PANE_REMOTE)
        press(app, INPUT_BIT(INPUT_L1));
    CHECK(app_state(app)->active_pane == UI_PANE_REMOTE);
    (void)app_tick(app);
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_DOWNLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        fake_peer_serve(again);
        if (fake_peer_retr_count(again) > 0 &&
            app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }
    CHECK(fake_peer_retr_count(again) == 1);
    CHECK(app_state(app)->dock.state == UI_DOCK_DONE);
    CHECK(content_is("ux0:data/C.BIN", "WORLD"));

    fake_peer_destroy(again);
    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_listing_cap(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    fake_fs_add_dir("ux0:data/BIG");
    char path[64];
    for (int i = 0; i < 2001; ++i) {
        snprintf(path, sizeof(path), "ux0:data/BIG/F%04d.TXT", i);
        fake_fs_add_file(path, 1);
    }

    pump(app, NULL, 10);
    enter_ux0_data(app);
    enter_local_named(app, "BIG", "ux0:data/BIG");

    const UiPaneState *local = &app_state(app)->panes[UI_PANE_LOCAL];
    CHECK(strcmp(local->path, "ux0:data/BIG") == 0);
    CHECK(local->row_count == WORKER_LIST_CAP);
    CHECK(local->truncated == 1);
    CHECK(local->loading == 0);
    CHECK(local->error[0] == '\0');

    /* A small folder must not inherit the previous truncation badge. */
    press(app, INPUT_BIT(INPUT_CIRCLE));
    for (int i = 0; i < 40; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") ==
                0 &&
            !app_state(app)->panes[UI_PANE_LOCAL].loading)
            break;
    }
    local = &app_state(app)->panes[UI_PANE_LOCAL];
    CHECK(strcmp(local->path, "ux0:data") == 0);
    CHECK(local->truncated == 0);

    app_destroy(app);
}

static void test_log_never_contains_a_password(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    const SiteBook *book = worker_sites(app_worker(app));
    CHECK(book != NULL && site_book_count(book) == 1);

    UiCommand save;
    memset(&save, 0, sizeof(save));
    save.type = UI_CMD_SAVE_SITE;
    save.index = 0;
    if (book != NULL && site_book_count(book) == 1)
        save.site = *site_book_at(book, 0);
    snprintf(save.site.password, sizeof(save.site.password),
             "SENTINEL-PASSWORD-42");

    CHECK(handoff_command_push(app_handoff(app), &save) == 1);
    pump(app, NULL, 20);

    CHECK(fake_log_count() > 0);
    CHECK(!log_has("SENTINEL-PASSWORD-42"));
    app_destroy(app);
}

static void test_quit_with_server(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    fake_net_set_link(1, 1, 0xC0A81DC0u, 0); /* 192.168.29.192 */
    press(app, INPUT_BIT(INPUT_START));
    CHECK(app_state(app)->screen == UI_SCREEN_SERVER);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);

    CHECK(app_state(app)->server.run != UI_SERVER_OFF);
    CHECK(app_state(app)->server.address_known == 1);
    CHECK(strcmp(app_state(app)->server.address, "192.168.29.192") == 0);
    /* Start binds the control listener only; a slot's data listener binds on
     * its first PASV, so a fresh server owns exactly one. */
    CHECK(fake_net_listener_count() == 1);
    NetSocket *listener = fake_net_listener_at(0);
    CHECK(fake_net_is_open(listener) == 1);

    UiCommand quit;
    memset(&quit, 0, sizeof(quit));
    quit.type = UI_CMD_QUIT;
    CHECK(handoff_command_push(app_handoff(app), &quit) == 1);
    CHECK(app_tick(app) == 0);
    CHECK(fake_net_is_open(listener) == 0);

    app_destroy(app);
}

/* R6: the SERVER port editor refuses one past the last startable port and
 * names the range; 65531 itself starts and reaches ON with four data ports
 * inside uint16_t. */
static void test_server_port_ceiling(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    fake_net_set_link(1, 1, PEER_ADDR, 0);

    UiCommand edit;
    memset(&edit, 0, sizeof(edit));
    edit.type = UI_CMD_EDIT_PORT;
    edit.value = 65532;
    CHECK(handoff_command_push(app_handoff(app), &edit) == 1);
    pump(app, NULL, 10);
    CHECK(strstr(app_state(app)->panes[UI_PANE_REMOTE].error,
                 "PORT MUST BE 1024-65531") != NULL);
    CHECK(worker_settings(app_worker(app))->server_port == 1337);

    edit.value = 65531;
    CHECK(handoff_command_push(app_handoff(app), &edit) == 1);
    pump(app, NULL, 10);
    CHECK(worker_settings(app_worker(app))->server_port == 65531);

    press(app, INPUT_BIT(INPUT_START));
    CHECK(app_state(app)->screen == UI_SCREEN_SERVER);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);

    CHECK(app_state(app)->server.run == UI_SERVER_ON);
    CHECK(app_state(app)->server.port == 65531);
    CHECK(fake_net_listener_count() == 1);
    CHECK(fake_net_listener_addr(0).port == 65531);

    app_destroy(app);
}

/* The SERVER port field opens text entry: the typed value reaches the worker,
 * and a refused value leaves the saved port alone. */
static void test_server_port_editor(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    fake_net_set_link(1, 1, PEER_ADDR, 0);
    press(app, INPUT_BIT(INPUT_START));
    CHECK(app_state(app)->screen == UI_SCREEN_SERVER);
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->server.focus_start == 0);

    fake_ime_queue_text("1338");
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);
    CHECK(worker_settings(app_worker(app))->server_port == 1338);
    CHECK(app_state(app)->server.port == 1338);

    fake_ime_queue_text("65532");
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);
    CHECK(worker_settings(app_worker(app))->server_port == 1338);
    CHECK(strstr(app_state(app)->panes[UI_PANE_REMOTE].error,
                 "PORT MUST BE 1024-65531") != NULL);

    app_destroy(app);
}

/* The start path refuses a port the editor and the settings loader would never
 * let through, so that branch is a backstop: the test plants the value through
 * the read-only accessor (the harness drives the worker inline). A refused start
 * reports one error and never reaches ON. */
static void test_server_start_refused_port(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    fake_net_set_link(1, 1, PEER_ADDR, 0);
    Settings *settings = (Settings *)worker_settings(app_worker(app));
    settings->server_port = 65532;

    press(app, INPUT_BIT(INPUT_START));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);

    CHECK(app_state(app)->server.run == UI_SERVER_OFF);
    CHECK(fake_net_listener_count() == 0);
    CHECK(strstr(app_state(app)->panes[UI_PANE_REMOTE].error, "65532") != NULL);
    CHECK(log_count("PORT%2065532") == 1);
    CHECK(!log_has("server_start"));

    app_destroy(app);
}

static void test_local_batch_terminal(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, "ux0:data/B.TXT", "B");
    if (app == NULL)
        return;
    fake_fs_seed_file("ux0:data/C.TXT", "C", 1);
    fake_fs_add_dir("ux0:data/DEST");

    enter_ux0_data(app);
    select_local_named(app, "A.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    select_local_named(app, "B.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    select_local_named(app, "C.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_COPY);
    press(app, INPUT_BIT(INPUT_CROSS));

    enter_local_named(app, "DEST", "ux0:data/DEST");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 80);

    CHECK(fake_fs_exists("ux0:data/DEST/A.TXT"));
    CHECK(fake_fs_exists("ux0:data/DEST/B.TXT"));
    CHECK(fake_fs_exists("ux0:data/DEST/C.TXT"));
    CHECK(app_state(app)->local_dock.state == UI_DOCK_DONE);
    CHECK(app_state(app)->local_dock.item_total == 3);
    CHECK(app_state(app)->local_dock.item_index == 3);
    CHECK(app_state(app)->local_dock.skipped == 0);
    CHECK(app_state(app)->local_dock.failed == 0);
    CHECK(app_state(app)->dock_primary == UI_DOCK_LANE_LOCAL);

    app_destroy(app);
}

/* A refused item still counts: the terminal must report 2 copied and 1
 * failed, not three copied. The refusal happens before run_plan, so the
 * advance step is the only place that can record it. */
static void test_local_batch_refused_item(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, "ux0:data/B.TXT", "B");
    if (app == NULL)
        return;
    fake_fs_seed_file("ux0:data/C.TXT", "C", 1);
    fake_fs_add_dir("ux0:data/DEST");

    enter_ux0_data(app);
    select_local_named(app, "A.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    select_local_named(app, "B.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    select_local_named(app, "C.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_CROSS));

    enter_local_named(app, "DEST", "ux0:data/DEST");
    FsPort *fs = fake_fs_port();
    CHECK(fs->remove(fs->ctx, "ux0:data/B.TXT") == FS_OK);
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 120);

    CHECK(fake_fs_exists("ux0:data/DEST/A.TXT"));
    CHECK(!fake_fs_exists("ux0:data/DEST/B.TXT"));
    CHECK(fake_fs_exists("ux0:data/DEST/C.TXT"));
    CHECK(app_state(app)->local_dock.state == UI_DOCK_DONE);
    CHECK(app_state(app)->local_dock.item_total == 3);
    CHECK(app_state(app)->local_dock.item_index == 2);
    CHECK(app_state(app)->local_dock.failed == 1);
    CHECK(app_state(app)->local_dock.skipped == 0);

    app_destroy(app);
}

static void test_local_batch_preflight(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, "ux0:data/B.TXT", "B");
    if (app == NULL)
        return;
    fake_fs_seed_file("ux0:data/C.TXT", "C", 1);
    fake_fs_add_dir("ux0:data/DEST");
    fake_fs_seed_file("ux0:data/DEST/A.TXT", "OLD-A", 5);
    fake_fs_seed_file("ux0:data/DEST/B.TXT", "OLD-B", 5);

    enter_ux0_data(app);
    select_local_named(app, "A.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    select_local_named(app, "B.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    select_local_named(app, "C.TXT");
    press(app, INPUT_BIT(INPUT_SQUARE));
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_COPY);
    press(app, INPUT_BIT(INPUT_CROSS));

    enter_local_named(app, "DEST", "ux0:data/DEST");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 30);

    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);
    CHECK(app_state(app)->confirm.item_count == 2);
    CHECK(app_state(app)->confirm.existing_count == 2);
    if (app_state(app)->confirm.item_count == 2) {
        CHECK(strcmp(app_state(app)->confirm.items[0].name,
                     "ux0:data/DEST/A.TXT") == 0);
        CHECK(strcmp(app_state(app)->confirm.items[1].name,
                     "ux0:data/DEST/B.TXT") == 0);
    }

    /* CANCEL, SKIP, OVERWRITE: move to SKIP and confirm. */
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 60);

    CHECK(content_is("ux0:data/DEST/A.TXT", "OLD-A"));
    CHECK(content_is("ux0:data/DEST/B.TXT", "OLD-B"));
    CHECK(fake_fs_exists("ux0:data/DEST/C.TXT"));
    CHECK(app_state(app)->local_dock.item_total == 3);
    CHECK(app_state(app)->local_dock.item_index == 1);
    CHECK(app_state(app)->local_dock.skipped == 2);
    CHECK(app_state(app)->local_dock.failed == 0);

    app_destroy(app);
}

static void test_local_conflict_prompt(void)
{
    App *app = make_app_ex("ux0:data/A.TXT", "A", NULL, NULL, NULL);
    if (app == NULL)
        return;
    fake_fs_add_dir("ux0:data/DEST");
    fake_fs_seed_file("ux0:data/DEST/A.TXT", "OLD", 3);

    enter_ux0_data(app);
    select_local_named(app, "A.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_COPY);
    press(app, INPUT_BIT(INPUT_CROSS));

    enter_local_named(app, "DEST", "ux0:data/DEST");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_PASTE);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 30);

    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);
    CHECK(app_state(app)->confirm.item_count == 1);
    if (app_state(app)->confirm.item_count == 1) {
        CHECK(strcmp(app_state(app)->confirm.items[0].name,
                     "ux0:data/DEST/A.TXT") == 0);
    }

    /* CANCEL, SKIP, OVERWRITE: move to SKIP and confirm. */
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 40);

    CHECK(content_is("ux0:data/DEST/A.TXT", "OLD"));
    CHECK(app_state(app)->local_dock.item_total == 1);
    CHECK(app_state(app)->local_dock.skipped == 1);
    CHECK(app_state(app)->local_dock.failed == 0);

    app_destroy(app);
}

/* A worker confirmation raised while a local dialog owns the screen waits for
 * that dialog instead of replacing it or vanishing. */
static void test_deferred_worker_confirmation(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "UP", NULL, "ux0:data/DEL.TXT",
                           "DEL");
    if (app == NULL)
        return;

    FakePeer *peer = peer_new();
    connect_site(app, peer);
    fake_peer_set_exists_path(peer, "/UP.TXT");
    fake_peer_set_exists_size(peer, 2);

    enter_ux0_data(app);
    select_local_named(app, "DEL.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_DELETE);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_DELETE);

    /* Queue the upload behind the open dialog. */
    UiCommand up;
    memset(&up, 0, sizeof(up));
    up.type = UI_CMD_TRANSFER;
    up.pane = UI_PANE_LOCAL;
    snprintf(up.path, sizeof(up.path), "ux0:data/UP.TXT");
    snprintf(up.path2, sizeof(up.path2), "/");
    up.total = 2;
    up.total_known = 1;
    CHECK(handoff_command_push(app_handoff(app), &up) == 1);
    pump(app, peer, 80);

    /* The worker prompt waits; the local dialog still owns the screen. */
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_DELETE);

    /* Answer the local dialog with CANCEL; the deferred prompt appears. */
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 80);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);

    press(app, INPUT_BIT(INPUT_RIGHT)); /* SKIP */
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, peer, 40);
    CHECK(fake_peer_stor_count(peer) == 0);

    fake_peer_destroy(peer);
    app_destroy(app);
}

static void test_local_rename_conflict(void)
{
    App *app = make_app_ex("ux0:data/OLD.TXT", "OLD", NULL, NULL, NULL);
    if (app == NULL)
        return;
    fake_fs_seed_file("ux0:data/NEW.TXT", "NEW", 3);
    enter_ux0_data(app);

    select_local_named(app, "OLD.TXT");
    fake_ime_queue_text("NEW.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 4; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_RENAME);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);
    press(app, INPUT_BIT(INPUT_RIGHT)); /* SKIP */
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 40);

    CHECK(fake_fs_exists("ux0:data/OLD.TXT"));
    CHECK(content_is("ux0:data/NEW.TXT", "NEW"));
    CHECK(app_state(app)->local_dock.skipped == 1);
    CHECK(app_state(app)->local_dock.failed == 0);

    select_local_named(app, "OLD.TXT");
    fake_ime_queue_text("NEW.TXT");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 4; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT)); /* OVERWRITE */
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 40);

    CHECK(!fake_fs_exists("ux0:data/OLD.TXT"));
    CHECK(content_is("ux0:data/NEW.TXT", "OLD"));
    CHECK(app_state(app)->local_dock.failed == 0);

    app_destroy(app);
}

int main(void)
{
    test_download_destination_failure();
    test_two_lanes_progress();
    test_two_cancel_presses();
    test_wait_resume_rechecks();
    test_connect_list_download();
    test_app_listing_slot_held();
    test_aside_debris_row();
    test_remote_navigation();
    test_remote_mutations();
    test_remote_disconnect_and_reconnect();
    test_remote_unsafe_name();
    test_remote_volume_root_open();
    test_remote_refresh();
    test_upload_conflict();
    test_upload_preflight_unsupported();
    test_upload_preflight_size_fallback();
    test_upload_preflight_directory();
    test_upload_preflight_failure();
    test_upload_preflight_refresh_folder();
    test_upload_preflight_refresh_not_needed();
    test_upload_preflight_refresh_not_for_listed_file();
    test_upload_preflight_refresh_names_its_own_item();
    test_upload_preflight_refresh_parent_not_shown();
    test_upload_preflight_refresh_refuses_cancel();
    test_upload_preflight_refresh_failure();
    test_upload_preflight_refresh_once_per_item();
    test_download_conflict();
    test_download_final_reply_loses_queue();
    test_listing_cap();
    test_local_file_actions();
    test_local_new_folder_without_selection();
    test_clipboard_copy();
    test_clipboard_move();
    test_local_batch_terminal();
    test_local_conflict_prompt();
    test_local_rename_conflict();
    test_local_rename_aside_notice();
    test_upload_preflight_size_folder();
    test_deferred_worker_confirmation();
    test_local_batch_preflight();
    test_local_batch_refused_item();
    test_delete_confirm_fresh();
    test_delete_inside_tai_and_protected();
    test_cancel();
    test_save_failure();
    test_log_never_contains_a_password();
    test_shutdown_cancels();
    test_quit_with_server();
    test_server_port_ceiling();
    test_server_port_editor();
    test_server_start_refused_port();

    if (s_failures != 0) {
        fprintf(stderr, "test_integration: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_integration: OK\n");
    return 0;
}
