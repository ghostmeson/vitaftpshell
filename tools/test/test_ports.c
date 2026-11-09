/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_clock.h"
#include "fake_fs.h"
#include "fake_ime.h"
#include "fake_log.h"
#include "fake_net.h"
#include "platform/clock.h"
#include "platform/fs.h"
#include "platform/ime.h"
#include "platform/log.h"
#include "platform/net.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_fs_listing(void)
{
    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ux0:data/b");
    fake_fs_add_dir("ux0:data/a");
    fake_fs_add_file("ux0:data/z", 12);

    FsPort *fs = fake_fs_port();
    FsDir *dir = NULL;
    CHECK(fs->list_open(fs->ctx, "ux0:data", &dir) == FS_OK);

    FsEntry entry;
    CHECK(fs->list_next(fs->ctx, dir, &entry) == FS_OK);
    CHECK(strcmp(entry.name, "b") == 0);
    CHECK(entry.type == FS_ENTRY_DIR);

    CHECK(fs->list_next(fs->ctx, dir, &entry) == FS_OK);
    CHECK(strcmp(entry.name, "a") == 0);

    CHECK(fs->list_next(fs->ctx, dir, &entry) == FS_OK);
    CHECK(strcmp(entry.name, "z") == 0);
    CHECK(entry.size == 12);
    CHECK(entry.size_known == 1);

    CHECK(fs->list_next(fs->ctx, dir, &entry) == FS_END);
    fs->list_close(fs->ctx, dir);

    CHECK(fs->stat(fs->ctx, "ux0:nope", &entry) == FS_NOT_FOUND);

    fake_fs_set_free("ux0:", 4321, 1);
    FsFreeSpace space;
    CHECK(fs->free_space(fs->ctx, "ux0:data", &space) == FS_OK);
    CHECK(space.known == 1);
    CHECK(space.bytes == 4321);

    fake_fs_set_free("ur0:", 0, 0);
    CHECK(fs->free_space(fs->ctx, "ur0:data", &space) == FS_OK);
    CHECK(space.known == 0);
}

static void test_fs_partial_listing(void)
{
    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_file("ux0:data/a", 1);
    fake_fs_add_file("ux0:data/b", 2);
    fake_fs_fail_list_after(1);

    FsPort *fs = fake_fs_port();
    FsDir *dir = NULL;
    CHECK(fs->list_open(fs->ctx, "ux0:data", &dir) == FS_OK);

    FsEntry entry;
    CHECK(fs->list_next(fs->ctx, dir, &entry) == FS_OK);
    CHECK(strcmp(entry.name, "a") == 0);
    CHECK(fs->list_next(fs->ctx, dir, &entry) == FS_IO);
    fs->list_close(fs->ctx, dir);
}

static void test_net_queue_and_wait(void)
{
    fake_net_reset();
    NetPort *net = fake_net_port();

    NetAddress peer = {0xC0A8012Au, 21};
    NetSocket *a = NULL;
    CHECK(net->connect(net->ctx, &peer, 100, &a) == NET_OK);

    uint8_t buf[16];
    size_t got = 0;
    CHECK(net->receive(net->ctx, a, buf, sizeof(buf), &got) == NET_WOULD_BLOCK);
    CHECK(got == 0);

    fake_net_queue_rx(a, "hello", 5);
    CHECK(net->receive(net->ctx, a, buf, sizeof(buf), &got) == NET_OK);
    CHECK(got == 5);
    CHECK(memcmp(buf, "hello", 5) == 0);

    NetSocket *b = NULL;
    CHECK(net->connect(net->ctx, &peer, 100, &b) == NET_OK);

    fake_net_set_ready(a, NET_WAIT_READ);
    fake_net_set_ready(b, 0);

    NetWaitSet set;
    set.count = 2;
    set.items[0].socket = a;
    set.items[0].flags = NET_WAIT_READ;
    set.items[1].socket = b;
    set.items[1].flags = NET_WAIT_READ;

    NetReadySet ready;
    CHECK(net->wait(net->ctx, &set, 10, &ready) == NET_OK);
    CHECK(ready.count == 1);
    CHECK(ready.items[0].socket == a);

    fake_net_set_ready(a, 0);
    CHECK(net->wait(net->ctx, &set, 10, &ready) == NET_OK);
    CHECK(ready.count == 0);

    NetLink link;
    fake_net_set_link(1, 0, 0, 0);
    CHECK(net->link(net->ctx, &link) == NET_OK);
    CHECK(link.up == 1);
    CHECK(link.address_known == 0);
}

static void test_net_lifecycle(void)
{
    fake_net_reset();
    NetPort *net = fake_net_port();
    NetAddress peer = {0xC0A8012Au, 21};

    /* A closed socket returns its slot. */
    for (int i = 0; i < FAKE_NET_SOCKETS; ++i) {
        NetSocket *s = NULL;
        CHECK(net->connect(net->ctx, &peer, 100, &s) == NET_OK);
        net->close(net->ctx, s);
    }
    NetSocket *s = NULL;
    CHECK(net->connect(net->ctx, &peer, 100, &s) == NET_OK);

    /* Queued bytes make the socket readable, and EOF is distinguishable. */
    fake_net_queue_rx(s, "x", 1);
    NetWaitSet set;
    set.count = 1;
    set.items[0].socket = s;
    set.items[0].flags = NET_WAIT_READ;
    NetReadySet ready;
    CHECK(net->wait(net->ctx, &set, 1, &ready) == NET_OK);
    CHECK(ready.count == 1);

    uint8_t byte = 0;
    size_t got = 0;
    CHECK(net->receive(net->ctx, s, &byte, 1, &got) == NET_OK);
    fake_net_shutdown(s);
    CHECK(net->wait(net->ctx, &set, 1, &ready) == NET_OK);
    CHECK(ready.count == 1);
    CHECK(net->receive(net->ctx, s, &byte, 1, &got) == NET_CLOSED);

    /* A listener is readable only when a peer is queued. */
    NetAddress bind = {0, 1337};
    NetSocket *listener = NULL;
    CHECK(net->listen(net->ctx, &bind, &listener) == NET_OK);
    set.count = 1;
    set.items[0].socket = listener;
    set.items[0].flags = NET_WAIT_READ;
    CHECK(net->wait(net->ctx, &set, 1, &ready) == NET_OK);
    CHECK(ready.count == 0);

    fake_net_queue_accept(listener, 0xC0A8012Au, 40000);
    CHECK(net->wait(net->ctx, &set, 1, &ready) == NET_OK);
    CHECK(ready.count == 1);

    NetSocket *accepted = NULL;
    NetAddress from;
    CHECK(net->accept(net->ctx, listener, &accepted, &from) == NET_OK);
    CHECK(from.port == 40000);
    CHECK(net->wait(net->ctx, &set, 1, &ready) == NET_OK);
    CHECK(ready.count == 0);
}

static void test_rename_capability(void)
{
    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_file("ux0:data/a", 1);
    fake_fs_add_file("ux0:data/b", 2);

    FsPort *fs = fake_fs_port();
    fake_fs_set_rename_replaces(0);
    CHECK(fs->rename_replaces(fs->ctx) == 0);
    CHECK(fs->rename(fs->ctx, "ux0:data/a", "ux0:data/b") == FS_EXISTS);

    fake_fs_set_rename_replaces(1);
    CHECK(fs->rename_replaces(fs->ctx) == 1);
    CHECK(fs->rename(fs->ctx, "ux0:data/a", "ux0:data/b") == FS_OK);

    CHECK(fs->rename(fs->ctx, "ux0:data/b", "ux0:data/b") == FS_OK);
    CHECK(fake_fs_exists("ux0:data/b"));
}

static void test_clock(void)
{
    fake_clock_reset();
    ClockPort *clock = fake_clock_port();
    CHECK(clock->now_ms(clock->ctx) == 0);

    fake_clock_advance(50);
    clock->sleep_ms(clock->ctx, 25);
    CHECK(clock->now_ms(clock->ctx) == 75);

    fake_clock_set_now(10);
    CHECK(clock->now_ms(clock->ctx) == 75);
}

static void test_log(void)
{
    fake_log_reset();
    fake_log_set_capacity(19);
    LogPort *log = fake_log_port();

    CHECK(log->append(log->ctx, "123456789", 9) == LOG_OK);
    CHECK(fake_log_count() == 1);
    CHECK(strcmp(fake_log_line(0), "123456789") == 0);

    char too_long[FAKE_LOG_LINE_MAX + 1];
    memset(too_long, 'x', sizeof(too_long));
    CHECK(log->append(log->ctx, too_long, sizeof(too_long)) == LOG_DROPPED);

    CHECK(log->append(log->ctx, "abcdefghi", 9) == LOG_ROTATE);
    CHECK(log->rotate(log->ctx) == LOG_OK);
    CHECK(fake_log_rotations() == 1);
    CHECK(fake_log_count() == 0);

    CHECK(log->append(log->ctx, "after", 5) == LOG_OK);

    fake_log_reset();
    fake_log_set_capacity(8);
    CHECK(log->append(log->ctx, "01234567890123456789", 20) == LOG_DROPPED);
}

static void test_ime(void)
{
    fake_ime_reset();
    TextInputPort *ime = fake_ime_port();
    ImeRequest request = {"TITLE", "initial", 0, 32};
    ImeResponse response;

    fake_ime_queue_text("hello");
    fake_ime_queue_cancel();
    fake_ime_queue_unavailable();

    CHECK(ime->begin(ime->ctx, &request) == IME_BEGIN_RUNNING);
    CHECK(ime->poll(ime->ctx, &response) == IME_POLL_DONE);
    CHECK(strcmp(response.text, "hello") == 0);
    ime->end(ime->ctx);

    CHECK(ime->begin(ime->ctx, &request) == IME_BEGIN_RUNNING);
    CHECK(ime->poll(ime->ctx, &response) == IME_POLL_CANCELLED);
    ime->end(ime->ctx);

    CHECK(ime->begin(ime->ctx, &request) == IME_BEGIN_UNAVAILABLE);
    CHECK(ime->begin(ime->ctx, &request) == IME_BEGIN_ERROR);
}

int main(void)
{
    test_fs_listing();
    test_fs_partial_listing();
    test_net_queue_and_wait();
    test_net_lifecycle();
    test_rename_capability();
    test_clock();
    test_log();
    test_ime();

    if (s_failures != 0) {
        fprintf(stderr, "test_ports: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_ports: OK\n");
    return 0;
}
