/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/ftp/server.h"
#include "fake_clock.h"
#include "fake_ftp_client.h"
#include "fake_fs.h"
#include "fake_net.h"

static int s_failures;
static unsigned s_states_seen;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

#define PEER_ADDR 0xC0A8012Au
#define PEER_PORT 40000

typedef struct {
    FtpServerState state;
    int clients;
    int state_changes;
    int client_events;
    int session_index;
    uint32_t peer_addr;
    char op[16];
    int errors;
    char detail[128];
    int done;
    int ok;
    FtpCause cause;
    char path[128];
    int progress;
    uint64_t bytes;
    uint64_t total;
    int total_known;
} SCapture;

static void capture_event(void *ctx, const FtpServerEvent *event)
{
    SCapture *cap = ctx;
    switch (event->kind) {
    case FTP_SERVER_EVENT_STATE:
        cap->state = event->state;
        cap->clients = event->clients;
        cap->state_changes++;
        s_states_seen |= 1u << event->state;
        break;
    case FTP_SERVER_EVENT_CLIENT:
        cap->clients = event->clients;
        cap->client_events++;
        cap->session_index = event->session_index;
        cap->peer_addr = event->peer.addr;
        snprintf(cap->op, sizeof(cap->op), "%s",
                 event->op != NULL ? event->op : "");
        snprintf(cap->path, sizeof(cap->path), "%s",
                 event->path != NULL ? event->path : "");
        break;
    case FTP_SERVER_EVENT_PROGRESS:
        cap->progress++;
        cap->bytes = event->bytes;
        cap->total = event->total;
        cap->total_known = event->total_known;
        break;
    case FTP_SERVER_EVENT_TRANSFER_DONE:
        cap->done++;
        cap->ok = event->ok;
        cap->cause = event->cause;
        cap->bytes = event->bytes;
        cap->total = event->total;
        cap->total_known = event->total_known;
        if (event->path != NULL)
            snprintf(cap->path, sizeof(cap->path), "%s", event->path);
        break;
    case FTP_SERVER_EVENT_ERROR:
        cap->errors++;
        if (event->detail != NULL)
            snprintf(cap->detail, sizeof(cap->detail), "%s", event->detail);
        break;
    }
}

static void setup(void)
{
    fake_net_reset();
    fake_fs_reset();
    fake_clock_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ur0:");
    fake_fs_add_dir("ur0:tai");
    fake_fs_add_dir("uma0:");
    fake_net_set_link(1, 1, PEER_ADDR, 0);
}

static FtpServer *start_server(SCapture *cap)
{
    FtpServer *server = ftp_server_create(fake_net_port(), fake_fs_port(),
                                          fake_clock_port(), capture_event,
                                          cap);
    CHECK(server != NULL);
    CHECK(ftp_server_start(server, 1337) == FTP_SERVER_OK);
    CHECK(ftp_server_state(server) == FTP_SERVER_ON);
    CHECK(fake_net_listener_count() == 2);
    CHECK(fake_net_listener_addr(0).port == 1337);
    CHECK(fake_net_listener_addr(1).port == 1338);
    return server;
}

static int read_reply(FakeFtpClient *client, int *code, char *text, size_t cap)
{
    return fake_ftp_client_read_reply(client, code, text, cap);
}

static void command(FtpServer *server, FakeFtpClient *client, const char *cmd,
                    int expect)
{
    fake_ftp_client_send(client, cmd);
    ftp_server_tick(server, 0);

    int code = 0;
    char text[512] = {0};
    int got = read_reply(client, &code, text, sizeof(text));
    if (!got) {
        fprintf(stderr, "no reply for [%s], expected %d\n", cmd, expect);
        s_failures++;
        return;
    }
    if (code != expect) {
        fprintf(stderr, "command [%s] expected %d got %d [%s]\n", cmd, expect,
                code, text);
        s_failures++;
    }
}

static void command_text(FtpServer *server, FakeFtpClient *client,
                         const char *cmd, int expect, char *out, size_t cap)
{
    fake_ftp_client_send(client, cmd);
    ftp_server_tick(server, 0);

    int code = 0;
    char text[512] = {0};
    int got = read_reply(client, &code, text, sizeof(text));
    if (out != NULL && cap > 0) {
        snprintf(out, cap, "%s", text);
    }
    if (!got) {
        fprintf(stderr, "no reply for [%s], expected %d\n", cmd, expect);
        s_failures++;
        return;
    }
    if (code != expect) {
        fprintf(stderr, "command [%s] expected %d got %d [%s]\n", cmd, expect,
                code, text);
        s_failures++;
    }
}

static void connect_session(FtpServer *server, FakeFtpClient *client)
{
    fake_net_queue_accept(fake_net_listener_at(0), PEER_ADDR, PEER_PORT);
    ftp_server_tick(server, 0);

    NetSocket *control = fake_net_accepted_at(0);
    CHECK(control != NULL);
    fake_ftp_client_init(client, control);

    int code = 0;
    char text[512];
    CHECK(read_reply(client, &code, text, sizeof(text)) == 1);
    CHECK(code == 220);

    command(server, client, "USER anonymous", 331);
    command(server, client, "PASS x", 230);
}

static void pasv(FtpServer *server, FakeFtpClient *client);
static void queue_data_peer(void);
static void queue_data_peer_for(uint32_t peer, uint16_t port);
static NetSocket *data_command(FtpServer *server, FakeFtpClient *client,
                               const char *cmd, int expect);
static void finish_data(FtpServer *server, FakeFtpClient *client, int expect);
static void wait_reply(FtpServer *server, FakeFtpClient *client, int expect);

/* A second control session from a different peer (the FileZilla browse and
 * transfer connections come from one PC; the test uses a second address to
 * keep the data-peer check unambiguous). */
static void connect_second_session(FtpServer *server, FakeFtpClient *client,
                                   uint32_t peer, uint16_t port)
{
    unsigned before = fake_net_accept_count();
    fake_net_queue_accept(fake_net_listener_at(0), peer, port);
    ftp_server_tick(server, 0);
    CHECK(fake_net_accept_count() == before + 1);

    NetSocket *control = fake_net_accepted_at(before);
    CHECK(control != NULL);
    fake_ftp_client_init(client, control);

    int code = 0;
    char text[512];
    CHECK(read_reply(client, &code, text, sizeof(text)) == 1);
    CHECK(code == 220);

    command(server, client, "USER anonymous", 331);
    command(server, client, "PASS x", 230);
}

static int s_two_held;

static int two_client_guard(void *ctx, FtpGuardAction action, const char *path,
                            int write, int tree)
{
    (void)ctx;
    (void)write;
    (void)tree;
    if (action == FTP_GUARD_RELEASE) {
        s_two_held = 0;
        return 0;
    }
    if (path == NULL || strcmp(path, "ux0:data/busy.bin") != 0)
        return 0;
    if (action == FTP_GUARD_CHECK)
        return s_two_held;
    if (action == FTP_GUARD_HOLD) {
        s_two_held = 1;
        return 0;
    }
    return 0;
}

static void test_two_clients(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    s_two_held = 0;
    ftp_server_set_guard(server, two_client_guard, NULL);

    FakeFtpClient a;
    FakeFtpClient b;
    connect_session(server, &a);
    connect_second_session(server, &b, PEER_ADDR + 1, PEER_PORT + 20);
    CHECK(ftp_server_clients(server) == 2);

    command(server, &a, "PWD", 257);
    command(server, &b, "PWD", 257);
    command(server, &b, "CWD /UX0:", 250);
    char text[512];
    command_text(server, &b, "PWD", 257, text, sizeof(text));
    CHECK(strstr(text, "/UX0:") != NULL);
    command_text(server, &a, "PWD", 257, text, sizeof(text));
    CHECK(strstr(text, "\"/\"") != NULL);

    /* A listing from B while A is idle is independent. */
    pasv(server, &b);
    queue_data_peer_for(PEER_ADDR + 1, PEER_PORT + 21);
    NetSocket *data =
        data_command(server, &b, "MLSD /UX0:", 150);
    CHECK(data != NULL);
    finish_data(server, &b, 226);

    /* A transfer owns the one data slot; B's PASV waits for it. */
    pasv(server, &a);
    queue_data_peer();
    data = data_command(server, &a, "STOR ux0:/data/busy.bin", 150);
    CHECK(data != NULL);
    (void)data_command(server, &b, "PASV", 450);
    /* A non-owner's control-only probe of the held path must not disarm A. */
    command(server, &b, "MLST /UX0:/data/busy.bin", 450);

    /* Closing the other client must not abort A's active transfer. */
    command(server, &b, "QUIT", 221);
    CHECK(ftp_server_clients(server) == 1);
    fake_net_queue_rx(data, "x", 1);
    fake_net_shutdown(data);
    wait_reply(server, &a, 226);
    command(server, &a, "NOOP", 200);

    /* A new session can transfer after A's data slot is released. */
    connect_second_session(server, &b, PEER_ADDR + 1, PEER_PORT + 22);
    pasv(server, &b);
    queue_data_peer_for(PEER_ADDR + 1, PEER_PORT + 21);
    data = data_command(server, &b, "MLSD /UX0:", 150);
    CHECK(data != NULL);
    finish_data(server, &b, 226);

    command(server, &b, "QUIT", 221);
    CHECK(ftp_server_clients(server) == 1);
    command(server, &a, "QUIT", 221);
    CHECK(ftp_server_clients(server) == 0);

    ftp_server_destroy(server);
}

/* The CLIENT event carries the peer and verb of the session that spoke last,
 * and falls back to a live session when that one closes. */
static void test_client_event_pairs_session(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);

    FakeFtpClient b;
    FakeFtpClient a;
    connect_session(server, &b);
    connect_second_session(server, &a, PEER_ADDR + 1, PEER_PORT + 20);

    command(server, &b, "PWD", 257);
    CHECK(cap.session_index == 0);
    CHECK(cap.peer_addr == PEER_ADDR);
    CHECK(strcmp(cap.op, "PWD") == 0);
    CHECK(strcmp(cap.path, "/") == 0);

    command(server, &a, "CWD /UX0:", 250);
    CHECK(cap.session_index == 1);
    CHECK(cap.peer_addr == PEER_ADDR + 1);
    CHECK(strcmp(cap.op, "CWD") == 0);

    command(server, &a, "QUIT", 221);
    CHECK(ftp_server_clients(server) == 1);
    CHECK(cap.session_index == 0);
    CHECK(cap.peer_addr == PEER_ADDR);

    command(server, &b, "QUIT", 221);
    ftp_server_destroy(server);
}

static void test_close_while_armed(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);

    FakeFtpClient b;
    FakeFtpClient a;
    connect_session(server, &b);
    connect_second_session(server, &a, PEER_ADDR + 1, PEER_PORT + 20);
    CHECK(ftp_server_clients(server) == 2);

    pasv(server, &a);
    /* Drop A's control between PASV and its data command. QUIT would disarm
     * the owner's slot on the way out and hide the wedge. */
    fake_net_reset_peer(a.control);
    ftp_server_tick(server, 0);
    ftp_server_tick(server, 0);
    CHECK(ftp_server_clients(server) == 1);

    /* B must be able to arm the slot and transfer. */
    pasv(server, &b);
    queue_data_peer_for(PEER_ADDR, PEER_PORT + 1);
    NetSocket *data = data_command(server, &b, "MLSD /UX0:", 150);
    CHECK(data != NULL);
    finish_data(server, &b, 226);

    command(server, &b, "QUIT", 221);
    ftp_server_destroy(server);
}

/* A client that arms PASV and then closes must not keep the one data slot.
 * B is connected first so slot reuse cannot hide it. */
static void test_owner_close_active_one_event(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);

    FakeFtpClient b;
    FakeFtpClient a;
    connect_session(server, &b);
    connect_second_session(server, &a, PEER_ADDR + 1, PEER_PORT + 20);

    pasv(server, &a);
    queue_data_peer_for(PEER_ADDR + 1, PEER_PORT + 21);
    NetSocket *data = data_command(server, &a, "STOR ux0:/data/a.bin", 150);
    CHECK(data != NULL);
    int before = cap.done;

    fake_net_reset_peer(a.control);
    ftp_server_tick(server, 0);
    ftp_server_tick(server, 0);

    CHECK(ftp_server_clients(server) == 1);
    CHECK(cap.done == before + 1);
    CHECK(cap.ok == 0);
    CHECK(cap.cause == FTP_CAUSE_NETWORK);

    /* The slot is free for the surviving session. */
    pasv(server, &b);
    queue_data_peer_for(PEER_ADDR, PEER_PORT + 1);
    NetSocket *bdata = data_command(server, &b, "MLSD /UX0:", 150);
    CHECK(bdata != NULL);
    finish_data(server, &b, 226);

    command(server, &b, "QUIT", 221);
    ftp_server_destroy(server);
}

/* A foreign ABOR may not disarm the owner's PASV. */
static void test_foreign_abor_keeps_pasv(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);

    FakeFtpClient b;
    FakeFtpClient a;
    connect_session(server, &b);
    connect_second_session(server, &a, PEER_ADDR + 1, PEER_PORT + 20);

    pasv(server, &a);
    command(server, &b, "ABOR", 225);

    /* A's armed slot survives: its data command reaches 150. */
    queue_data_peer_for(PEER_ADDR + 1, PEER_PORT + 21);
    NetSocket *data = data_command(server, &a, "MLSD /UX0:", 150);
    CHECK(data != NULL);
    finish_data(server, &a, 226);

    command(server, &a, "QUIT", 221);
    command(server, &b, "QUIT", 221);
    ftp_server_destroy(server);
}

static void pasv(FtpServer *server, FakeFtpClient *client)
{
    command(server, client, "PASV", 227);
}

static void queue_data_peer_for(uint32_t peer, uint16_t port)
{
    fake_net_queue_accept(fake_net_listener_at(1), peer, port);
}

static void queue_data_peer(void)
{
    queue_data_peer_for(PEER_ADDR, PEER_PORT + 1);
}

static NetSocket *data_command(FtpServer *server, FakeFtpClient *client,
                               const char *cmd, int expect)
{
    unsigned before = fake_net_accept_count();
    fake_ftp_client_send(client, cmd);
    ftp_server_tick(server, 0);

    int code = 0;
    char text[512] = {0};
    int got = read_reply(client, &code, text, sizeof(text));
    if (!got) {
        fprintf(stderr, "no reply for [%s], expected %d\n", cmd, expect);
        s_failures++;
        return NULL;
    }
    if (code != expect) {
        fprintf(stderr, "data command [%s] expected %d got %d [%s]\n", cmd,
                expect, code, text);
        s_failures++;
    }

    if (fake_net_accept_count() > before)
        return fake_net_accepted_at(fake_net_accept_count() - 1);
    return NULL;
}

static void wait_reply(FtpServer *server, FakeFtpClient *client, int expect)
{
    for (int i = 0; i < 6; ++i) {
        ftp_server_tick(server, 0);
        int code = 0;
        char text[512] = {0};
        if (read_reply(client, &code, text, sizeof(text))) {
            if (code != expect) {
                fprintf(stderr, "wait expected %d got %d [%s]\n", expect, code,
                        text);
                s_failures++;
            }
            return;
        }
    }
    fprintf(stderr, "wait: no reply, expected %d\n", expect);
    s_failures++;
}

static void finish_data(FtpServer *server, FakeFtpClient *client, int expect)
{
    wait_reply(server, client, expect);
}

static void send_stor(FtpServer *server, FakeFtpClient *client,
                      const char *path, const char *bytes, int reset,
                      int expect)
{
    pasv(server, client);
    queue_data_peer();
    NetSocket *data = data_command(server, client, path, 150);
    CHECK(data != NULL);
    if (bytes != NULL && bytes[0] != '\0')
        fake_net_queue_rx(data, bytes, strlen(bytes));
    if (reset)
        fake_net_reset_peer(data);
    else
        fake_net_shutdown(data);
    wait_reply(server, client, expect);
}

static void test_start_and_bind(void)
{
    setup();
    fake_net_set_link(0, 1, PEER_ADDR, 0);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = ftp_server_create(fake_net_port(), fake_fs_port(),
                                          fake_clock_port(), capture_event,
                                          &cap);
    CHECK(ftp_server_start(server, 1337) == FTP_SERVER_NO_NETWORK);
    CHECK(ftp_server_state(server) == FTP_SERVER_OFF);
    CHECK(cap.errors == 1);
    CHECK(strstr(cap.detail, "NO NETWORK") != NULL);
    ftp_server_destroy(server);

    setup();
    memset(&cap, 0, sizeof(cap));
    server = ftp_server_create(fake_net_port(), fake_fs_port(),
                               fake_clock_port(), capture_event, &cap);
    fake_net_fail_next(NET_ERROR);
    CHECK(ftp_server_start(server, 1337) == FTP_SERVER_BIND_FAILED);
    CHECK(ftp_server_state(server) == FTP_SERVER_OFF);
    CHECK(fake_net_listener_count() == 0);
    CHECK(cap.errors == 1);
    CHECK(strstr(cap.detail, "BIND FAILED") != NULL);
    ftp_server_destroy(server);
}

static void test_login(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;

    connect_session(server, &client);
    CHECK(ftp_server_clients(server) == 1);
    CHECK(cap.clients == 1);

    command(server, &client, "NOOP", 200);
    command(server, &client, "SYST", 215);
    command(server, &client, "OPTS UTF8 ON", 200);
    command(server, &client, "FEAT", 211);
    command(server, &client, "ABOR", 225);

    fake_ftp_client_send(&client, "USER bob");
    ftp_server_tick(server, 0);
    int code = 0;
    char text[512];
    CHECK(read_reply(&client, &code, text, sizeof(text)) == 1);
    CHECK(code == 530);

    ftp_server_destroy(server);
}

static void test_list_mlsd_nlst(void)
{
    setup();
    fake_fs_add_dir("ux0:data/FOLDER");
    fake_fs_add_file("ux0:data/FILE.TXT", 5);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    char pasv_text[512];
    command_text(server, &client, "PASV", 227, pasv_text, sizeof(pasv_text));
    CHECK(strstr(pasv_text, "(192,168,1,42,5,58)") != NULL);
    queue_data_peer();
    NetSocket *data = data_command(server, &client, "LIST /UX0:/DATA", 150);
    CHECK(data != NULL);
    finish_data(server, &client, 226);
    char tx[4096];
    size_t n = fake_net_read_tx(data, tx, sizeof(tx) - 1);
    tx[n] = '\0';
    CHECK(strstr(tx, "FILE.TXT") != NULL);
    CHECK(strstr(tx, "FOLDER") != NULL);
    CHECK(strstr(tx, "drwx") != NULL);

    pasv(server, &client);
    queue_data_peer();
    data = data_command(server, &client, "MLSD /UX0:/DATA", 150);
    CHECK(data != NULL);
    finish_data(server, &client, 226);
    n = fake_net_read_tx(data, tx, sizeof(tx) - 1);
    tx[n] = '\0';
    CHECK(strstr(tx, "type=file;size=5;") != NULL);
    CHECK(strstr(tx, "type=dir;") != NULL);

    pasv(server, &client);
    queue_data_peer();
    data = data_command(server, &client, "NLST /UX0:/DATA", 150);
    CHECK(data != NULL);
    finish_data(server, &client, 226);
    n = fake_net_read_tx(data, tx, sizeof(tx) - 1);
    tx[n] = '\0';
    CHECK(strstr(tx, "FILE.TXT") != NULL);
    CHECK(strstr(tx, "FOLDER") != NULL);

    ftp_server_destroy(server);
}

static void test_root_listing(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    queue_data_peer();
    NetSocket *data = data_command(server, &client, "LIST /", 150);
    CHECK(data != NULL);
    finish_data(server, &client, 226);
    char tx[2048];
    size_t n = fake_net_read_tx(data, tx, sizeof(tx) - 1);
    tx[n] = '\0';
    CHECK(strstr(tx, "UX0:") != NULL);
    CHECK(strstr(tx, "UR0:") != NULL);
    CHECK(strstr(tx, "UMA0:") != NULL);

    ftp_server_destroy(server);
}

static void test_retr(void)
{
    setup();
    fake_fs_seed_file("ux0:data/hello.txt", "hello", 5);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    queue_data_peer();
    NetSocket *data =
        data_command(server, &client, "RETR /UX0:/DATA/hello.txt", 150);
    CHECK(data != NULL);
    finish_data(server, &client, 226);
    char tx[64];
    size_t n = fake_net_read_tx(data, tx, sizeof(tx));
    CHECK(n == 5 && memcmp(tx, "hello", 5) == 0);
    CHECK(cap.done == 1 && cap.ok == 1);
    CHECK(cap.bytes == 5);
    CHECK(cap.total_known == 1 && cap.total == 5);

    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "RETR /UX0:/DATA/missing.txt", 550);
    command(server, &client, "SIZE /UX0:/DATA/hello.txt", 213);
    command(server, &client, "SIZE /UX0:/DATA/missing.txt", 550);
    ftp_server_destroy(server);
}

static void test_stor(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    send_stor(server, &client, "STOR ux0:data/new.bin", "abc", 0, 226);
    char buf[16];
    size_t n = fake_fs_read_file("ux0:data/new.bin", buf, sizeof(buf));
    CHECK(n == 3 && memcmp(buf, "abc", 3) == 0);
    CHECK(!fake_fs_exists("ux0:data/new.bin.vftp.part"));
    CHECK(cap.done == 1 && cap.ok == 1);

    fake_fs_seed_file("ux0:data/over.bin", "old", 3);
    send_stor(server, &client, "STOR ux0:data/over.bin", "new", 0, 226);
    n = fake_fs_read_file("ux0:data/over.bin", buf, sizeof(buf));
    CHECK(n == 3 && memcmp(buf, "new", 3) == 0);
    CHECK(!fake_fs_exists("ux0:data/over.bin.vftp.part"));

    fake_fs_seed_file("ux0:data/reset.bin", "old", 3);
    send_stor(server, &client, "STOR ux0:data/reset.bin", "ab", 1, 426);
    n = fake_fs_read_file("ux0:data/reset.bin", buf, sizeof(buf));
    CHECK(n == 3 && memcmp(buf, "old", 3) == 0);
    CHECK(!fake_fs_exists("ux0:data/reset.bin.vftp.part"));

    ftp_server_destroy(server);
}

/* FileZilla sends the transfer command and connects the data socket after it.
 * The server must arm, wait for the peer, and only then send 150. */
static void test_stor_connect_after_command(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    fake_ftp_client_send(&client, "STOR ux0:data/late.bin");
    ftp_server_tick(server, 0);

    int code = 0;
    char text[512] = {0};
    CHECK(read_reply(&client, &code, text, sizeof(text)) == 0);

    queue_data_peer();
    ftp_server_tick(server, 0);
    CHECK(read_reply(&client, &code, text, sizeof(text)) == 1);
    CHECK(code == 150);

    NetSocket *data = fake_net_accepted_at(fake_net_accept_count() - 1);
    CHECK(data != NULL);
    fake_net_queue_rx(data, "late", 4);
    fake_net_shutdown(data);
    wait_reply(server, &client, 226);

    char buf[16];
    size_t n = fake_fs_read_file("ux0:data/late.bin", buf, sizeof(buf));
    CHECK(n == 4 && memcmp(buf, "late", 4) == 0);
    CHECK(cap.done == 1 && cap.ok == 1);

    ftp_server_destroy(server);
}

/* The destination of an in-flight upload has no new file yet: the owner gets
 * not-found, not the busy reply, and never the old file's size. */
static void test_owner_size_while_upload(void)
{
    setup();
    fake_fs_seed_file("ux0:data/old.bin", "OLD", 3);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient a;
    connect_session(server, &a);

    pasv(server, &a);
    queue_data_peer();
    NetSocket *data = data_command(server, &a, "STOR ux0:/data/old.bin", 150);
    CHECK(data != NULL);

    char text[128];
    command_text(server, &a, "SIZE ux0:/data/old.bin", 550, text, sizeof(text));
    CHECK(strstr(text, "3") == NULL);

    ftp_server_destroy(server);
}

static int s_tree_holds;

static int tree_guard(void *ctx, FtpGuardAction action, const char *path,
                      int write, int tree)
{
    (void)ctx;
    (void)path;
    (void)write;
    if (action == FTP_GUARD_HOLD && tree)
        s_tree_holds++;
    return 0;
}

static int s_retr_guard_checks;

static int retr_owner_guard(void *ctx, FtpGuardAction action, const char *path,
                            int write, int tree)
{
    (void)ctx;
    (void)path;
    (void)write;
    (void)tree;
    if (action != FTP_GUARD_CHECK)
        return 0;
    /* Let the transfer's own check through; refuse the later foreign probe. */
    s_retr_guard_checks++;
    return s_retr_guard_checks > 1;
}

/* The owner may read metadata for its own RETR source while the guard refuses
 * every other session. */
static void test_owner_size_while_download(void)
{
    setup();
    static char big[8192];
    memset(big, 'X', sizeof(big));
    fake_fs_seed_file("ux0:data/own.bin", big, sizeof(big));
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    s_retr_guard_checks = 0;
    ftp_server_set_guard(server, retr_owner_guard, NULL);

    FakeFtpClient b;
    FakeFtpClient a;
    connect_session(server, &b);
    connect_second_session(server, &a, PEER_ADDR + 1, PEER_PORT + 20);

    pasv(server, &a);
    /* Leave the data peer unqueued: the transfer stays PENDING, which is the
     * state the owner exemption must cover. */
    fake_ftp_client_send(&a, "RETR ux0:/data/own.bin");
    ftp_server_tick(server, 0);

    char text[128];
    command_text(server, &a, "SIZE ux0:/data/own.bin", 213, text, sizeof(text));
    CHECK(strstr(text, "8192") != NULL);
    command_text(server, &b, "SIZE ux0:/data/own.bin", 450, text, sizeof(text));

    ftp_server_destroy(server);
}

/* A rename may not create a transfer sidecar name. */
static void test_rename_sidecar_refused(void)
{
    setup();
    fake_fs_seed_file("ux0:data/a.bin", "A", 1);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient a;
    connect_session(server, &a);

    command(server, &a, "RNFR ux0:/data/a.bin", 350);
    command(server, &a, "RNTO ux0:/data/a.bin.vftp.part", 550);
    CHECK(!fake_fs_exists("ux0:data/a.bin.vftp.part"));
    CHECK(fake_fs_exists("ux0:data/a.bin"));

    command(server, &a, "QUIT", 221);
    ftp_server_destroy(server);
}

/* A tree command checks the path but must not leave a hold behind; a leaked
 * hold makes the next rename on the path answer 450. */
static void test_tree_command_releases(void)
{
    setup();
    fake_fs_seed_file("ux0:data/a.bin", "A", 1);
    fake_fs_seed_file("ux0:data/b.bin", "B", 1);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    s_tree_holds = 0;
    ftp_server_set_guard(server, tree_guard, NULL);
    FakeFtpClient a;
    connect_session(server, &a);

    command(server, &a, "DELE ux0:/data/a.bin", 250);
    command(server, &a, "RNFR ux0:/data/b.bin", 350);
    command(server, &a, "RNTO ux0:/data/c.bin", 250);
    CHECK(s_tree_holds == 0);

    command(server, &a, "QUIT", 221);
    ftp_server_destroy(server);
}

/* A storage failure mid-upload answers 451 and leaves no temp. */
static void test_storage_failure_451(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient a;
    connect_session(server, &a);

    fake_fs_fail_op(FAKE_FS_OP_WRITE, FS_NO_SPACE);
    pasv(server, &a);
    queue_data_peer();
    NetSocket *data = data_command(server, &a, "STOR ux0:/data/fail.bin", 150);
    CHECK(data != NULL);
    fake_net_queue_rx(data, "payload", 7);
    fake_net_shutdown(data);
    wait_reply(server, &a, 451);

    CHECK(!fake_fs_exists("ux0:data/fail.bin"));
    CHECK(!fake_fs_exists("ux0:data/fail.bin.vftp.part"));
    CHECK(cap.done == 1 && cap.ok == 0);
    ftp_server_destroy(server);
}

static char s_trace[8192];

static void trace_line(void *ctx, const char *line)
{
    (void)ctx;
    size_t used = strlen(s_trace);
    size_t len = strlen(line);
    if (used + len + 2 >= sizeof(s_trace))
        return;
    memcpy(s_trace + used, line, len);
    s_trace[used + len] = '\n';
    s_trace[used + len + 1] = '\0';
}

static void test_trace_masks_password(void)
{
    setup();
    memset(s_trace, 0, sizeof(s_trace));
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = ftp_server_create(fake_net_port(), fake_fs_port(),
                                          fake_clock_port(), capture_event,
                                          &cap);
    CHECK(server != NULL);
    ftp_server_set_trace(server, trace_line, NULL);
    CHECK(ftp_server_start(server, 1337) == FTP_SERVER_OK);
    FakeFtpClient client;
    connect_session(server, &client);

    command(server, &client, "USER anonymous", 331);
    command(server, &client, "PASS hunter2", 230);
    CHECK(strstr(s_trace, "FTPC USER anonymous") != NULL);
    CHECK(strstr(s_trace, "FTPC PASS ****") != NULL);
    CHECK(strstr(s_trace, "hunter2") == NULL);

    pasv(server, &client);
    queue_data_peer();
    NetSocket *data = data_command(server, &client, "STOR ux0:data/t.bin",
                                   150);
    CHECK(data != NULL);
    fake_net_queue_rx(data, "x", 1);
    fake_net_shutdown(data);
    wait_reply(server, &client, 226);
    CHECK(strstr(s_trace, "FTPD STOR OPEN ux0:data/t.bin") != NULL);
    CHECK(strstr(s_trace, "FTPD RX 1") != NULL);
    CHECK(strstr(s_trace, "FTPD FINISH kind=4 ok=1") != NULL);
    CHECK(strstr(s_trace, "FTPR 226") != NULL);

    ftp_server_destroy(server);
}

/* The reply-backlog path masks a dropped PASS before it traces the line. The
 * fake keeps the control socket from draining so the 16 KiB reply buffer fills,
 * which no ordinary client does. */
static void test_trace_drops_password(void)
{
    setup();
    memset(s_trace, 0, sizeof(s_trace));
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = ftp_server_create(fake_net_port(), fake_fs_port(),
                                          fake_clock_port(), capture_event,
                                          &cap);
    CHECK(server != NULL);
    ftp_server_set_trace(server, trace_line, NULL);
    CHECK(ftp_server_start(server, 1337) == FTP_SERVER_OK);

    FakeFtpClient client;
    connect_session(server, &client);
    NetSocket *control = fake_net_accepted_at(0);
    CHECK(control != NULL);
    fake_net_block_sends(control, 1);

    /* PWD before login answers through server_reply; 600 of them fill the
     * server's 16 KiB tx buffer even though the fake accepts nothing. The
     * direct append paths hide a full buffer, so the backlog needs this one. */
    char burst[4096];
    size_t used = 0;
    for (int i = 0; i < 600; ++i) {
        memcpy(burst + used, "PWD\r\n", 5);
        used += 5;
    }
    fake_net_queue_rx(control, burst, used);
    ftp_server_tick(server, 0);
    CHECK(cap.errors > 0);
    CHECK(strstr(cap.detail, "REPLY BACKLOG") != NULL);

    /* Only the drop path's own lines remain in the trace. */
    memset(s_trace, 0, sizeof(s_trace));
    static const char *dropped =
        "PASS hunter2\r\n"
        "pass hunter2\r\n"
        "PASS\thunter2\r\n"
        "PASS\r\n"
        "NOOP\r\n";
    fake_net_queue_rx(control, dropped, strlen(dropped));
    ftp_server_tick(server, 0);

    CHECK(strstr(s_trace, "FTPC DROPPED PASS ****") != NULL);
    CHECK(strstr(s_trace, "FTPC DROPPED NOOP") != NULL);
    CHECK(strstr(s_trace, "hunter2") == NULL);
    /* The four PASS spellings each produced one masked line. */
    int masked = 0;
    for (const char *p = s_trace;
         (p = strstr(p, "FTPC DROPPED PASS ****")) != NULL; ++p)
        masked++;
    CHECK(masked == 4);

    fake_net_block_sends(control, 0);
    ftp_server_destroy(server);
}

static int s_guard_checks;
static int s_guard_holds;
static int s_guard_releases;

static int test_guard(void *ctx, FtpGuardAction action, const char *path,
                      int write, int tree)
{
    (void)ctx;
    (void)write;
    (void)tree;
    if (action == FTP_GUARD_RELEASE) {
        s_guard_releases++;
        return 0;
    }
    if (action == FTP_GUARD_HOLD) {
        s_guard_holds++;
        return 0;
    }
    s_guard_checks++;
    return path != NULL && strcmp(path, "ux0:data/held.bin") == 0;
}

/* U26: a held path answers 450 and disarms PASV; a sibling still works. */
static void test_held_path(void)
{
    setup();
    s_guard_checks = 0;
    s_guard_holds = 0;
    s_guard_releases = 0;
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    ftp_server_set_guard(server, test_guard, NULL);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    unsigned before = fake_net_accept_count();
    (void)data_command(server, &client, "STOR ux0:data/held.bin", 450);
    CHECK(fake_net_accept_count() == before);
    CHECK(!fake_fs_exists("ux0:data/held.bin"));

    /* The refusal disarmed PASV: a data command now needs a new one. */
    (void)data_command(server, &client, "STOR ux0:data/other.bin", 425);

    send_stor(server, &client, "STOR ux0:data/free.bin", "ok", 0, 226);
    CHECK(fake_fs_exists("ux0:data/free.bin"));
    CHECK(s_guard_checks > 0);
    CHECK(s_guard_holds > 0);
    CHECK(s_guard_releases > 0);

    command(server, &client, "DELE ux0:data/held.bin", 450);

    ftp_server_destroy(server);
}

/* A held RNTO target must send one 450, not a 450 followed by a stale 550. */
static void test_held_rename(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    ftp_server_set_guard(server, test_guard, NULL);
    FakeFtpClient client;
    connect_session(server, &client);
    fake_fs_seed_file("ux0:data/a.bin", "A", 1);

    command(server, &client, "RNFR ux0:data/a.bin", 350);
    command(server, &client, "RNTO ux0:data/held.bin", 450);
    /* One reply per command: a stale 550 would be consumed here. */
    command(server, &client, "NOOP", 200);
    CHECK(fake_fs_exists("ux0:data/a.bin"));

    ftp_server_destroy(server);
}

static void test_denials(void)
{
    setup();
    fake_fs_add_dir("ux0:tai");
    fake_fs_add_dir("ux0:data/vitaFTPShell");
    fake_fs_seed_file("ur0:tai/config.txt", "config", 6);
    fake_fs_seed_file("ux0:tai/config.txt", "config", 6);
    fake_fs_seed_file("ux0:data/a.txt", "a", 1);
    fake_fs_seed_file("ur0:tai/boot_config.txt", "boot", 4);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    /* boot_config.txt stays locked for the FTP role. */
    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR ur0:tai/boot_config.txt", 550);
    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR UR0:/TAI/./BOOT_CONFIG.TXT",
                       550);
    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR //UR0://TAI//BOOT_CONFIG.TXT",
                       550);
    command(server, &client, "DELE ur0:tai/boot_config.txt", 550);
    CHECK(fake_fs_exists("ur0:tai/boot_config.txt"));

    /* tai is writable except boot_config.txt. */
    send_stor(server, &client, "STOR ur0:tai/x.suprx", "X", 0, 226);
    CHECK(fake_fs_exists("ur0:tai/x.suprx"));
    command(server, &client, "DELE ur0:tai/x.suprx", 250);
    CHECK(!fake_fs_exists("ur0:tai/x.suprx"));
    command(server, &client, "MKD ur0:tai/NEW", 250);
    CHECK(fake_fs_exists("ur0:tai/NEW"));
    command(server, &client, "RMD ur0:tai/NEW", 250);
    CHECK(!fake_fs_exists("ur0:tai/NEW"));
    command(server, &client, "DELE ux0:tai/config.txt", 250);
    CHECK(!fake_fs_exists("ux0:tai/config.txt"));

    /* The app data folder is exposed. */
    send_stor(server, &client, "STOR ux0:data/vitaFTPShell/x", "X", 0, 226);
    CHECK(fake_fs_exists("ux0:data/vitaFTPShell/x"));
    command(server, &client, "DELE ux0:data/vitaFTPShell/x", 250);
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/x"));

    /* A rename into tai is allowed now. */
    command(server, &client, "RNFR /UX0:/DATA/a.txt", 350);
    command(server, &client, "RNTO ux0:tai/a.txt", 250);
    CHECK(fake_fs_exists("ux0:tai/a.txt"));
    CHECK(!fake_fs_exists("ux0:data/a.txt"));

    /* The volume and data roots themselves remain protected. */
    command(server, &client, "DELE /UX0:", 550);
    command(server, &client, "RNFR /UX0:", 550);
    command(server, &client, "RMD /UR0:", 550);
    command(server, &client, "RMD /UMA0:", 550);
    command(server, &client, "RNFR /UX0:/DATA", 550);
    CHECK(fake_fs_exists("ux0:"));
    CHECK(fake_fs_exists("ur0:"));
    CHECK(fake_fs_exists("uma0:"));

    ftp_server_destroy(server);
}

static void test_cwd(void)
{
    setup();
    fake_fs_add_dir("ux0:data/SUB");
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    command(server, &client, "CWD /UX0:DATA", 250);
    char text[512];
    command_text(server, &client, "PWD", 257, text, sizeof(text));
    CHECK(strstr(text, "/UX0:/DATA") != NULL);
    command(server, &client, "CDUP", 250);
    command_text(server, &client, "PWD", 257, text, sizeof(text));
    CHECK(strstr(text, "/UX0:") != NULL);

    command(server, &client, "CWD /UX0:/DATA", 250);
    command_text(server, &client, "PWD", 257, text, sizeof(text));
    CHECK(strstr(text, "/UX0:/DATA") != NULL);

    command(server, &client, "CWD /../../", 250);
    command_text(server, &client, "PWD", 257, text, sizeof(text));
    CHECK(strstr(text, "\"/\"") != NULL);

    command(server, &client, "CWD /NOPE", 550);
    command_text(server, &client, "PWD", 257, text, sizeof(text));
    CHECK(strstr(text, "\"/\"") != NULL);

    command(server, &client, "CDUP", 250);

    ftp_server_destroy(server);
}

static void test_mlst_size_mdtm(void)
{
    setup();
    fake_fs_add_file("ux0:data/f.txt", 5);
    fake_fs_add_dir("ux0:data/vitaFTPShell");
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    command(server, &client, "MLST /UX0:/DATA/f.txt", 250);
    command(server, &client, "MLST /", 250);
    command(server, &client, "MLST /UX0:/DATA", 250);
    command(server, &client, "MLST ux0:data/vitaFTPShell", 250);
    command(server, &client, "SIZE /UX0:/DATA/f.txt", 213);
    command(server, &client, "MDTM /UX0:/DATA/f.txt", 550);

    fake_ftp_client_send(&client, "MLST /UX0:/DATA/f.txt");
    ftp_server_tick(server, 0);
    int code = 0;
    char text[512];
    CHECK(read_reply(&client, &code, text, sizeof(text)) == 1);
    CHECK(code == 250);
    CHECK(strstr(text, "type=file;size=5;") != NULL);

    ftp_server_destroy(server);
}

static void test_file_ops(void)
{
    setup();
    fake_fs_seed_file("ux0:data/a.txt", "a", 1);
    fake_fs_seed_file("ux0:data/r1.txt", "r", 1);
    fake_fs_add_dir("ux0:data/FULL");
    fake_fs_add_file("ux0:data/FULL/x", 1);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    command(server, &client, "RNTO /x", 503);
    command(server, &client, "MKD /UX0:/DATA/NEW", 250);
    CHECK(fake_fs_exists("ux0:data/NEW"));
    command(server, &client, "MKD /UX0:/DATA/NEW", 550);
    command(server, &client, "RMD /UX0:/DATA/NEW", 250);
    CHECK(!fake_fs_exists("ux0:data/NEW"));

    command(server, &client, "RMD /UX0:/DATA/FULL", 550);
    CHECK(fake_fs_exists("ux0:data/FULL/x"));
    command(server, &client, "DELE /UX0:/DATA/FULL", 550);

    command(server, &client, "DELE /UX0:/DATA/a.txt", 250);
    CHECK(!fake_fs_exists("ux0:data/a.txt"));

    command(server, &client, "RNFR /UX0:/DATA/r1.txt", 350);
    command(server, &client, "RNTO /UX0:/DATA/r2.txt", 250);
    CHECK(fake_fs_exists("ux0:data/r2.txt"));
    CHECK(!fake_fs_exists("ux0:data/r1.txt"));

    command(server, &client, "MKD ur0:tai/NEW", 250);
    CHECK(fake_fs_exists("ur0:tai/NEW"));
    command(server, &client, "RMD ur0:tai/NEW", 250);
    CHECK(!fake_fs_exists("ur0:tai/NEW"));

    command(server, &client, "TYPE A", 200);
    command(server, &client, "TYPE E", 504);
    command(server, &client, "MODE S", 200);
    command(server, &client, "MODE B", 504);
    command(server, &client, "STRU F", 200);
    command(server, &client, "STRU R", 504);

    ftp_server_destroy(server);
}

static void test_mlst_mtime(void)
{
    setup();
    fake_fs_seed_file("ux0:data/t.txt", "x", 1);
    fake_fs_set_mtime("ux0:data/t.txt", 1767225600u);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    char text[512];
    command_text(server, &client, "MDTM /UX0:/DATA/t.txt", 213, text,
                 sizeof(text));
    CHECK(strcmp(text, "20260101000000") == 0);

    command_text(server, &client, "MLST /UX0:/DATA/t.txt", 250, text,
                 sizeof(text));
    CHECK(strstr(text, "modify=20260101000000;") != NULL);
    CHECK(strstr(text, "size=1;") != NULL);

    command_text(server, &client, "SIZE /UX0:/DATA/t.txt", 213, text,
                 sizeof(text));
    CHECK(strcmp(text, "1") == 0);

    ftp_server_destroy(server);
}

static void test_root_absent_volume(void)
{
    fake_net_reset();
    fake_fs_reset();
    fake_clock_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ur0:");
    fake_net_set_link(1, 1, PEER_ADDR, 0);

    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    queue_data_peer();
    NetSocket *data = data_command(server, &client, "LIST /", 150);
    CHECK(data != NULL);
    finish_data(server, &client, 226);
    char tx[2048];
    size_t n = fake_net_read_tx(data, tx, sizeof(tx) - 1);
    tx[n] = '\0';
    CHECK(strstr(tx, "UX0:") != NULL);
    CHECK(strstr(tx, "UR0:") != NULL);
    CHECK(strstr(tx, "UMA0:") == NULL);

    ftp_server_destroy(server);
}

static void test_client_limit(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);

    FakeFtpClient clients[4];
    connect_session(server, &clients[0]);
    for (int i = 1; i < 4; ++i)
        connect_second_session(server, &clients[i], PEER_ADDR + (uint32_t)i,
                               (uint16_t)(PEER_PORT + 20 + i));
    CHECK(ftp_server_clients(server) == 4);

    /* The fifth control connection is refused without disturbing the four. */
    unsigned before = fake_net_accept_count();
    fake_net_queue_accept(fake_net_listener_at(0), 0x0A000009u, 41000);
    ftp_server_tick(server, 0);
    CHECK(fake_net_accept_count() == before + 1);
    NetSocket *fifth = fake_net_accepted_at(before);
    CHECK(fifth != NULL);
    int code = 0;
    char text[128];
    CHECK(read_reply(&clients[0], &code, text, sizeof(text)) == 0);
    char raw[64];
    size_t n = fake_net_read_tx(fifth, raw, sizeof(raw) - 1);
    raw[n] = '\0';
    CHECK(strstr(raw, "450") != NULL);
    CHECK(!fake_net_is_open(fifth));
    CHECK(ftp_server_clients(server) == 4);

    command(server, &clients[3], "QUIT", 221);
    CHECK(ftp_server_clients(server) == 3);
    command(server, &clients[0], "NOOP", 200);

    ftp_server_destroy(server);
}

static void test_busy_and_abort(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    queue_data_peer();
    NetSocket *data = data_command(server, &client, "STOR ux0:/data/busy.bin", 150);
    CHECK(data != NULL);
    CHECK(ftp_server_busy(server));

    command(server, &client, "DELE ux0:data/a.txt", 450);
    command(server, &client, "STOR ux0:data/b.bin", 450);
    command(server, &client, "PASV", 450);
    command(server, &client, "ABOR", 426);
    int code = 0;
    char text[512];
    CHECK(read_reply(&client, &code, text, sizeof(text)) == 1);
    CHECK(code == 226);
    command(server, &client, "NOOP", 200);
    CHECK(!fake_fs_exists("ux0:/data/busy.bin"));
    CHECK(!fake_fs_exists("ux0:/data/busy.bin.vftp.part"));

    ftp_server_destroy(server);
}

static void test_control_write_failure(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    queue_data_peer();
    NetSocket *data =
        data_command(server, &client, "STOR ux0:data/live.bin", 150);
    CHECK(data != NULL);
    CHECK(fake_fs_exists("ux0:data/live.bin.vftp.part"));

    fake_ftp_client_send(&client, "NOOP");
    fake_net_fail_send_next(NET_ERROR);
    ftp_server_tick(server, 0);

    CHECK(ftp_server_state(server) == FTP_SERVER_ON);
    CHECK(ftp_server_clients(server) == 0);
    CHECK(!ftp_server_busy(server));
    CHECK(!fake_fs_exists("ux0:data/live.bin.vftp.part"));
    CHECK(!fake_fs_exists("ux0:data/live.bin"));
    CHECK(!fake_net_is_open(data));
    CHECK(cap.done >= 1);
    CHECK(cap.ok == 0);
    CHECK(cap.cause == FTP_CAUSE_NETWORK);

    ftp_server_destroy(server);
}

static void test_peer_check(void)
{
    setup();
    fake_fs_seed_file("ux0:data/f.txt", "data", 4);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    /* The 150 follows the data accept, so a foreign-only peer produces no
     * reply until the accept deadline passes. */
    fake_ftp_client_send(&client, "RETR /UX0:/DATA/f.txt");
    ftp_server_tick(server, 0);
    fake_net_queue_accept(fake_net_listener_at(1), 0x0A000009u, 40001);
    ftp_server_tick(server, 0);
    NetSocket *foreign = fake_net_accepted_at(1);
    CHECK(foreign != NULL);
    CHECK(!fake_net_is_open(foreign));
    CHECK(fake_net_tx_len(foreign) == 0);

    /* A foreign peer is dropped without failing the command; the command
     * fails only when the accept deadline passes with no matching peer. */
    fake_clock_advance(6000);
    wait_reply(server, &client, 425);

    ftp_server_destroy(server);
}

/* A foreign peer queued first must not consume the legitimate data peer. */
static void test_peer_check_recovers(void)
{
    setup();
    fake_fs_seed_file("ux0:data/f.txt", "data", 4);
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    fake_net_queue_accept(fake_net_listener_at(1), 0x0A000009u, 40001);
    queue_data_peer();
    NetSocket *data =
        data_command(server, &client, "RETR /UX0:/DATA/f.txt", 150);
    CHECK(data != NULL);
    NetSocket *foreign = fake_net_accepted_at(1);
    CHECK(foreign != NULL);
    CHECK(!fake_net_is_open(foreign));

    ftp_server_destroy(server);
}

/* A control peer that sends nothing loses the session instead of holding the
 * only slot forever. */
static void test_control_idle(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    fake_clock_advance(31000);
    ftp_server_tick(server, 0);
    int code = 0;
    char text[128];
    CHECK(read_reply(&client, &code, text, sizeof(text)) == 1);
    CHECK(code == 421);
    CHECK(ftp_server_clients(server) == 0);

    ftp_server_destroy(server);
}

static void test_port_rest_502(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    command(server, &client, "PORT 192,168,1,42,5,58", 502);
    command(server, &client, "EPRT |1|192.168.1.42|1338|", 502);
    command(server, &client, "REST 10", 502);
    command(server, &client, "APPE /x", 502);
    command(server, &client, "FROB", 502);
    command(server, &client, "LIST /", 425);

    ftp_server_destroy(server);
}

static void test_quit(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    command(server, &client, "QUIT", 221);
    CHECK(ftp_server_state(server) == FTP_SERVER_ON);
    CHECK(ftp_server_clients(server) == 0);

    FakeFtpClient second;
    fake_net_queue_accept(fake_net_listener_at(0), PEER_ADDR, 40100);
    ftp_server_tick(server, 0);
    NetSocket *control = fake_net_accepted_at(1);
    CHECK(control != NULL);
    fake_ftp_client_init(&second, control);
    int code = 0;
    char text[512];
    CHECK(read_reply(&second, &code, text, sizeof(text)) == 1);
    CHECK(code == 220);

    ftp_server_destroy(server);
}

static void test_network_loss(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    NetSocket *control_listener = fake_net_listener_at(0);

    fake_net_set_link(0, 1, PEER_ADDR, 0);
    fake_clock_advance(1000);
    ftp_server_tick(server, 0);

    CHECK(ftp_server_state(server) == FTP_SERVER_OFF);
    CHECK(ftp_server_clients(server) == 0);
    CHECK(cap.errors == 1);
    CHECK(strstr(cap.detail, "NETWORK LOST") != NULL);
    CHECK(control_listener != NULL);
    CHECK(!fake_net_is_open(control_listener));

    ftp_server_destroy(server);
}

static void test_stop(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    ftp_server_stop(server);
    CHECK(ftp_server_state(server) == FTP_SERVER_OFF);
    CHECK(ftp_server_clients(server) == 0);
    CHECK(!fake_net_is_open(fake_net_listener_at(0)));
    CHECK(!fake_net_is_open(fake_net_listener_at(1)));

    ftp_server_destroy(server);
}

int main(void)
{
    test_start_and_bind();
    test_login();
    test_list_mlsd_nlst();
    test_root_listing();
    test_retr();
    test_stor();
    test_stor_connect_after_command();
    test_two_clients();
    test_client_event_pairs_session();
    test_close_while_armed();
    test_owner_close_active_one_event();
    test_foreign_abor_keeps_pasv();
    test_storage_failure_451();
    test_tree_command_releases();
    test_rename_sidecar_refused();
    test_owner_size_while_download();
    test_trace_masks_password();
    test_trace_drops_password();
    test_owner_size_while_upload();
    test_held_path();
    test_held_rename();
    test_denials();
    test_cwd();
    test_file_ops();
    test_mlst_mtime();
    test_root_absent_volume();
    test_mlst_size_mdtm();
    test_client_limit();
    test_busy_and_abort();
    test_control_write_failure();
    test_peer_check();
    test_peer_check_recovers();
    test_control_idle();
    test_port_rest_502();
    test_quit();
    test_network_loss();
    test_stop();

    for (int st = FTP_SERVER_OFF; st <= FTP_SERVER_STOPPING; ++st)
        CHECK((s_states_seen & (1u << st)) != 0);
    CHECK(fake_net_overflow_count() == 0);

    if (s_failures != 0) {
        fprintf(stderr, "test_server: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_server: OK\n");
    return 0;
}
