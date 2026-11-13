/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/ftp/client.h"
#include "fake_clock.h"
#include "fake_fs.h"
#include "fake_ftpd.h"
#include "fake_net.h"

static int s_failures;
static unsigned s_states_seen;
static unsigned s_data_seen;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

#define MAX_ROWS 64

typedef struct {
    FtpClientState state;
    FtpStage stage;
    FtpCause cause;
    NetResult net;

    int rows;
    ListingEntry row[MAX_ROWS];
    int begin;
    int end;
    int end_ok;

    int done;
    FtpClientOperation op;
    int ok;
    FtpCause done_cause;
    FtpStage done_stage;
    char detail[512];

    int progress;
    uint64_t bytes;
    uint64_t total;
    int total_known;
    int exists;
    int is_dir;
    int unsupported;
    uint64_t file_size;
    int file_size_known;
} Capture;

static void capture_event(void *ctx, const FtpClientEvent *event)
{
    Capture *cap = ctx;
    switch (event->kind) {
    case FTP_CLIENT_EVENT_STATE:
        cap->state = event->state;
        cap->stage = event->stage;
        cap->cause = event->cause;
        cap->net = event->net;
        s_states_seen |= 1u << event->state;
        s_data_seen |= 1u << event->data_state;
        break;
    case FTP_CLIENT_EVENT_LISTING_BEGIN:
        cap->begin++;
        break;
    case FTP_CLIENT_EVENT_LISTING_ROW:
        if (cap->rows < MAX_ROWS && event->row != NULL)
            cap->row[cap->rows++] = *event->row;
        break;
    case FTP_CLIENT_EVENT_LISTING_END:
        cap->end++;
        cap->end_ok = event->ok;
        break;
    case FTP_CLIENT_EVENT_PROGRESS:
        cap->progress++;
        cap->bytes = event->bytes;
        cap->total = event->total;
        cap->total_known = event->total_known;
        s_data_seen |= 1u << event->data_state;
        break;
    case FTP_CLIENT_EVENT_OPERATION_DONE:
        cap->done++;
        cap->op = event->operation;
        cap->ok = event->ok;
        cap->done_cause = event->cause;
        cap->done_stage = event->stage;
        if (event->detail != NULL)
            snprintf(cap->detail, sizeof(cap->detail), "%s", event->detail);
        else
            cap->detail[0] = '\0';
        cap->exists = event->exists;
        cap->is_dir = event->is_dir;
        cap->unsupported = event->unsupported;
        cap->file_size = event->file_size;
        cap->file_size_known = event->file_size_known;
        break;
    }
}

static void capture_reset_rows(Capture *cap)
{
    cap->rows = 0;
    cap->begin = 0;
    cap->end = 0;
    cap->end_ok = 0;
    cap->progress = 0;
    cap->bytes = 0;
    cap->total = 0;
    cap->total_known = 0;
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
}

static void observe(FtpClient *client)
{
    s_states_seen |= 1u << ftp_client_state(client);
    s_data_seen |= 1u << ftp_client_data_state(client);
}

static FtpClient *make_client(Capture *cap)
{
    FtpClient *client = ftp_client_create(fake_net_port(), fake_fs_port(),
                                          fake_clock_port(), capture_event, cap);
    observe(client);
    return client;
}

static void tick(FtpClient *client)
{
    observe(client);
    ftp_client_tick(client);
    observe(client);
}

static void tick_until_settled(FtpClient *client)
{
    for (int i = 0; i < 8; ++i) {
        FtpClientState state = ftp_client_state(client);
        if (state == FTP_CLIENT_READY || state == FTP_CLIENT_IDLE ||
            state == FTP_CLIENT_LOST || state == FTP_CLIENT_FAILED ||
            state == FTP_CLIENT_STALLED)
            return;
        tick(client);
    }
}

static void expect_cmd(FakeFtpd *server, const char *expected)
{
    char line[512];
    line[0] = '\0';
    int ok = fake_ftpd_next_command(server, line, sizeof(line));
    if (ok != 1) {
        fprintf(stderr, "no command, expected [%s]\n", expected);
        s_failures++;
        return;
    }
    if (strcmp(line, expected) != 0) {
        fprintf(stderr, "command expected [%s] got [%s]\n", expected, line);
        s_failures++;
    }
}

static void reply(FakeFtpd *server, int code, const char *text)
{
    fake_ftpd_reply(server, code, text);
}

/*
 * Log in and answer through FEAT. On return the client has just sent PASV and
 * the caller owns the data exchange.
 */
static void connect_login(FtpClient *client, FakeFtpd *server, Capture *cap,
                          int anonymous, const char *user, const char *pass,
                          int with_mlsd)
{
    (void)cap;
    FtpClientLogin login;
    memset(&login, 0, sizeof(login));
    login.host = "ftp.test";
    login.port = 21;
    login.anonymous = anonymous;
    login.username = user;
    login.password = pass;

    CHECK(ftp_client_connect(client, &login) == FTP_CLIENT_OK);
    fake_ftpd_init(server, fake_net_last_connected());

    reply(server, 220, "Welcome.");
    tick(client);
    expect_cmd(server, anonymous ? "USER anonymous" : "USER bob");
    reply(server, 331, "Password required.");
    tick(client);
    expect_cmd(server, anonymous ? "PASS anonymous@" : "PASS secret");
    reply(server, 230, "Logged in.");
    tick(client);
    expect_cmd(server, "TYPE I");
    reply(server, 200, "Type set.");
    tick(client);
    expect_cmd(server, "PWD");
    reply(server, 257, "\"/\" is the current directory.");
    tick(client);
    expect_cmd(server, "FEAT");
    if (with_mlsd)
        fake_ftpd_send_raw(server,
                           "211-Features:\r\n"
                           " MLST type*;size*;modify*;perm*;\r\n"
                           "211 End\r\n");
    else
        reply(server, 211, "Features: none");
    tick(client);
    expect_cmd(server, "PASV");
}

static void finish_connect(FakeFtpd *server, FtpClient *client, int with_mlsd,
                           const char *payload, NetSocket **data_out)
{
    reply(server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);

    NetSocket *data = fake_net_last_connected();
    if (data_out != NULL)
        *data_out = data;

    expect_cmd(server, with_mlsd ? "MLSD /" : "LIST /");
    reply(server, 150, "Opening data connection.");
    tick(client);

    if (payload != NULL && payload[0] != '\0')
        fake_net_queue_rx(data, payload, strlen(payload));
    fake_net_shutdown(data);
    reply(server, 226, "Transfer complete.");
    tick_until_settled(client);
}

static void connect_ok(FtpClient *client, FakeFtpd *server, Capture *cap,
                       NetSocket **data_out)
{
    connect_login(client, server, cap, 1, NULL, NULL, 1);
    finish_connect(server, client, 1, NULL, data_out);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    capture_reset_rows(cap);
}

static void test_anonymous_connect_real(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_login(client, &server, &cap, 1, NULL, NULL, 1);
    finish_connect(&server, client, 1, NULL, NULL);

    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(ftp_client_has_mlsd(client) == 1);
    CHECK(strcmp(ftp_client_cwd_text(client), "/") == 0);
    CHECK(cap.done == 1);
    CHECK(cap.op == FTP_CLIENT_OP_CONNECT);
    CHECK(cap.ok == 1);
    CHECK(cap.begin == 1);
    CHECK(cap.end == 1);
    CHECK(cap.end_ok == 1);

    ftp_client_destroy(client);
}

static void test_login_site(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_login(client, &server, &cap, 0, "bob", "secret", 1);
    finish_connect(&server, client, 1, NULL, NULL);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);

    ftp_client_destroy(client);
}

static void test_greeting_120(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);

    FtpClientLogin login = {"ftp.test", 21, 1, NULL, NULL};
    CHECK(ftp_client_connect(client, &login) == FTP_CLIENT_OK);
    FakeFtpd server;
    fake_ftpd_init(&server, fake_net_last_connected());

    fake_ftpd_send_raw(&server, "120 Wait.\r\n220 Ready.\r\n");
    tick(client);
    expect_cmd(&server, "USER anonymous");

    ftp_client_destroy(client);
}

static void test_login_530(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);

    FtpClientLogin login = {"ftp.test", 21, 1, NULL, NULL};
    CHECK(ftp_client_connect(client, &login) == FTP_CLIENT_OK);
    FakeFtpd server;
    fake_ftpd_init(&server, fake_net_last_connected());

    reply(&server, 220, "Welcome.");
    tick(client);
    expect_cmd(&server, "USER anonymous");
    reply(&server, 331, "Password required.");
    tick(client);
    expect_cmd(&server, "PASS anonymous@");
    reply(&server, 530, "Login incorrect.");
    tick(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_FAILED);
    CHECK(ftp_client_stage(client) == FTP_STAGE_LOGIN);
    CHECK(cap.done == 1);
    CHECK(cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_SERVER);
    CHECK(cap.done_stage == FTP_STAGE_LOGIN);
    CHECK(strstr(cap.detail, "Login incorrect") != NULL);
    const Reply *last = ftp_client_last_reply(client);
    CHECK(last != NULL && last->code == 530);

    ftp_client_destroy(client);
}

static void test_feat_fallback(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_login(client, &server, &cap, 1, NULL, NULL, 0);
    finish_connect(&server, client, 0,
                   "-rw-r--r-- 1 u g 1234 Nov 08 12:00 FILE NAME\r\n", NULL);

    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(ftp_client_has_mlsd(client) == 0);
    CHECK(cap.rows == 1);
    CHECK(strcmp(cap.row[0].name, "FILE NAME") == 0);
    CHECK(cap.row[0].type == LISTING_FILE);
    CHECK(cap.row[0].size_known == 1 && cap.row[0].size == 1234);

    ftp_client_destroy(client);
}

static void test_mlsd_fallback(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_login(client, &server, &cap, 1, NULL, NULL, 1);

    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    NetSocket *first = fake_net_last_connected();
    expect_cmd(&server, "MLSD /");
    reply(&server, 550, "MLSD not supported.");
    tick(client);
    CHECK(!fake_net_is_open(first));

    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,141).");
    tick(client);
    NetSocket *second = fake_net_last_connected();
    CHECK(fake_net_is_open(second));

    expect_cmd(&server, "LIST /");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    fake_net_queue_rx(second,
                      "drwxr-xr-x 2 u g 4096 Nov 08 12:00 FOLDER\r\n",
                      strlen("drwxr-xr-x 2 u g 4096 Nov 08 12:00 FOLDER\r\n"));
    fake_net_shutdown(second);
    reply(&server, 226, "Transfer complete.");
    tick_until_settled(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(cap.rows == 1);
    CHECK(strcmp(cap.row[0].name, "FOLDER") == 0);
    CHECK(cap.row[0].type == LISTING_DIR);

    ftp_client_destroy(client);
}

static void test_mlsd_spaces(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_login(client, &server, &cap, 1, NULL, NULL, 1);
    finish_connect(&server, client, 1,
                   "type=file;size=12;modify=20260101120000; name with "
                   "spaces\r\n",
                   NULL);

    CHECK(cap.rows == 1);
    CHECK(strcmp(cap.row[0].name, "name with spaces") == 0);
    CHECK(cap.row[0].size == 12);
    CHECK(cap.row[0].mtime_known == 1);

    ftp_client_destroy(client);
}

static void test_cwd(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    CHECK(ftp_client_cwd(client, "/UX0:/DATA") == FTP_CLIENT_OK);
    expect_cmd(&server, "CWD /UX0:/DATA");
    reply(&server, 250, "Directory changed.");
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(strcmp(ftp_client_cwd_text(client), "/UX0:/DATA") == 0);
    CHECK(cap.done == 2 && cap.ok == 1);
    CHECK(cap.op == FTP_CLIENT_OP_CWD);

    CHECK(ftp_client_cwd(client, "/NOPE") == FTP_CLIENT_OK);
    expect_cmd(&server, "CWD /NOPE");
    reply(&server, 550, "Not found.");
    tick(client);
    CHECK(strcmp(ftp_client_cwd_text(client), "/UX0:/DATA") == 0);
    CHECK(cap.done == 3 && cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_SERVER);

    ftp_client_destroy(client);
}

static void test_download(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/UX0:/DATA/dl.bin", "ux0:data/dl.bin", 0,
                                  100, 1};
    CHECK(ftp_client_download(client, &transfer, NULL) == FTP_CLIENT_OK);
    CHECK(cap.total == 100 && cap.total_known == 1);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);

    NetSocket *data = fake_net_last_connected();
    expect_cmd(&server, "RETR /UX0:/DATA/dl.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);

    fake_net_queue_rx(data, "hello", 5);
    fake_net_shutdown(data);
    tick(client);
    tick(client);
    CHECK(ftp_client_data_state(client) == FTP_DATA_DRAINING);
    reply(&server, 226, "Transfer complete.");
    tick_until_settled(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(cap.done == 2 && cap.ok == 1);
    CHECK(cap.op == FTP_CLIENT_OP_DOWNLOAD);
    CHECK(cap.bytes == 5);

    char buf[16];
    size_t n = fake_fs_read_file("ux0:data/dl.bin", buf, sizeof(buf));
    CHECK(n == 5 && memcmp(buf, "hello", 5) == 0);
    CHECK(!fake_fs_exists("ux0:data/dl.bin.vftp.part"));

    fake_fs_add_dir("ux0:data/adir");
    FtpClientTransfer dir = {"/x", "ux0:data/adir", 0, 0, 0};
    CHECK(ftp_client_download(client, &dir, NULL) == FTP_CLIENT_OK);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(cap.done == 3 && cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_STORAGE);
    CHECK(!fake_fs_exists("ux0:data/adir.vftp.part"));

    ftp_client_destroy(client);
}

static void test_upload(void)
{
    setup();
    fake_fs_seed_file("ux0:data/up.bin", "abc", 3);
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/UX0:/DATA/up.bin", "ux0:data/up.bin", 0, 0, 0};
    CHECK(ftp_client_upload(client, &transfer) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);

    NetSocket *data = fake_net_last_connected();
    expect_cmd(&server, "STOR /UX0:/DATA/up.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);

    tick(client);
    char sent[16];
    size_t n = fake_net_read_tx(data, sent, sizeof(sent));
    CHECK(n == 3 && memcmp(sent, "abc", 3) == 0);

    tick(client);
    CHECK(!fake_net_is_open(data));
    reply(&server, 226, "Transfer complete.");
    tick_until_settled(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(cap.done == 2 && cap.ok == 1);
    CHECK(cap.op == FTP_CLIENT_OP_UPLOAD);
    CHECK(cap.total_known == 1 && cap.total == 3);

    /* A listing after an upload must send MLSD/LIST, not the old STOR. */
    capture_reset_rows(&cap);
    CHECK(ftp_client_list(client, "/") == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    NetSocket *list_data = fake_net_last_connected();
    expect_cmd(&server, "MLSD /");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    fake_net_queue_rx(list_data, "type=file;size=1; X.TXT\r\n",
                      strlen("type=file;size=1; X.TXT\r\n"));
    fake_net_shutdown(list_data);
    reply(&server, 226, "Transfer complete.");
    tick_until_settled(client);
    CHECK(cap.rows == 1 && cap.end == 1 && cap.end_ok == 1);

    ftp_client_destroy(client);
}

static void test_download_reset(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/x.bin", "ux0:data/x.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &transfer, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    NetSocket *data = fake_net_last_connected();
    expect_cmd(&server, "RETR /x.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);

    fake_net_queue_rx(data, "he", 2);
    fake_net_reset_peer(data);
    tick_until_settled(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(cap.done == 2 && cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_NETWORK);
    CHECK(!fake_fs_exists("ux0:data/x.bin"));
    CHECK(!fake_fs_exists("ux0:data/x.bin.vftp.part"));

    expect_cmd(&server, "ABOR");
    reply(&server, 426, "Transfer aborted.");
    reply(&server, 226, "Abort successful.");
    tick(client);
    fake_clock_advance(260);
    tick(client);
    CHECK(!ftp_client_busy(client));

    ftp_client_destroy(client);
}

static void test_control_drop(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/x.bin", "ux0:data/x.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &transfer, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    NetSocket *data = fake_net_last_connected();
    expect_cmd(&server, "RETR /x.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    fake_net_queue_rx(data, "he", 2);
    fake_net_shutdown(fake_net_last_connected());
    fake_net_shutdown(server.control);
    tick(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_LOST);
    CHECK(cap.done == 2 && cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_NETWORK);
    CHECK(!fake_fs_exists("ux0:data/x.bin"));
    CHECK(!fake_fs_exists("ux0:data/x.bin.vftp.part"));

    ftp_client_destroy(client);
}

static void test_resolve_timeout(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);

    fake_net_fail_next(NET_TIMEOUT);
    FtpClientLogin login = {"ftp.test", 21, 1, NULL, NULL};
    CHECK(ftp_client_connect(client, &login) == FTP_CLIENT_OK);
    CHECK(ftp_client_state(client) == FTP_CLIENT_STALLED);
    CHECK(ftp_client_stage(client) == FTP_STAGE_RESOLVE);
    CHECK(cap.done == 1 && cap.done_stage == FTP_STAGE_RESOLVE);

    ftp_client_destroy(client);
}

static void test_connect_refused(void)
{
    setup();
    fake_net_set_connect_result(NET_REFUSED);

    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);

    FtpClientLogin login = {"ftp.test", 21, 1, NULL, NULL};
    CHECK(ftp_client_connect(client, &login) == FTP_CLIENT_OK);
    CHECK(ftp_client_state(client) == FTP_CLIENT_FAILED);
    CHECK(cap.stage == FTP_STAGE_CONNECT);
    CHECK(cap.cause == FTP_CAUSE_NETWORK);
    CHECK(cap.net == NET_REFUSED);
    ftp_client_destroy(client);
}

static void test_connect_timeout(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);

    fake_net_set_connect_result(NET_TIMEOUT);
    FtpClientLogin login = {"ftp.test", 21, 1, NULL, NULL};
    CHECK(ftp_client_connect(client, &login) == FTP_CLIENT_OK);
    CHECK(ftp_client_state(client) == FTP_CLIENT_STALLED);
    CHECK(ftp_client_stage(client) == FTP_STAGE_CONNECT);

    ftp_client_destroy(client);
}

static void test_login_timeout(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);

    FtpClientLogin login = {"ftp.test", 21, 1, NULL, NULL};
    CHECK(ftp_client_connect(client, &login) == FTP_CLIENT_OK);
    FakeFtpd server;
    fake_ftpd_init(&server, fake_net_last_connected());
    reply(&server, 220, "Welcome.");
    tick(client);
    expect_cmd(&server, "USER anonymous");

    fake_clock_advance(5001);
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_STALLED);
    CHECK(ftp_client_stage(client) == FTP_STAGE_LOGIN);

    ftp_client_destroy(client);
}

static void test_first_listing_timeout(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_login(client, &server, &cap, 1, NULL, NULL, 1);
    /* The client has sent PASV; do not answer it. */
    fake_clock_advance(10001);
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_STALLED);
    CHECK(ftp_client_stage(client) == FTP_STAGE_FIRST_LISTING);

    ftp_client_destroy(client);
}

static void test_data_idle(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/x.bin", "ux0:data/x.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &transfer, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    expect_cmd(&server, "RETR /x.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);

    fake_clock_advance(20001);
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(cap.done == 2 && cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_NETWORK);
    CHECK(!fake_fs_exists("ux0:data/x.bin"));

    expect_cmd(&server, "ABOR");
    reply(&server, 426, "Transfer aborted.");
    reply(&server, 226, "Abort successful.");
    tick(client);
    fake_clock_advance(260);
    tick(client);
    CHECK(!ftp_client_busy(client));

    ftp_client_destroy(client);
}

static void test_cancel_alignment(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/x.bin", "ux0:data/x.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &transfer, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    NetSocket *data = fake_net_last_connected();
    expect_cmd(&server, "RETR /x.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    fake_net_queue_rx(data, "abc", 3);
    tick(client);

    CHECK(ftp_client_cancel(client) == FTP_CLIENT_OK);
    expect_cmd(&server, "ABOR");
    CHECK(cap.done == 2 && cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_USER);

    reply(&server, 426, "Transfer aborted.");
    reply(&server, 226, "Abort successful.");
    tick(client);
    fake_clock_advance(260);
    tick(client);
    CHECK(!ftp_client_busy(client));

    CHECK(ftp_client_cwd(client, "/AFTER") == FTP_CLIENT_OK);
    expect_cmd(&server, "CWD /AFTER");
    reply(&server, 250, "Directory changed.");
    tick(client);
    CHECK(strcmp(ftp_client_cwd_text(client), "/AFTER") == 0);
    CHECK(cap.done == 3 && cap.ok == 1);

    ftp_client_destroy(client);
}

static void test_pasv_nat(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    CHECK(ftp_client_list(client, "/") == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    /* A PASV host different from the control peer is ignored. The client must
     * use the control peer and the advertised port 5004, not fail. */
    reply(&server, 227, "Entering Passive Mode (10,0,0,1,19,140).");
    tick(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_LISTING);

    ftp_client_destroy(client);
}

static void test_450_disarms(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/a.bin", "ux0:data/a.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &transfer, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    NetSocket *first = fake_net_last_connected();
    expect_cmd(&server, "RETR /a.bin");
    reply(&server, 450, "Busy.");
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(!fake_net_is_open(first));
    CHECK(cap.done == 2 && cap.ok == 0);
    CHECK(cap.done_cause == FTP_CAUSE_SERVER);

    FtpClientTransfer second = {"/b.bin", "ux0:data/b.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &second, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,141).");
    tick(client);
    NetSocket *fresh = fake_net_last_connected();
    CHECK(fake_net_is_open(fresh));
    expect_cmd(&server, "RETR /b.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    CHECK(ftp_client_cancel(client) == FTP_CLIENT_OK);
    expect_cmd(&server, "ABOR");
    reply(&server, 426, "Transfer aborted.");
    reply(&server, 226, "Abort successful.");
    tick(client);

    ftp_client_destroy(client);
}

static void test_high_risk(void)
{
    setup();
    fake_fs_add_dir("ur0:tai");
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/x.suprx", "ur0:tai/x.suprx", 0, 0, 0};
    PathRisk risk = PATH_RISK_NORMAL;
    CHECK(ftp_client_download(client, &transfer, &risk) ==
          FTP_CLIENT_NEED_RISK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(!fake_fs_exists("ur0:tai/x.suprx"));

    transfer.risk_approved = 1;
    CHECK(ftp_client_download(client, &transfer, &risk) == FTP_CLIENT_OK);
    CHECK(ftp_client_state(client) == FTP_CLIENT_TRANSFERRING);
    CHECK(!fake_fs_exists("ur0:tai/x.suprx"));

    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    expect_cmd(&server, "RETR /x.suprx");

    CHECK(ftp_client_cancel(client) == FTP_CLIENT_OK);
    expect_cmd(&server, "ABOR");
    reply(&server, 426, "Transfer aborted.");
    reply(&server, 226, "Abort successful.");
    tick(client);
    CHECK(!fake_fs_exists("ur0:tai/x.suprx.vftp.part"));

    ftp_client_destroy(client);
}

static void test_simple_commands(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    CHECK(ftp_client_mkdir(client, "/NEW") == FTP_CLIENT_OK);
    expect_cmd(&server, "MKD /NEW");
    reply(&server, 250, "Directory created.");
    tick(client);
    CHECK(cap.done == 2 && cap.ok == 1 && cap.op == FTP_CLIENT_OP_MKDIR);

    CHECK(ftp_client_rename(client, "/OLD", "/NEW2") == FTP_CLIENT_OK);
    expect_cmd(&server, "RNFR /OLD");
    reply(&server, 350, "Ready.");
    tick(client);
    expect_cmd(&server, "RNTO /NEW2");
    reply(&server, 250, "Renamed.");
    tick(client);
    CHECK(cap.done == 3 && cap.ok == 1 && cap.op == FTP_CLIENT_OP_RENAME);

    CHECK(ftp_client_delete(client, "/FILE") == FTP_CLIENT_OK);
    expect_cmd(&server, "DELE /FILE");
    reply(&server, 250, "Deleted.");
    tick(client);
    CHECK(cap.done == 4 && cap.ok == 1 && cap.op == FTP_CLIENT_OP_DELETE);

    CHECK(ftp_client_rmdir(client, "/DIR") == FTP_CLIENT_OK);
    expect_cmd(&server, "RMD /DIR");
    reply(&server, 250, "Removed.");
    tick(client);
    CHECK(cap.done == 5 && cap.ok == 1 && cap.op == FTP_CLIENT_OP_RMDIR);

    CHECK(ftp_client_quit(client) == FTP_CLIENT_OK);
    expect_cmd(&server, "QUIT");
    reply(&server, 221, "Goodbye.");
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_IDLE);
    CHECK(cap.done == 6 && cap.ok == 1 && cap.op == FTP_CLIENT_OP_QUIT);

    ftp_client_destroy(client);
}

static void test_nlst(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    CHECK(ftp_client_nlst(client, "/") == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    NetSocket *data = fake_net_last_connected();
    expect_cmd(&server, "NLST /");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    fake_net_queue_rx(data, "a.txt\r\nb.txt\r\n", 14);
    fake_net_shutdown(data);
    reply(&server, 226, "Transfer complete.");
    tick_until_settled(client);

    CHECK(cap.rows == 2);
    CHECK(strcmp(cap.row[0].name, "a.txt") == 0);
    CHECK(strcmp(cap.row[1].name, "b.txt") == 0);
    CHECK(cap.end == 1 && cap.end_ok == 1);

    ftp_client_destroy(client);
}

static void test_abort_reverse_order(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    FtpClientTransfer transfer = {"/x.bin", "ux0:data/x.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &transfer, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    expect_cmd(&server, "RETR /x.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);

    CHECK(ftp_client_cancel(client) == FTP_CLIENT_OK);
    expect_cmd(&server, "ABOR");
    /* The other legal order: the terminal reply first, then the 426. */
    reply(&server, 226, "Abort successful.");
    reply(&server, 426, "Transfer aborted.");
    tick(client);
    fake_clock_advance(260);
    tick(client);
    CHECK(!ftp_client_busy(client));

    CHECK(ftp_client_cwd(client, "/AFTER") == FTP_CLIENT_OK);
    expect_cmd(&server, "CWD /AFTER");
    reply(&server, 250, "Directory changed.");
    tick(client);
    CHECK(strcmp(ftp_client_cwd_text(client), "/AFTER") == 0);

    /* A server that refuses ABOR must not cost the session. */
    FtpClientTransfer second = {"/y.bin", "ux0:data/y.bin", 0, 0, 0};
    CHECK(ftp_client_download(client, &second, NULL) == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,141).");
    tick(client);
    expect_cmd(&server, "RETR /y.bin");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    CHECK(ftp_client_cancel(client) == FTP_CLIENT_OK);
    expect_cmd(&server, "ABOR");
    reply(&server, 500, "ABOR not understood.");
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(!ftp_client_busy(client));

    ftp_client_destroy(client);
}

static void test_browse_mlsd_fallback(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    CHECK(ftp_client_list(client, "/DIR") == FTP_CLIENT_OK);
    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,140).");
    tick(client);
    expect_cmd(&server, "MLSD /DIR");
    reply(&server, 550, "Not found.");
    tick(client);

    expect_cmd(&server, "PASV");
    reply(&server, 227, "Entering Passive Mode (192,168,1,42,19,141).");
    tick(client);
    NetSocket *data = fake_net_last_connected();
    expect_cmd(&server, "LIST /DIR");
    reply(&server, 150, "Opening data connection.");
    tick(client);
    fake_net_queue_rx(data, "-rw-r--r-- 1 u g 3 Nov 08 12:00 A.TXT\r\n",
                      strlen("-rw-r--r-- 1 u g 3 Nov 08 12:00 A.TXT\r\n"));
    fake_net_shutdown(data);
    reply(&server, 226, "Transfer complete.");
    tick_until_settled(client);

    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(cap.rows == 1);
    CHECK(strcmp(cap.row[0].name, "A.TXT") == 0);
    CHECK(cap.done == 2 && cap.ok == 1 && cap.op == FTP_CLIENT_OP_LIST);

    ftp_client_destroy(client);
}

static void test_idle_control_drop(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    /* No operation is in flight; the server closes the control channel. */
    fake_net_shutdown(server.control);
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_LOST);

    ftp_client_destroy(client);
}

static void test_keepalive(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);
    cap.done = 0;

    fake_clock_advance(20001);
    tick(client);
    expect_cmd(&server, "NOOP");
    CHECK(cap.done == 0);
    CHECK(ftp_client_busy(client) == 1);

    reply(&server, 200, "Still here.");
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);
    CHECK(ftp_client_busy(client) == 0);
    CHECK(cap.done == 0);

    ftp_client_destroy(client);
}

static void test_keepalive_timeout(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    fake_clock_advance(20001);
    tick(client);
    expect_cmd(&server, "NOOP");
    CHECK(ftp_client_state(client) == FTP_CLIENT_READY);

    /* No reply: the keepalive deadline closes the session instead of leaving
     * a dead server marked CONNECTED forever. */
    fake_clock_advance(5001);
    tick(client);
    CHECK(ftp_client_state(client) == FTP_CLIENT_LOST);

    ftp_client_destroy(client);
}

static void test_disconnect(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);

    CHECK(ftp_client_disconnect(client) == FTP_CLIENT_OK);
    CHECK(ftp_client_state(client) == FTP_CLIENT_IDLE);
    CHECK(ftp_client_busy(client) == 0);
    CHECK(fake_net_is_open(server.control) == 0);
    CHECK(ftp_client_disconnect(client) == FTP_CLIENT_NOT_CONNECTED);

    ftp_client_destroy(client);
}

static void test_mlst(void)
{
    setup();
    Capture cap;
    memset(&cap, 0, sizeof(cap));
    FtpClient *client = make_client(&cap);
    FakeFtpd server;

    connect_ok(client, &server, &cap, NULL);
    capture_reset_rows(&cap);
    cap.done = 0;

    CHECK(ftp_client_mlst(client, "/X.BIN") == FTP_CLIENT_OK);
    expect_cmd(&server, "MLST /X.BIN");
    fake_ftpd_send_raw(&server,
                       "250-Listing /X.BIN\r\n"
                       " type=file;size=42;modify=20240101000000; /X.BIN\r\n"
                       "250 End\r\n");
    tick(client);
    CHECK(cap.done == 1 && cap.ok == 1 && cap.op == FTP_CLIENT_OP_STAT);
    CHECK(cap.exists == 1 && cap.is_dir == 0 && cap.unsupported == 0);
    CHECK(cap.file_size == 42 && cap.file_size_known == 1);

    cap.done = 0;
    CHECK(ftp_client_mlst(client, "/MISSING") == FTP_CLIENT_OK);
    expect_cmd(&server, "MLST /MISSING");
    reply(&server, 550, "Not found.");
    tick(client);
    CHECK(cap.done == 1 && cap.ok == 1 && cap.exists == 0);

    cap.done = 0;
    CHECK(ftp_client_mlst(client, "/ANY") == FTP_CLIENT_OK);
    expect_cmd(&server, "MLST /ANY");
    reply(&server, 502, "Not implemented.");
    tick(client);
    CHECK(cap.done == 1 && cap.ok == 1 && cap.unsupported == 1);

    ftp_client_destroy(client);
}

int main(void)
{
    test_anonymous_connect_real();
    test_login_site();
    test_greeting_120();
    test_login_530();
    test_feat_fallback();
    test_mlsd_fallback();
    test_browse_mlsd_fallback();
    test_mlsd_spaces();
    test_cwd();
    test_download();
    test_upload();
    test_download_reset();
    test_control_drop();
    test_resolve_timeout();
    test_connect_refused();
    test_connect_timeout();
    test_login_timeout();
    test_first_listing_timeout();
    test_data_idle();
    test_cancel_alignment();
    test_abort_reverse_order();
    test_pasv_nat();
    test_450_disarms();
    test_high_risk();
    test_simple_commands();
    test_nlst();
    test_mlst();
    test_keepalive();
    test_idle_control_drop();
    test_keepalive_timeout();
    test_disconnect();

    for (int s = FTP_CLIENT_IDLE; s <= FTP_CLIENT_FAILED; ++s)
        CHECK((s_states_seen & (1u << s)) != 0);
    for (int d = FTP_DATA_NONE; d <= FTP_DATA_DRAINING; ++d)
        CHECK((s_data_seen & (1u << d)) != 0);
    CHECK(fake_net_overflow_count() == 0);

    if (s_failures != 0) {
        fprintf(stderr, "test_client: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_client: OK\n");
    return 0;
}
