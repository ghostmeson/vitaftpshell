/* SPDX-License-Identifier: GPL-3.0-only */

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#include "core/handoff.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static Handoff s_handoff;

static UiCommand command(UiCommandType type, int index)
{
    UiCommand c;
    memset(&c, 0, sizeof(c));
    c.type = type;
    c.index = index;
    snprintf(c.path, sizeof(c.path), "ux0:data/%d", index);
    snprintf(c.text, sizeof(c.text), "item %d", index);
    return c;
}

static void test_command_critical(void)
{
    handoff_init(&s_handoff);

    for (int i = 0; i < HANDOFF_CMD_CAP; ++i) {
        UiCommand c = command(UI_CMD_OPEN_PATH, i);
        CHECK(handoff_command_push(&s_handoff, &c) == 1);
    }
    UiCommand overflow = command(UI_CMD_OPEN_PATH, 99);
    CHECK(handoff_command_push(&s_handoff, &overflow) == 0);
    CHECK(handoff_command_lost(&s_handoff) == 1);

    UiCommand stop = command(UI_CMD_STOP_SERVER, 0);
    UiCommand cancel = command(UI_CMD_CANCEL_TRANSFER, 0);
    CHECK(handoff_command_push(&s_handoff, &stop) == 1);
    CHECK(handoff_command_push(&s_handoff, &cancel) == 1);

    UiCommand out;
    CHECK(handoff_command_pop(&s_handoff, &out) == 1);
    CHECK(out.type == UI_CMD_STOP_SERVER);
    CHECK(handoff_command_pop(&s_handoff, &out) == 1);
    CHECK(out.type == UI_CMD_CANCEL_TRANSFER);

    for (int i = 0; i < HANDOFF_CMD_CAP; ++i) {
        CHECK(handoff_command_pop(&s_handoff, &out) == 1);
        CHECK(out.type == UI_CMD_OPEN_PATH);
        CHECK(out.index == i);
    }
    CHECK(handoff_command_pop(&s_handoff, &out) == 0);
}

static void test_event_progress(void)
{
    handoff_init(&s_handoff);

    HandoffEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = HANDOFF_EVT_PROGRESS;
    for (int i = 0; i < 100; ++i) {
        event.bytes = (uint64_t)i;
        event.total = 100;
        event.total_known = 1;
        CHECK(handoff_event_push(&s_handoff, &event) == 1);
    }

    HandoffEvent out;
    CHECK(handoff_event_pop(&s_handoff, &out) == 1);
    CHECK(out.kind == HANDOFF_EVT_PROGRESS);
    CHECK(out.bytes == 99);
    CHECK(handoff_event_pop(&s_handoff, &out) == 0);

    memset(&event, 0, sizeof(event));
    event.kind = HANDOFF_EVT_PROGRESS;
    event.bytes = 10;
    CHECK(handoff_event_push(&s_handoff, &event) == 1);
    memset(&event, 0, sizeof(event));
    event.kind = HANDOFF_EVT_TRANSFER_DONE;
    event.ok = 1;
    snprintf(event.detail, sizeof(event.detail), "DONE");
    CHECK(handoff_event_push(&s_handoff, &event) == 1);
    CHECK(handoff_event_pop(&s_handoff, &out) == 1);
    CHECK(out.kind == HANDOFF_EVT_TRANSFER_DONE);
    CHECK(strcmp(out.detail, "DONE") == 0);
    /* The older progress sample was dropped with the terminal record. */
    CHECK(handoff_event_pop(&s_handoff, &out) == 0);
}

static void test_latched_server(void)
{
    handoff_init(&s_handoff);
    handoff_latch_server(&s_handoff, UI_SERVER_ON, 1);

    HandoffEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = HANDOFF_EVT_STATE;
    for (int i = 0; i < 10; ++i) {
        event.bytes = (uint64_t)i;
        CHECK(handoff_event_push(&s_handoff, &event) == 1);
    }

    HandoffEvent out;
    while (handoff_event_pop(&s_handoff, &out))
        ;

    UiServerRun run = UI_SERVER_OFF;
    int clients = 0;
    CHECK(handoff_server_state(&s_handoff, &run, &clients) == 1);
    CHECK(run == UI_SERVER_ON);
    CHECK(clients == 1);
    CHECK(handoff_server_state(&s_handoff, &run, &clients) == 1);
}

static void test_listing_generations(void)
{
    handoff_init(&s_handoff);

    static int list_a[] = {1, 2, 3};
    static int list_b[] = {4, 5, 6};
    static int list_c[] = {7, 8, 9};
    static int list_d[] = {10, 11, 12};

    int slot = handoff_listing_begin(&s_handoff, UI_PANE_LOCAL);
    CHECK(slot == 0);
    CHECK(handoff_listing_publish(&s_handoff, UI_PANE_LOCAL, slot, list_a, 3) ==
          1);

    const void *rows = NULL;
    int count = 0;
    uint32_t generation = 0;
    CHECK(handoff_listing_acquire(&s_handoff, UI_PANE_LOCAL, &rows, &count,
                                  &generation) == 1);
    CHECK(rows == list_a);
    CHECK(count == 3);
    CHECK(generation == 1);

    slot = handoff_listing_begin(&s_handoff, UI_PANE_LOCAL);
    CHECK(slot == 1);
    CHECK(handoff_listing_publish(&s_handoff, UI_PANE_LOCAL, slot, list_b, 3) ==
          1);
    slot = handoff_listing_begin(&s_handoff, UI_PANE_LOCAL);
    CHECK(slot == 2);
    CHECK(handoff_listing_publish(&s_handoff, UI_PANE_LOCAL, slot, list_c, 3) ==
          1);

    /* Slot 0 is held. The next publish must reuse one of the others. */
    slot = handoff_listing_begin(&s_handoff, UI_PANE_LOCAL);
    CHECK(slot == 1);
    CHECK(handoff_listing_publish(&s_handoff, UI_PANE_LOCAL, slot, list_d, 3) ==
          1);
    CHECK(s_handoff.listings[UI_PANE_LOCAL].slots[0].rows == list_a);
    CHECK(((const int *)s_handoff.listings[UI_PANE_LOCAL].slots[0].rows)[0] ==
          1);

    /* While the reader holds generation 1, acquire returns it unchanged. */
    const void *held_rows = NULL;
    uint32_t held_generation = 0;
    CHECK(handoff_listing_acquire(&s_handoff, UI_PANE_LOCAL, &held_rows, NULL,
                                  &held_generation) == 1);
    CHECK(held_rows == list_a);
    CHECK(held_generation == 1);

    handoff_listing_release(&s_handoff, UI_PANE_LOCAL);

    /* The newest visible generation is the overwritten slot. */
    CHECK(handoff_listing_acquire(&s_handoff, UI_PANE_LOCAL, &rows, &count,
                                  &generation) == 1);
    CHECK(rows == list_d);
    CHECK(count == 3);
    CHECK(generation == 4);
    handoff_listing_release(&s_handoff, UI_PANE_LOCAL);
}

static void test_log_ring(void)
{
    handoff_init(&s_handoff);

    for (int i = 0; i < HANDOFF_LOG_CAP; ++i) {
        HandoffLogRecord record;
        memset(&record, 0, sizeof(record));
        record.kind = HANDOFF_LOG_KEY;
        snprintf(record.screen, sizeof(record.screen), "WORKSPACE");
        snprintf(record.key, sizeof(record.key), "K%d", i);
        record.value = i;
        record.value_set = 1;
        CHECK(handoff_log_push(&s_handoff, &record) == 1);
    }

    HandoffLogRecord overflow;
    memset(&overflow, 0, sizeof(overflow));
    overflow.kind = HANDOFF_LOG_KEY;
    snprintf(overflow.key, sizeof(overflow.key), "OVERFLOW");
    CHECK(handoff_log_push(&s_handoff, &overflow) == 0);
    CHECK(handoff_log_lost(&s_handoff) == 1);

    HandoffLogRecord out;
    for (int i = 0; i < HANDOFF_LOG_CAP; ++i) {
        CHECK(handoff_log_pop(&s_handoff, &out) == 1);
        char expected[16];
        snprintf(expected, sizeof(expected), "K%d", i);
        CHECK(strcmp(out.key, expected) == 0);
        CHECK(out.value == i);
    }
    CHECK(handoff_log_pop(&s_handoff, &out) == 0);
}

static void test_bulk(void)
{
    handoff_init(&s_handoff);

    static unsigned char book[4096];
    static unsigned char copy[4096];
    for (int i = 0; i < (int)sizeof(book); ++i)
        book[i] = (unsigned char)(i * 7 + 3);

    CHECK(handoff_bulk_write(&s_handoff, book, sizeof(book), 7) == 1);
    size_t len = 0;
    uint32_t generation = 0;
    CHECK(handoff_bulk_read(&s_handoff, copy, sizeof(copy), &len, &generation) ==
          1);
    CHECK(len == sizeof(book));
    CHECK(generation == 7);
    CHECK(memcmp(book, copy, sizeof(book)) == 0);
    CHECK(handoff_bulk_read(&s_handoff, copy, sizeof(copy), &len, &generation) ==
          0);
}

static void test_record_integrity(void)
{
    handoff_init(&s_handoff);

    UiCommand in;
    memset(&in, 0, sizeof(in));
    in.type = UI_CMD_TRANSFER;
    in.pane = UI_PANE_REMOTE;
    in.action = UI_ACTION_DOWNLOAD;
    in.index = 7;
    in.value = 42;
    snprintf(in.path, sizeof(in.path), "/remote/file.bin");
    snprintf(in.path2, sizeof(in.path2), "ux0:data");
    snprintf(in.text, sizeof(in.text), "file.bin");

    CHECK(handoff_command_push(&s_handoff, &in) == 1);
    UiCommand out;
    CHECK(handoff_command_pop(&s_handoff, &out) == 1);
    CHECK(out.type == in.type);
    CHECK(out.pane == in.pane);
    CHECK(out.action == in.action);
    CHECK(out.index == in.index);
    CHECK(out.value == in.value);
    CHECK(strcmp(out.path, in.path) == 0);
    CHECK(strcmp(out.path2, in.path2) == 0);
    CHECK(strcmp(out.text, in.text) == 0);

    HandoffEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = HANDOFF_EVT_ERROR;
    event.cause = OP_CAUSE_NETWORK;
    event.stage = OP_STAGE_DATA;
    snprintf(event.path, sizeof(event.path), "/remote/file.bin");
    snprintf(event.detail, sizeof(event.detail), "DATA LOST");
    CHECK(handoff_event_push(&s_handoff, &event) == 1);
    HandoffEvent got;
    CHECK(handoff_event_pop(&s_handoff, &got) == 1);
    CHECK(got.kind == event.kind);
    CHECK(got.cause == event.cause);
    CHECK(got.stage == event.stage);
    CHECK(strcmp(got.path, event.path) == 0);
    CHECK(strcmp(got.detail, event.detail) == 0);
}

#define STRESS_COUNT 2000

typedef struct {
    Handoff *handoff;
} StressCtx;

static void *event_producer(void *arg)
{
    Handoff *handoff = ((StressCtx *)arg)->handoff;
    for (int i = 0; i < STRESS_COUNT; ++i) {
        HandoffEvent event;
        memset(&event, 0, sizeof(event));
        event.kind = HANDOFF_EVT_STATE;
        event.bytes = (uint64_t)i;
        while (!handoff_event_push(handoff, &event))
            sched_yield();
    }
    HandoffEvent done;
    memset(&done, 0, sizeof(done));
    done.kind = HANDOFF_EVT_TRANSFER_DONE;
    while (!handoff_event_push(handoff, &done))
        sched_yield();
    return NULL;
}

static void *event_consumer(void *arg)
{
    Handoff *handoff = ((StressCtx *)arg)->handoff;
    int expected = 0;
    int finished = 0;
    while (!finished) {
        HandoffEvent event;
        if (!handoff_event_pop(handoff, &event)) {
            sched_yield();
            continue;
        }
        if (event.kind == HANDOFF_EVT_TRANSFER_DONE) {
            finished = 1;
        } else if (event.kind == HANDOFF_EVT_STATE) {
            CHECK(event.bytes == (uint64_t)expected);
            expected++;
        }
    }
    CHECK(expected == STRESS_COUNT);
    return NULL;
}

typedef struct {
    Handoff *handoff;
    int seen;
} ProgressStress;

static void *progress_producer(void *arg)
{
    ProgressStress *ctx = arg;
    for (int i = 0; i < STRESS_COUNT; ++i) {
        HandoffEvent event;
        memset(&event, 0, sizeof(event));
        event.kind = HANDOFF_EVT_PROGRESS;
        event.bytes = (uint64_t)i;
        event.total = (uint64_t)i + 1;
        snprintf(event.path, sizeof(event.path), "P");
        CHECK(handoff_event_push(ctx->handoff, &event) == 1);
        if (i % 100 == 0) {
            while (__atomic_load_n(&ctx->seen, __ATOMIC_ACQUIRE) == 0)
                sched_yield();
        }
    }
    HandoffEvent done;
    memset(&done, 0, sizeof(done));
    done.kind = HANDOFF_EVT_TRANSFER_DONE;
    CHECK(handoff_event_push(ctx->handoff, &done) == 1);
    return NULL;
}

static void *progress_consumer(void *arg)
{
    ProgressStress *ctx = arg;
    for (;;) {
        HandoffEvent event;
        if (!handoff_event_pop(ctx->handoff, &event)) {
            sched_yield();
            continue;
        }
        if (event.kind == HANDOFF_EVT_TRANSFER_DONE)
            break;
        if (event.kind == HANDOFF_EVT_PROGRESS) {
            CHECK(event.total == event.bytes + 1);
            CHECK(strcmp(event.path, "P") == 0);
            __atomic_add_fetch(&ctx->seen, 1, __ATOMIC_RELEASE);
        }
    }
    return NULL;
}

static void test_progress_stress(void)
{
    handoff_init(&s_handoff);
    ProgressStress ctx = {&s_handoff, 0};
    pthread_t producer;
    pthread_t consumer;
    CHECK(pthread_create(&producer, NULL, progress_producer, &ctx) == 0);
    CHECK(pthread_create(&consumer, NULL, progress_consumer, &ctx) == 0);
    CHECK(pthread_join(producer, NULL) == 0);
    CHECK(pthread_join(consumer, NULL) == 0);
    CHECK(__atomic_load_n(&ctx.seen, __ATOMIC_ACQUIRE) > 0);
}

static void test_event_stress(void)
{
    handoff_init(&s_handoff);
    StressCtx ctx = {&s_handoff};
    pthread_t producer;
    pthread_t consumer;
    CHECK(pthread_create(&producer, NULL, event_producer, &ctx) == 0);
    CHECK(pthread_create(&consumer, NULL, event_consumer, &ctx) == 0);
    CHECK(pthread_join(producer, NULL) == 0);
    CHECK(pthread_join(consumer, NULL) == 0);
}

static void *command_producer(void *arg)
{
    Handoff *handoff = ((StressCtx *)arg)->handoff;
    for (int i = 0; i < STRESS_COUNT; ++i) {
        UiCommand c = command(UI_CMD_OPEN_PATH, i);
        while (!handoff_command_push(handoff, &c))
            sched_yield();
    }
    UiCommand stop = command(UI_CMD_STOP_SERVER, 0);
    UiCommand cancel = command(UI_CMD_CANCEL_TRANSFER, 0);
    while (!handoff_command_push(handoff, &stop))
        sched_yield();
    while (!handoff_command_push(handoff, &cancel))
        sched_yield();
    return NULL;
}

static void *command_consumer(void *arg)
{
    Handoff *handoff = ((StressCtx *)arg)->handoff;
    int normal = 0;
    int stop = 0;
    int cancel = 0;
    while (normal < STRESS_COUNT || !stop || !cancel) {
        UiCommand c;
        if (!handoff_command_pop(handoff, &c)) {
            sched_yield();
            continue;
        }
        if (c.type == UI_CMD_STOP_SERVER)
            stop = 1;
        else if (c.type == UI_CMD_CANCEL_TRANSFER)
            cancel = 1;
        else {
            CHECK(c.type == UI_CMD_OPEN_PATH);
            CHECK(c.index == normal);
            normal++;
        }
    }
    CHECK(normal == STRESS_COUNT);
    CHECK(stop == 1);
    CHECK(cancel == 1);
    return NULL;
}

static void test_command_stress(void)
{
    handoff_init(&s_handoff);
    StressCtx ctx = {&s_handoff};
    pthread_t producer;
    pthread_t consumer;
    CHECK(pthread_create(&producer, NULL, command_producer, &ctx) == 0);
    CHECK(pthread_create(&consumer, NULL, command_consumer, &ctx) == 0);
    CHECK(pthread_join(producer, NULL) == 0);
    CHECK(pthread_join(consumer, NULL) == 0);
}

int main(void)
{
    test_command_critical();
    test_event_progress();
    test_latched_server();
    test_listing_generations();
    test_log_ring();
    test_bulk();
    test_record_integrity();
    test_progress_stress();
    test_event_stress();
    test_command_stress();

    if (s_failures != 0) {
        fprintf(stderr, "test_handoff: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_handoff: OK\n");
    return 0;
}
