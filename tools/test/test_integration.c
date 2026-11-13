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

/* Scripted MLST answers for the upload preflight. */
enum {
    MLST_NORMAL = 0,
    MLST_UNSUPPORTED,
    MLST_DIR,
    MLST_FAIL
};

typedef struct {
    FakeFtpd server;
    NetSocket *control;
    NetSocket *data;
    const char *listing;
    const char *sub_listing;
    const char *payload;
    const char *exists_path;
    uint64_t exists_size;
    int mlst_mode;
    NetSocket *stor_data;
    char last_stor[256];
    char cwd[64];
    int hold_data;
    int listings;
    int retrs;
    int aborts;
    int cwds;
    int mkds;
    int deles;
    int rmds;
    int renames;
    int stors;
} Peer;

static void peer_init(Peer *peer, const char *listing, const char *payload)
{
    memset(peer, 0, sizeof(*peer));
    peer->control = fake_net_last_connected();
    peer->listing = listing;
    peer->payload = payload;
    snprintf(peer->cwd, sizeof(peer->cwd), "/");
    fake_ftpd_init(&peer->server, peer->control);
    fake_ftpd_reply(&peer->server, 220, "Welcome.");
}

static void peer_serve(Peer *peer)
{
    char line[512];

    if (peer->stor_data != NULL) {
        char sink[512];
        while (fake_net_read_tx(peer->stor_data, sink, sizeof(sink)) > 0) {
        }
        if (!fake_net_is_open(peer->stor_data)) {
            fake_ftpd_reply(&peer->server, 226, "Transfer complete.");
            peer->stor_data = NULL;
        }
    }

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
        } else if (strncmp(line, "CWD ", 4) == 0) {
            snprintf(peer->cwd, sizeof(peer->cwd), "%s", line + 4);
            peer->cwds++;
            fake_ftpd_reply(&peer->server, 250, "Directory changed.");
        } else if (strncmp(line, "MKD ", 4) == 0) {
            peer->mkds++;
            fake_ftpd_reply(&peer->server, 257, "Created.");
        } else if (strncmp(line, "DELE ", 5) == 0) {
            peer->deles++;
            fake_ftpd_reply(&peer->server, 250, "Deleted.");
        } else if (strncmp(line, "RMD ", 4) == 0) {
            peer->rmds++;
            fake_ftpd_reply(&peer->server, 250, "Removed.");
        } else if (strncmp(line, "RNFR ", 5) == 0) {
            peer->renames++;
            fake_ftpd_reply(&peer->server, 350, "Ready for RNTO.");
        } else if (strncmp(line, "RNTO ", 5) == 0) {
            fake_ftpd_reply(&peer->server, 250, "Renamed.");
        } else if (strncmp(line, "NOOP", 4) == 0) {
            fake_ftpd_reply(&peer->server, 200, "Still here.");
        } else if (strncmp(line, "MLST ", 5) == 0) {
            const char *path = line + 5;
            if (peer->mlst_mode == MLST_UNSUPPORTED) {
                fake_ftpd_reply(&peer->server, 502, "Not implemented.");
            } else if (peer->mlst_mode == MLST_FAIL) {
                fake_ftpd_reply(&peer->server, 421,
                                "Service not available.");
            } else if (peer->mlst_mode == MLST_DIR) {
                char reply_text[640];
                snprintf(reply_text, sizeof(reply_text),
                         "250-Listing %s\r\n"
                         " type=dir; %s\r\n"
                         "250 End\r\n",
                         path, path);
                fake_ftpd_send_raw(&peer->server, reply_text);
            } else if (peer->exists_path != NULL &&
                       strcmp(path, peer->exists_path) == 0) {
                char reply_text[640];
                snprintf(reply_text, sizeof(reply_text),
                         "250-Listing %s\r\n"
                         " type=file;size=%llu; %s\r\n"
                         "250 End\r\n",
                         path, (unsigned long long)peer->exists_size, path);
                fake_ftpd_send_raw(&peer->server, reply_text);
            } else {
                fake_ftpd_reply(&peer->server, 550, "Not found.");
            }
        } else if (strncmp(line, "STOR ", 5) == 0) {
            snprintf(peer->last_stor, sizeof(peer->last_stor), "%s",
                     line + 5);
            peer->stors++;
            peer->stor_data = fake_net_last_connected();
            if (peer->stor_data == peer->control)
                peer->stor_data = NULL;
            fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
        } else if (strncmp(line, "MLSD ", 5) == 0 ||
                   strncmp(line, "LIST ", 5) == 0) {
            const char *listing = peer->listing;
            if (strcmp(peer->cwd, "/") != 0 && peer->sub_listing != NULL)
                listing = peer->sub_listing;
            peer->data = fake_net_last_connected();
            if (peer->data == peer->control)
                peer->data = NULL;
            fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
            if (listing != NULL && peer->data != NULL) {
                fake_net_queue_rx(peer->data, listing, strlen(listing));
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

static void pump(App *app, Peer *peer, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        (void)app_tick(app);
        if (peer != NULL)
            peer_serve(peer);
    }
}

static void connect_site_listing(App *app, Peer *peer, const char *listing,
                                 const char *payload)
{
    fake_net_set_resolve(PEER_ADDR, 0);
    fake_net_set_link(1, 1, PEER_ADDR, 0);

    press(app, INPUT_BIT(INPUT_SELECT));
    CHECK(app_state(app)->screen == UI_SCREEN_SITES);
    press(app, INPUT_BIT(INPUT_CROSS));
    (void)app_tick(app);

    peer_init(peer, listing, payload);

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(peer);
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

static void connect_site(App *app, Peer *peer)
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
    press(app, INPUT_BIT(INPUT_SQUARE));
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

static void test_delete_high_risk_and_protected(void)
{
    App *app = make_app_ex("ur0:tai/x.suprx", "X", NULL,
                           "ur0:tai/boot_config.txt", "CONFIG");
    if (app == NULL)
        return;

    pump(app, NULL, 10);
    enter_local_named(app, "ur0:", "ur0:");
    enter_local_named(app, "tai", "ur0:tai");

    /* A normal high-risk path shows the delete list and needs a second step. */
    select_local_named(app, "x.suprx");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 5; ++i)
        press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_DELETE);
    CHECK(app_state(app)->confirm.high_risk == 1);
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

    Peer peer;
    connect_site_listing(app, &peer, root, NULL);
    peer.sub_listing = sub;
    select_remote(app);

    /* At the remote root there is no ".." row; SUB is row 0. */
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        UiPaneState *r = &app_state(app)->panes[UI_PANE_REMOTE];
        if (!r->loading && strcmp(r->path, "/SUB") == 0 &&
            r->row_count >= 2)
            break;
    }

    UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
    CHECK(peer.cwds == 1);
    CHECK(remote->loading == 0);
    CHECK(strcmp(remote->path, "/SUB") == 0);
    CHECK(remote->row_count >= 2);
    CHECK(remote->rows != NULL);
    if (remote->rows != NULL)
        CHECK(strcmp(remote->rows[1].name, "IN.TXT") == 0);

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

    Peer peer;
    connect_site_listing(app, &peer, root, NULL);
    select_remote(app);

    /* NEW FOLDER: cursor on ".." is no selection; the folder is the target. */
    fake_ime_queue_text("NEWDIR");
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN)); /* DOWNLOAD -> NEW FOLDER */
    CHECK(app_state(app)->actions.selected == UI_ACTION_NEW_FOLDER);
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, &peer, 80);
    CHECK(peer.mkds == 1);
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
    pump(app, &peer, 80);
    CHECK(peer.renames == 1);
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
    pump(app, &peer, 80);
    CHECK(peer.rmds == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    app_destroy(app);
}

static void test_remote_disconnect_and_reconnect(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    Peer peer;
    connect_site(app, &peer);
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
    CHECK(fake_net_is_open(peer.control) == 0);

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

    Peer again;
    peer_init(&again, listing, "HELLO");
    pump(app, &again, 120);
    CHECK(app_state(app)->panes[UI_PANE_REMOTE].connected == 1);
    CHECK(again.listings >= 1);
    CHECK(again.cwds == 0); /* the first listing needs no CWD */

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

    Peer peer;
    connect_site_listing(app, &peer, root, NULL);
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
        app_destroy(app);
        return;
    }
    CHECK(remote->rows[unsafe_index].unsafe == 1);

    remote->selected = unsafe_index;
    press(app, INPUT_BIT(INPUT_CROSS));
    CHECK(strcmp(app_state(app)->status, "UNSAFE NAME") == 0);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    CHECK(app_state(app)->actions.selected == UI_ACTION_DOWNLOAD);
    CHECK(app_state(app)->actions.enabled[UI_ACTION_DOWNLOAD] == 0);
    CHECK(strcmp(app_state(app)->actions.reason[UI_ACTION_DOWNLOAD],
                 "UNSAFE NAME") == 0);

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

    Peer peer;
    connect_site_listing(app, &peer, root, NULL);
    select_remote(app);

    UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
    int target = -1;
    for (int i = 0; i < remote->row_count; ++i) {
        if (strcmp(remote->rows[i].name, "UX0:") == 0)
            target = i;
    }
    CHECK(target >= 0);
    if (target < 0) {
        app_destroy(app);
        return;
    }

    remote->selected = target;
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (peer.cwds > 0)
            break;
    }
    CHECK(peer.cwds == 1);
    CHECK(strcmp(peer.cwd, "/UX0:") == 0);

    app_destroy(app);
}

static void test_remote_refresh(void)
{
    App *app = make_app();
    if (app == NULL)
        return;

    Peer peer;
    connect_site(app, &peer);
    select_remote(app);

    static const char *fresh =
        "type=file;size=7;modify=20240101000000; NEW.TXT\r\n"
        "type=file;size=9;modify=20240101000000; OTHER.TXT\r\n";
    peer.listing = fresh;

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    for (int i = 0; i < 4; ++i) /* DOWNLOAD -> REFRESH */
        press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_REFRESH);
    press(app, INPUT_BIT(INPUT_CROSS));

    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
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

    Peer peer;
    connect_site(app, &peer);
    peer.exists_path = "/UP.TXT";
    peer.exists_size = 5;

    /* First attempt: the remote file exists, so the batch asks. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);
    press(app, INPUT_BIT(INPUT_RIGHT)); /* SKIP */
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, &peer, 40);
    CHECK(peer.stors == 0);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    /* Second attempt: OVERWRITE. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT)); /* OVERWRITE */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 160; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (peer.stors > 0 && app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }
    CHECK(peer.stors == 1);
    CHECK(strcmp(peer.last_stor, "/UP.TXT") == 0);

    /* Let the post-upload remote refresh settle before the next action. */
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
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
        peer_serve(&peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_CROSS)); /* CANCEL is focused first */
    pump(app, &peer, 40);
    CHECK(peer.stors == 1);
    /* CANCEL must be observed as a cancellation, not look like SKIP. */
    CHECK(app_state(app)->dock.state == UI_DOCK_IDLE);
    CHECK(strcmp(app_state(app)->status, "CANCELLED") == 0);

    /* CANCEL cleared the batch latch: a new attempt asks again. */
    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_CIRCLE));

    app_destroy(app);
}

static void start_upload(App *app, Peer *peer)
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

    Peer peer;
    connect_site(app, &peer);
    peer.mlst_mode = MLST_UNSUPPORTED;

    /* A server that does not implement MLST keeps the old direct STOR. */
    start_upload(app, &peer);
    CHECK(peer.stors == 1);
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);

    app_destroy(app);
}

static void test_upload_preflight_directory(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    Peer peer;
    connect_site(app, &peer);
    peer.mlst_mode = MLST_DIR;

    start_upload(app, &peer);
    CHECK(peer.stors == 0);
    CHECK(strstr(app_state(app)->status, "DESTINATION IS A FOLDER") != NULL);

    app_destroy(app);
}

static void test_upload_preflight_failure(void)
{
    App *app = make_app_ex("ux0:data/UP.TXT", "HELLO", NULL, NULL, NULL);
    if (app == NULL)
        return;

    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    Peer peer;
    connect_site(app, &peer);
    peer.mlst_mode = MLST_FAIL;

    start_upload(app, &peer);
    CHECK(peer.stors == 0);
    CHECK(strstr(app_state(app)->status, "REMOTE CHECK FAILED") != NULL);

    app_destroy(app);
}

static void test_download_conflict(void)
{
    App *app = make_app_ex("ux0:data/TEST.BIN", "OLD!!", NULL, NULL, NULL);
    if (app == NULL)
        return;

    pump(app, NULL, 10);
    Peer peer;
    connect_site(app, &peer);
    select_remote(app);

    /* Rows are .., BIG.BIN, TEST.BIN. */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    CHECK(app_state(app)->confirm.kind == UI_CONFIRM_CONFLICT);

    /* SKIP keeps the local bytes and starts no RETR. */
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    pump(app, &peer, 60);
    CHECK(peer.retrs == 0);
    CHECK(content_is("ux0:data/TEST.BIN", "OLD!!"));

    /* OVERWRITE runs the RETR and replaces the destination. */
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 120; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (app_state(app)->screen == UI_SCREEN_CONFIRM)
            break;
    }
    CHECK(app_state(app)->screen == UI_SCREEN_CONFIRM);
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_RIGHT));
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < 160; ++i) {
        (void)app_tick(app);
        peer_serve(&peer);
        if (peer.retrs > 0 && app_state(app)->dock.state == UI_DOCK_DONE)
            break;
    }
    CHECK(peer.retrs == 1);
    CHECK(content_is("ux0:data/TEST.BIN", "HELLO"));

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
    test_remote_navigation();
    test_remote_mutations();
    test_remote_disconnect_and_reconnect();
    test_remote_unsafe_name();
    test_remote_volume_root_open();
    test_remote_refresh();
    test_upload_conflict();
    test_upload_preflight_unsupported();
    test_upload_preflight_directory();
    test_upload_preflight_failure();
    test_download_conflict();
    test_listing_cap();
    test_local_file_actions();
    test_local_new_folder_without_selection();
    test_clipboard_copy();
    test_clipboard_move();
    test_delete_confirm_fresh();
    test_delete_high_risk_and_protected();
    test_cancel();
    test_save_failure();
    test_log_never_contains_a_password();
    test_shutdown_cancels();
    test_quit_with_server();

    if (s_failures != 0) {
        fprintf(stderr, "test_integration: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_integration: OK\n");
    return 0;
}
