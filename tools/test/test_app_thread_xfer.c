/* SPDX-License-Identifier: GPL-3.0-only */

#define _POSIX_C_SOURCE 200809L

/*
 * The real app with its worker on a pthread, driven through a download, the
 * SIZE-only refresh refusal, a cancel, and a server session. This is separate
 * from test_app_thread: the fakes are process-wide singletons and this binary
 * gives the scripted peer its own thread.
 *
 * With the stock fake clock the worker advances time by WORKER_TICK_MS on
 * every step, far faster than wall time: a deadline of a few seconds of fake
 * time can pass while one frame renders. A peer that answered only between
 * the app thread's frames could then miss a reply window the worker had
 * already closed. This binary gives the peer its own thread and feeds the app
 * a clock that advances with wall time, so the worker cannot outrun the peer
 * by more than the protocol's own margin. The app thread only ticks, reads
 * UiState, and presses keys; every wait is a bounded tick or spin loop, never
 * a sleep (KTD6).
 */

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app/app.h"
#include "core/handoff.h"
#include "fake_clock.h"
#include "fake_ftp_client.h"
#include "fake_fs.h"
#include "fake_ftpd.h"
#include "fake_ime.h"
#include "fake_input.h"
#include "fake_log.h"
#include "fake_net.h"

static int s_failures;

/*
 * The worker's real cadence is 8 ms of fake time per step, but the host worker
 * runs as fast as the CPU allows: a deadline of a few seconds of fake time can
 * pass while one frame renders. The peer thread would then answer a command
 * whose reply window the worker had already closed. This binary therefore
 * feeds the app a clock that advances with wall time, so the worker cannot
 * outrun the peer by more than the protocol's own margin.
 */
static uint64_t s_clock_start_ns;

static uint64_t paced_now(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return ns > s_clock_start_ns ? (ns - s_clock_start_ns) / 1000000ull : 0;
}

static void paced_sleep(void *ctx, unsigned ms)
{
    (void)ctx;
    (void)ms;
}

static ClockPort s_clock = {NULL, paced_now, paced_sleep};

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

#define PEER_ADDR 0xC0A8012Au /* 192.168.1.42 */
#define PEER_PORT 21
#define SERVER_CONTROL_PORT 1337
#define SERVER_DATA_PORT 1338
#define TICK_LIMIT 4000
#define PEER_SPIN_LIMIT 5000000u

enum {
    MLST_NORMAL = 0,
    MLST_UNSUPPORTED
};

typedef struct {
    const char *listing;
    /* The second listing the upload preflight asks for; NULL serves `listing`
     * again. */
    const char *refresh_listing;
    const char *payload;
    const char *exists_path;
    uint64_t exists_size;
    int mlst_mode;
    int size_supported;
    int hold_data;
    int dir_on_refresh;
} PeerConfig;

typedef struct {
    PeerConfig cfg;
    FakeFtpd server;
    NetSocket *control;
    NetSocket *data;
    NetSocket *stor_data;
    int listings;
    int sizes;
    int retrs;
    int aborts;
    int stors;
    int stop;
} Peer;

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
        if (strcmp(line, "USER anonymous") == 0) {
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
                               " MLST type*;size*;modify*;\r\n"
                               "211 End\r\n");
        } else if (strcmp(line, "PASV") == 0) {
            fake_ftpd_reply(&peer->server, 227,
                            "Entering Passive Mode (192,168,1,42,19,140).");
            peer->data = NULL;
        } else if (strncmp(line, "CWD ", 4) == 0) {
            fake_ftpd_reply(&peer->server, 250, "Directory changed.");
        } else if (strncmp(line, "NOOP", 4) == 0) {
            fake_ftpd_reply(&peer->server, 200, "Still here.");
        } else if (strncmp(line, "MLST ", 5) == 0) {
            const char *path = line + 5;
            if (peer->cfg.mlst_mode == MLST_UNSUPPORTED) {
                fake_ftpd_reply(&peer->server, 502, "Not implemented.");
            } else if (peer->cfg.exists_path != NULL &&
                       strcmp(path, peer->cfg.exists_path) == 0) {
                char reply_text[640];
                snprintf(reply_text, sizeof(reply_text),
                         "250-Listing %s\r\n"
                         " type=file;size=%llu; %s\r\n"
                         "250 End\r\n",
                         path, (unsigned long long)peer->cfg.exists_size, path);
                fake_ftpd_send_raw(&peer->server, reply_text);
            } else {
                fake_ftpd_reply(&peer->server, 550, "Not found.");
            }
        } else if (strncmp(line, "STOR ", 5) == 0) {
            peer->stors++;
            peer->stor_data = fake_net_last_connected();
            if (peer->stor_data == peer->control)
                peer->stor_data = NULL;
            fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
        } else if (strncmp(line, "MLSD ", 5) == 0 ||
                   strncmp(line, "LIST ", 5) == 0) {
            const char *listing = peer->cfg.listing;
            if (peer->cfg.dir_on_refresh && peer->listings >= 1)
                listing = peer->cfg.refresh_listing;
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
        } else if (strncmp(line, "SIZE ", 5) == 0) {
            const char *path = line + 5;
            peer->sizes++;
            if (peer->cfg.size_supported && peer->cfg.exists_path != NULL &&
                strcmp(path, peer->cfg.exists_path) == 0) {
                char size[32];
                snprintf(size, sizeof(size), "%llu",
                         (unsigned long long)peer->cfg.exists_size);
                fake_ftpd_reply(&peer->server, 213, size);
            } else if (peer->cfg.size_supported) {
                fake_ftpd_reply(&peer->server, 550, "Not found.");
            } else {
                fake_ftpd_reply(&peer->server, 502, "Not implemented.");
            }
        } else if (strncmp(line, "RETR ", 5) == 0) {
            peer->data = fake_net_last_connected();
            if (peer->data == peer->control)
                peer->data = NULL;
            fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
            peer->retrs++;
            if (!peer->cfg.hold_data && peer->cfg.payload != NULL &&
                peer->data != NULL) {
                fake_net_queue_rx(peer->data, peer->cfg.payload,
                                  strlen(peer->cfg.payload));
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

static void peer_begin(Peer *peer, NetSocket *control)
{
    peer->control = control;
    fake_ftpd_init(&peer->server, control);
    fake_ftpd_reply(&peer->server, 220, "Welcome.");
}

static void *peer_thread_main(void *arg)
{
    Peer *peer = arg;
    while (!__atomic_load_n(&peer->stop, __ATOMIC_ACQUIRE)) {
        if (peer->control == NULL) {
            NetSocket *control = fake_net_last_connected();
            if (control != NULL)
                peer_begin(peer, control);
        } else {
            peer_serve(peer);
        }
        sched_yield();
    }
    return NULL;
}

static App *make_app(const char *seed_path, const char *seed_text)
{
    fake_net_reset();
    fake_fs_reset();
    fake_clock_reset();
    fake_input_reset();
    fake_ime_reset();
    fake_log_reset();

    struct timespec base;
    if (clock_gettime(CLOCK_MONOTONIC, &base) == 0)
        s_clock_start_ns =
            (uint64_t)base.tv_sec * 1000000000ull + (uint64_t)base.tv_nsec;
    else
        s_clock_start_ns = 0;

    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ur0:");
    fake_fs_add_dir("ur0:tai");
    fake_fs_add_dir("uma0:");
    if (seed_path != NULL && seed_text != NULL)
        fake_fs_seed_file(seed_path, seed_text, strlen(seed_text));
    fake_net_set_link(1, 1, PEER_ADDR, 0);

    AppPorts ports;
    ports.net = fake_net_port();
    ports.fs = fake_fs_port();
    ports.input = fake_input_port();
    ports.ime = fake_ime_port();
    ports.clock = &s_clock;
    ports.log = fake_log_port();

    App *app = app_create(&ports, 1);
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

static void connect_site(App *app)
{
    fake_net_set_resolve(PEER_ADDR, 0);
    fake_net_set_link(1, 1, PEER_ADDR, 0);

    press(app, INPUT_BIT(INPUT_SELECT));
    CHECK(app_state(app)->screen == UI_SCREEN_SITES);
    press(app, INPUT_BIT(INPUT_CROSS));

    int ready = 0;
    for (int i = 0; i < TICK_LIMIT && !ready; ++i) {
        (void)app_tick(app);
        const UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
        if (remote->connected && !remote->loading && remote->row_count >= 2)
            ready = 1;
    }
    if (!ready) {
        const UiPaneState *remote = &app_state(app)->panes[UI_PANE_REMOTE];
        fprintf(stderr, "connect failed: connected=%d loading=%d rows=%d "
                        "err=[%s]\n",
                remote->connected, remote->loading, remote->row_count,
                remote->error);
    }
    CHECK(ready);
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

static void select_local_named(App *app, const char *name)
{
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
    for (int i = 0; i < 10; ++i)
        (void)app_tick(app);
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < TICK_LIMIT; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:") == 0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 1)
            break;
    }
    press(app, INPUT_BIT(INPUT_CROSS));
    for (int i = 0; i < TICK_LIMIT; ++i) {
        (void)app_tick(app);
        if (strcmp(app_state(app)->panes[UI_PANE_LOCAL].path, "ux0:data") ==
                0 &&
            app_state(app)->panes[UI_PANE_LOCAL].row_count >= 2)
            break;
    }
    for (int i = 0; i < 5; ++i)
        (void)app_tick(app);
}

static int content_is(const char *path, const char *expected)
{
    char buf[64];
    size_t n = fake_fs_read_file(path, buf, sizeof(buf));
    return n == strlen(expected) && memcmp(buf, expected, n) == 0;
}

static int wait_dock_done(App *app)
{
    for (int i = 0; i < TICK_LIMIT; ++i) {
        if (!app_tick(app))
            return 0;
        if (app_state(app)->dock.state == UI_DOCK_DONE)
            return 1;
    }
    return 0;
}

static int wait_status(App *app, const char *text)
{
    for (int i = 0; i < TICK_LIMIT; ++i) {
        if (!app_tick(app))
            return 0;
        if (strstr(app_state(app)->status, text) != NULL)
            return 1;
    }
    return 0;
}

static int wait_server_on(App *app)
{
    for (int i = 0; i < TICK_LIMIT; ++i) {
        if (!app_tick(app))
            return 0;
        if (app_state(app)->server.run == UI_SERVER_ON)
            return 1;
    }
    return 0;
}

static int wait_net_cancelled(App *app)
{
    for (int i = 0; i < TICK_LIMIT; ++i) {
        if (!app_tick(app))
            return 0;
        if (strcmp(app_state(app)->status, "NETWORK CANCELLED") == 0 &&
            app_state(app)->dock.state == UI_DOCK_IDLE)
            return 1;
    }
    return 0;
}

static int start_peer_thread(Peer *peer, pthread_t *thread)
{
    return pthread_create(thread, NULL, peer_thread_main, peer) == 0;
}

static void stop_peer_thread(Peer *peer, pthread_t thread)
{
    __atomic_store_n(&peer->stop, 1, __ATOMIC_RELEASE);
    CHECK(pthread_join(thread, NULL) == 0);
}

/* A download completes: the destination bytes match and the dock says DONE. */
static void test_download_completes(void)
{
    App *app = make_app(NULL, NULL);
    if (app == NULL)
        return;

    static const char *listing =
        "type=file;size=5;modify=20240101000000; TEST.BIN\r\n"
        "type=file;size=100;modify=20240101000000; BIG.BIN\r\n";

    Peer peer;
    memset(&peer, 0, sizeof(peer));
    peer.cfg.listing = listing;
    peer.cfg.payload = "HELLO";
    pthread_t thread;
    int peer_started = start_peer_thread(&peer, &thread);
    CHECK(peer_started);

    connect_site(app);
    select_remote(app);

    /* Rows sort BIG.BIN, TEST.BIN; one DOWN reaches TEST.BIN. */
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));

    CHECK(wait_dock_done(app));
    CHECK(app_state(app)->dock.state == UI_DOCK_DONE);
    CHECK(app_state(app)->dock.active == 0);

    if (peer_started)
        stop_peer_thread(&peer, thread);
    CHECK(peer.retrs == 1);
    CHECK(peer.aborts == 0);
    app_destroy(app);

    CHECK(content_is("ux0:data/TEST.BIN", "HELLO"));
    CHECK(!fake_fs_exists("ux0:data/TEST.BIN.vftp.part"));
}

/* A SIZE-only peer with a folder destination: one refresh and the refusal. */
static void test_upload_refresh_refusal(void)
{
    App *app = make_app("ux0:data/UP.TXT", "HELLO");
    if (app == NULL)
        return;
    enter_ux0_data(app);
    select_local_named(app, "UP.TXT");

    static const char *listing =
        "type=file;size=5;modify=20240101000000; F.BIN\r\n"
        "type=file;size=5;modify=20240101000000; G.BIN\r\n";

    Peer peer;
    memset(&peer, 0, sizeof(peer));
    peer.cfg.listing = listing;
    peer.cfg.refresh_listing = "type=dir;modify=20240101000000; UP.TXT\r\n";
    peer.cfg.dir_on_refresh = 1;
    peer.cfg.mlst_mode = MLST_UNSUPPORTED;
    peer.cfg.size_supported = 1;
    peer.cfg.exists_path = "/UP.TXT";
    peer.cfg.exists_size = 5;
    pthread_t thread;
    int peer_started = start_peer_thread(&peer, &thread);
    CHECK(peer_started);

    connect_site(app);

    press(app, INPUT_BIT(INPUT_TRIANGLE));
    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_DOWN));
    CHECK(app_state(app)->actions.selected == UI_ACTION_UPLOAD);
    press(app, INPUT_BIT(INPUT_CROSS));

    CHECK(wait_status(app, "DESTINATION IS A FOLDER"));
    CHECK(app_state(app)->screen == UI_SCREEN_WORKSPACE);
    CHECK(strstr(app_state(app)->status, "DESTINATION IS A FOLDER") != NULL);

    if (peer_started)
        stop_peer_thread(&peer, thread);
    CHECK(peer.sizes == 1);
    /* The open listing plus the one refresh; no listing loop after it. */
    CHECK(peer.listings == 2);
    CHECK(peer.stors == 0);
    app_destroy(app);
}

/* A cancel on a stalled transfer: the cancelled terminal, no partial file. */
static void test_cancel_stalled_transfer(void)
{
    App *app = make_app(NULL, NULL);
    if (app == NULL)
        return;

    static const char *listing =
        "type=file;size=5;modify=20240101000000; TEST.BIN\r\n"
        "type=file;size=100;modify=20240101000000; BIG.BIN\r\n";

    Peer peer;
    memset(&peer, 0, sizeof(peer));
    peer.cfg.listing = listing;
    peer.cfg.payload = "HELLO";
    peer.cfg.hold_data = 1;
    pthread_t thread;
    int peer_started = start_peer_thread(&peer, &thread);
    CHECK(peer_started);

    connect_site(app);
    select_remote(app);

    press(app, INPUT_BIT(INPUT_DOWN));
    press(app, INPUT_BIT(INPUT_CROSS));

    int active = 0;
    for (int i = 0; i < TICK_LIMIT && !active; ++i) {
        (void)app_tick(app);
        if (app_state(app)->dock.active == 1)
            active = 1;
    }
    CHECK(active);

    UiCommand cancel;
    memset(&cancel, 0, sizeof(cancel));
    cancel.type = UI_CMD_CANCEL_NETWORK;
    CHECK(handoff_command_push(app_handoff(app), &cancel) == 1);

    CHECK(wait_net_cancelled(app));
    CHECK(app_state(app)->dock.active == 0);

    if (peer_started)
        stop_peer_thread(&peer, thread);
    CHECK(peer.retrs == 1);
    CHECK(peer.aborts > 0);
    app_destroy(app);

    CHECK(!fake_fs_exists("ux0:data/TEST.BIN"));
    CHECK(!fake_fs_exists("ux0:data/TEST.BIN.vftp.part"));
}

typedef struct {
    int done;
    int stop;
    int ok;
    int data_port_ok;
    int stage;
    char payload[64];
    size_t payload_len;
} ServerProbe;

static int probe_reply(FakeFtpClient *client, int expected, char *text,
                       size_t cap)
{
    for (unsigned i = 0; i < PEER_SPIN_LIMIT; ++i) {
        int code = 0;
        if (fake_ftp_client_read_reply(client, &code, text, cap))
            return code == expected;
        sched_yield();
    }
    return 0;
}

static int probe_command(FakeFtpClient *client, const char *command,
                         int expected, char *text, size_t cap)
{
    fake_ftp_client_send(client, command);
    return probe_reply(client, expected, text, cap);
}

static uint16_t probe_parse_227(const char *text)
{
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    unsigned p1 = 0;
    unsigned p2 = 0;
    const char *open = strchr(text, '(');
    if (open == NULL ||
        sscanf(open, "(%u,%u,%u,%u,%u,%u)", &a, &b, &c, &d, &p1, &p2) != 6 ||
        p1 >= 256 || p2 >= 256)
        return 0;
    return (uint16_t)(p1 * 256 + p2);
}

static NetSocket *probe_listener_for(uint16_t port)
{
    for (unsigned i = 0; i < fake_net_listener_count(); ++i) {
        if (fake_net_listener_addr(i).port == port)
            return fake_net_listener_at(i);
    }
    return NULL;
}

/* A fake client logs in to the app's server, arms the session's own slot port,
 * and retrieves a file. */
static void *server_probe_main(void *arg)
{
    ServerProbe *probe = arg;
    NetSocket *listener = NULL;
    probe->stage = 1;
    for (unsigned i = 0; i < PEER_SPIN_LIMIT && listener == NULL &&
                    !__atomic_load_n(&probe->stop, __ATOMIC_ACQUIRE); ++i) {
        if (fake_net_listener_count() > 0)
            listener = probe_listener_for(SERVER_CONTROL_PORT);
        sched_yield();
    }
    if (listener == NULL)
        goto finish;

    probe->stage = 2;
    fake_net_queue_accept(listener, PEER_ADDR, PEER_PORT);
    NetSocket *control = NULL;
    for (unsigned i = 0; i < PEER_SPIN_LIMIT && control == NULL &&
                    !__atomic_load_n(&probe->stop, __ATOMIC_ACQUIRE); ++i) {
        if (fake_net_accept_count() > 0)
            control = fake_net_accepted_at(0);
        sched_yield();
    }
    if (control == NULL)
        goto finish;

    FakeFtpClient client;
    fake_ftp_client_init(&client, control);

    char text[512] = {0};
    probe->stage = 3;
    if (!probe_reply(&client, 220, text, sizeof(text)))
        goto finish;
    probe->stage = 4;
    if (!probe_command(&client, "USER anonymous", 331, text, sizeof(text)))
        goto finish;
    probe->stage = 5;
    if (!probe_command(&client, "PASS x", 230, text, sizeof(text)))
        goto finish;

    probe->stage = 6;
    fake_ftp_client_send(&client, "PASV");
    if (!probe_reply(&client, 227, text, sizeof(text)))
        goto finish;
    uint16_t data_port = probe_parse_227(text);
    probe->data_port_ok = data_port == SERVER_DATA_PORT;

    probe->stage = 7;
    NetSocket *data_listener = probe_listener_for(data_port);
    if (data_listener == NULL)
        goto finish;
    fake_net_queue_accept(data_listener, PEER_ADDR, PEER_PORT + 1);

    probe->stage = 8;
    unsigned before = fake_net_accept_count();
    fake_ftp_client_send(&client, "RETR /UX0:/DATA/SRV.TXT");
    if (!probe_reply(&client, 150, text, sizeof(text)))
        goto finish;

    probe->stage = 9;
    NetSocket *data = NULL;
    for (unsigned i = 0; i < PEER_SPIN_LIMIT && data == NULL &&
                    !__atomic_load_n(&probe->stop, __ATOMIC_ACQUIRE); ++i) {
        if (fake_net_accept_count() > before)
            data = fake_net_accepted_at(fake_net_accept_count() - 1);
        sched_yield();
    }
    if (data == NULL)
        goto finish;

    probe->stage = 10;
    if (!probe_reply(&client, 226, text, sizeof(text)))
        goto finish;

    probe->stage = 11;
    probe->payload_len = fake_net_read_tx(data, probe->payload,
                                          sizeof(probe->payload) - 1);
    probe->payload[probe->payload_len] = '\0';
    probe->ok = 1;

finish:
    __atomic_store_n(&probe->done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void test_server_session_retrieves(void)
{
    App *app = make_app("ux0:data/SRV.TXT", "SERVER");
    if (app == NULL)
        return;

    fake_net_set_link(1, 1, PEER_ADDR, 0);
    press(app, INPUT_BIT(INPUT_START));
    CHECK(app_state(app)->screen == UI_SCREEN_SERVER);
    press(app, INPUT_BIT(INPUT_CROSS));

    int on = wait_server_on(app);
    CHECK(on);
    if (!on) {
        app_destroy(app);
        return;
    }

    ServerProbe probe;
    memset(&probe, 0, sizeof(probe));
    pthread_t thread;
    int probe_started =
        pthread_create(&thread, NULL, server_probe_main, &probe) == 0;
    CHECK(probe_started);

    int done = 0;
    for (int i = 0; i < TICK_LIMIT && !done; ++i) {
        (void)app_tick(app);
        if (__atomic_load_n(&probe.done, __ATOMIC_ACQUIRE))
            done = 1;
    }
    CHECK(done);
    if (!done)
        __atomic_store_n(&probe.stop, 1, __ATOMIC_RELEASE);
    if (probe_started)
        CHECK(pthread_join(thread, NULL) == 0);

    CHECK(probe.ok);
    if (!probe.ok)
        fprintf(stderr, "server probe failed: stage=%d port_ok=%d len=%zu\n",
                probe.stage, probe.data_port_ok, probe.payload_len);
    CHECK(probe.data_port_ok);
    CHECK(probe.payload_len == 6);
    CHECK(strcmp(probe.payload, "SERVER") == 0);

    app_destroy(app);
}

int main(void)
{
    test_download_completes();
    test_upload_refresh_refusal();
    test_cancel_stalled_transfer();
    test_server_session_retrieves();

    if (s_failures != 0) {
        fprintf(stderr, "test_app_thread_xfer: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_app_thread_xfer: OK\n");
    return 0;
}
