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
    ftp_server_tick(server);

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
    ftp_server_tick(server);

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
    ftp_server_tick(server);

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

static void pasv(FtpServer *server, FakeFtpClient *client)
{
    command(server, client, "PASV", 227);
}

static void queue_data_peer(void)
{
    fake_net_queue_accept(fake_net_listener_at(1), PEER_ADDR, PEER_PORT + 1);
}

static NetSocket *data_command(FtpServer *server, FakeFtpClient *client,
                               const char *cmd, int expect)
{
    unsigned before = fake_net_accept_count();
    fake_ftp_client_send(client, cmd);
    ftp_server_tick(server);

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
        ftp_server_tick(server);
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
    ftp_server_tick(server);
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

static void test_denials(void)
{
    setup();
    fake_fs_add_dir("ux0:tai");
    fake_fs_seed_file("ur0:tai/config.txt", "config", 6);
    fake_fs_seed_file("ux0:tai/config.txt", "config", 6);
    fake_fs_seed_file("ux0:data/a.txt", "a", 1);
    size_t before = fake_fs_node_count();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR ur0:tai/x.suprx", 550);
    CHECK(!fake_fs_exists("ur0:tai/x.suprx"));
    CHECK(!fake_fs_exists("ur0:tai/x.suprx.vftp.part"));
    CHECK(!ftp_server_busy(server));

    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR ux0:tai/boot_config.txt", 550);
    CHECK(!fake_fs_exists("ux0:tai/boot_config.txt"));

    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR UR0:/TAI/./BOOT_CONFIG.TXT", 550);
    CHECK(!fake_fs_exists("ur0:tai/boot_config.txt"));

    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR //UR0://TAI//BOOT_CONFIG.TXT",
                       550);
    CHECK(!fake_fs_exists("ur0:tai/boot_config.txt"));

    pasv(server, &client);
    queue_data_peer();
    (void)data_command(server, &client, "STOR ux0:data/vitaFTPShell/x", 550);
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/x"));

    command(server, &client, "DELE ux0:tai/config.txt", 550);
    CHECK(fake_fs_exists("ux0:tai/config.txt"));
    CHECK(fake_fs_exists("ur0:tai/config.txt"));
    CHECK(fake_fs_exists("ux0:data/a.txt"));

    command(server, &client, "RNFR /UR0:/TAI/CONFIG.TXT", 550);
    CHECK(fake_fs_exists("ur0:tai/config.txt"));

    command(server, &client, "RNFR /UX0:/DATA/a.txt", 350);
    command(server, &client, "RNTO ux0:tai/a.txt", 550);
    CHECK(fake_fs_exists("ux0:data/a.txt"));
    CHECK(!fake_fs_exists("ux0:tai/a.txt"));

    command(server, &client, "DELE /UX0:", 550);
    CHECK(fake_fs_exists("ux0:data/a.txt"));
    command(server, &client, "RNFR /UX0:", 550);
    command(server, &client, "RMD /UR0:", 550);
    command(server, &client, "RMD /UMA0:", 550);
    CHECK(fake_fs_exists("uma0:"));
    CHECK(fake_fs_exists("ur0:tai/config.txt"));
    command(server, &client, "RNFR /UX0:/DATA", 550);

    CHECK(fake_fs_node_count() == before);

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
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    command(server, &client, "MLST /UX0:/DATA/f.txt", 250);
    command(server, &client, "MLST /", 250);
    command(server, &client, "MLST /UX0:/DATA", 250);
    command(server, &client, "MLST ux0:data/vitaFTPShell", 550);
    command(server, &client, "SIZE /UX0:/DATA/f.txt", 213);
    command(server, &client, "MDTM /UX0:/DATA/f.txt", 550);

    fake_ftp_client_send(&client, "MLST /UX0:/DATA/f.txt");
    ftp_server_tick(server);
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

    command(server, &client, "MKD ur0:tai/NEW", 550);
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

static void test_second_client(void)
{
    setup();
    SCapture cap;
    memset(&cap, 0, sizeof(cap));
    FtpServer *server = start_server(&cap);
    FakeFtpClient client;
    connect_session(server, &client);

    fake_net_queue_accept(fake_net_listener_at(0), 0x0A000009u, 41000);
    ftp_server_tick(server);
    NetSocket *second = fake_net_accepted_at(1);
    CHECK(second != NULL);
    char tx[64];
    size_t n = fake_net_read_tx(second, tx, sizeof(tx) - 1);
    tx[n] = '\0';
    CHECK(strstr(tx, "450") != NULL);
    CHECK(!fake_net_is_open(second));
    CHECK(ftp_server_clients(server) == 1);
    command(server, &client, "NOOP", 200);

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
    NetSocket *data = data_command(server, &client, "STOR ux0:data/busy.bin", 150);
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
    CHECK(!fake_fs_exists("ux0:data/busy.bin"));
    CHECK(!fake_fs_exists("ux0:data/busy.bin.vftp.part"));

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
    ftp_server_tick(server);

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
    fake_net_queue_accept(fake_net_listener_at(1), 0x0A000009u, 40001);
    (void)data_command(server, &client, "RETR /UX0:/DATA/f.txt", 425);
    NetSocket *foreign = fake_net_accepted_at(1);
    CHECK(foreign != NULL);
    CHECK(!fake_net_is_open(foreign));
    CHECK(fake_net_tx_len(foreign) == 0);

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
    ftp_server_tick(server);
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
    ftp_server_tick(server);

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
    test_denials();
    test_cwd();
    test_file_ops();
    test_mlst_mtime();
    test_root_absent_volume();
    test_mlst_size_mdtm();
    test_second_client();
    test_busy_and_abort();
    test_control_write_failure();
    test_peer_check();
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
