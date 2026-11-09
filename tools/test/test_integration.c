/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "core/handoff.h"
#include "fake_clock.h"
#include "fake_fs.h"
#include "fake_ftpd.h"
#include "fake_ime.h"
#include "fake_input.h"
#include "fake_log.h"
#include "fake_net.h"
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

typedef struct {
    FakeFtpd server;
    NetSocket *control;
    NetSocket *data;
    const char *listing;
    const char *payload;
    int hold_data;
    int listings;
    int retrs;
    int aborts;
} Peer;

static void peer_init(Peer *peer, const char *listing, const char *payload)
{
    memset(peer, 0, sizeof(*peer));
    peer->control = fake_net_last_connected();
    peer->listing = listing;
    peer->payload = payload;
    fake_ftpd_init(&peer->server, peer->control);
    fake_ftpd_reply(&peer->server, 220, "Welcome.");
}

static void peer_serve(Peer *peer)
{
    char line[512];
    while (fake_ftpd_next_command(&peer->server, line, sizeof(line)) == 1) {
        if (strcmp(line, "USER anonymous") == 0 ||
            strcmp(line, "USER bob") == 0) {
            fake_ftpd_reply(&peer->server, 331, "Password required.");
        } else if (strncmp(line, "PASS ", 5) == 0) {
            fake_ftpd_reply(&peer->server, 230, "Logged in.");
        } else if (strcmp(line, "TYPE I") == 0) {
            fake_ftpd_reply(&peer->server, 200, "Type set.");
        } else if (strcmp(line, "PWD") == 0) {
            fake_ftpd_reply(&peer->server, 257,
                            "\"/\" is the current directory.");
        } else if (strcmp(line, "FEAT") == 0) {
            fake_ftpd_send_raw(&peer->server,
                               "211-Features:\r\n"
                               " MLST type*;size*;modify*;perm*;\r\n"
                               "211 End\r\n");
        } else if (strcmp(line, "PASV") == 0) {
            fake_ftpd_reply(&peer->server, 227,
                            "Entering Passive Mode (192,168,1,42,19,140).");
            peer->data = NULL;
        } else if (strcmp(line, "MLSD /") == 0 || strcmp(line, "LIST /") == 0) {
            peer->data = fake_net_last_connected();
            if (peer->data == peer->control)
                peer->data = NULL;
            fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
            if (peer->listing != NULL && peer->data != NULL) {
                fake_net_queue_rx(peer->data, peer->listing,
                                  strlen(peer->listing));
                fake_net_shutdown(peer->data);
            }
            fake_ftpd_reply(&peer->server, 226, "Transfer complete.");
            peer->listings++;
            peer->data = NULL;
        } else if (strncmp(line, "RETR ", 5) == 0) {
            peer->data = fake_net_last_connected();
            if (peer->data == peer->control)
                peer->data = NULL;
            fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
            peer->retrs++;
            if (!peer->hold_data && peer->payload != NULL &&
                peer->data != NULL) {
                fake_net_queue_rx(peer->data, peer->payload,
                                  strlen(peer->payload));
                fake_net_shutdown(peer->data);
                fake_ftpd_reply(&peer->server, 226, "Transfer complete.");
                peer->data = NULL;
            }
        } else if (strcmp(line, "ABOR") == 0) {
            fake_ftpd_reply(&peer->server, 426, "Transfer aborted.");
            fake_ftpd_reply(&peer->server, 226, "Abort successful.");
            peer->aborts++;
        } else if (strcmp(line, "QUIT") == 0) {
            fake_ftpd_reply(&peer->server, 221, "Bye.");
        } else {
            fake_ftpd_reply(&peer->server, 200, "OK.");
        }
    }
}

static App *make_app(void)
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

static void press(App *app, uint32_t button)
{
    fake_input_set(button, 0, 0, 0);
    (void)app_tick(app);
}

static void pump(App *app, Peer *peer, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        (void)app_tick(app);
        if (peer != NULL)
            peer_serve(peer);
    }
}

static void connect_site(App *app, Peer *peer)
{
    fake_net_set_resolve(PEER_ADDR, 0);
    fake_net_set_link(1, 1, PEER_ADDR, 0);

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->screen == UI_SCREEN_SITES);
    press(app, INPUT_BIT(INPUT_CROSS));
    (void)app_tick(app);

    static const char *listing =
        "type=file;size=5;modify=20240101000000; TEST.BIN\r\n"
        "type=file;size=100;modify=20240101000000; BIG.BIN\r\n";
    peer_init(peer, listing, "HELLO");

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(peer);
        if (app_state(app)->panes[UI_PANE_REMOTE].connected &&
            app_state(app)->panes[UI_PANE_REMOTE].row_count >= 2)
            break;
    }
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].connected == 1);
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].row_count >= 2);

    /* Leave SITES so L/R and the workspace keys apply. */
    press(app, INPUT_BIT(INPUT_CIRCLE));
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
}

static void select_remote(App *app)
{
    press(app, INPUT_BIT(INPUT_L1));
    CHECK(app_state(app)->active_pane == UI_PANE_REMOTE);
}

static int content_is(const char *path, const char *expected)
{
    char buf[64];
    size_t n = fake_fs_read_file(path, buf, sizeof(buf));
    return n == strlen(expected) && memcmp(buf, expected, n) == 0;
}

static void test_connect_list_download(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    Peer peer;
    connect_site(app, &peer);
    select_remote(app);

    /* Rows are .., BIG.BIN, TEST.BIN; select TEST.BIN. */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_SQUARE));
    CHECK(ui_state_mark_count(app_state(app), UI_PANE_REMOTE) == 1);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }

    CHECK(app_state(app)->dock.state == UI_DOCK_DONE);
    CHECK(content_is("ux0:data/TEST.BIN", "HELLO"));
    CHECK(!fake_fs_exists("ux0:data/TEST.BIN.vftp.part"));

    app_destroy(app);
}

static void test_cancel(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    Peer peer;
    connect_site(app, &peer);
    select_remote(app);

    /* Select BIG.BIN directly (row 1). */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));

    peer.hold_data = 1;
    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (peer.retrs > 0 && app_state(app)->dock.active)
            break;
    }
    CHECK(peer.retrs > 0);
    CHECK(app_state(app)->dock.active == 1);

    /* Down to the last row, then into the dock, Cross, Right, Cross. */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->dock.focused == 1);
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));

    pump(app, &peer, 40);
    CHECK(peer.aborts > 0);
    CHECK(!fake_fs_exists("ux0:data/BIG.BIN"));
    CHECK(!fake_fs_exists("ux0:data/BIG.BIN.vftp.part"));
    CHECK(app_state(app)->dock.state == UI_DOCK_IDLE);

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

    Peer peer;
    connect_site(app, &peer);
    select_remote(app);

    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    peer.hold_data = 1;
    for (int i = 0; i < 80; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (peer.retrs > 0)
            break;
    }
    CHECK(peer.retrs > 0);

    UiCommand quit;
    memset(&quit, 0, sizeof(quit));
    quit.type = UI_CMD_QUIT;
    CHECK(handoff_command_push(app_handoff(app), &quit) == 1);
    CHECK(app_tick(app) == 0);

    CHECK(!fake_fs_exists("ux0:data/BIG.BIN"));
    CHECK(!fake_fs_exists("ux0:data/BIG.BIN.vftp.part"));

    app_destroy(app);
}

static void test_quit_with_server(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    fake_net_set_link(1, 1, PEER_ADDR, 0);
    press(app, INPUT_BIT(INPUT_START));
    CHECK(app_state(app)->screen == UI_SCREEN_SERVER);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, NULL, 20);

    CHECK(app_state(app)->server.run != UI_SERVER_OFF);
    CHECK(fake_net_listener_count() >= 2);
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

int main(void)
{
    test_connect_list_download();
    test_cancel();
    test_save_failure();
    test_shutdown_cancels();
    test_quit_with_server();

    if (s_failures != 0) {
        fprintf(stderr, "test_integration: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_integration: OK\n");
    return 0;
}
