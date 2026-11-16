/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/xfer.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static XferItem item(const char *source, const char *destination)
{
    XferItem it;
    memset(&it, 0, sizeof(it));
    it.direction = XFER_DIR_UPLOAD;
    snprintf(it.source.text, sizeof(it.source.text), "%s", source);
    snprintf(it.destination.text, sizeof(it.destination.text), "%s",
             destination);
    it.total = 10;
    it.total_known = 1;
    return it;
}

static int push(XferQueue *queue, const char *source, const char *destination)
{
    XferItem it = item(source, destination);
    return xfer_enqueue(queue, &it);
}

static void test_order_and_slot(void)
{
    XferQueue queue;
    xfer_init(&queue);

    CHECK(!xfer_busy(&queue));
    CHECK(push(&queue, "a", "x") == 1);
    CHECK(push(&queue, "b", "x") == 1);
    CHECK(push(&queue, "c", "x") == 1);
    CHECK(queue.count == 3);
    CHECK(xfer_depth(&queue) == 3);

    XferItem active;
    CHECK(xfer_claim(&queue, &active) == 1);
    CHECK(strcmp(active.source.text, "a") == 0);
    CHECK(active.state == XFER_ITEM_ACTIVE);
    CHECK(xfer_busy(&queue));
    CHECK(xfer_depth(&queue) == 2);

    XferItem second;
    CHECK(xfer_claim(&queue, &second) == 0);
    CHECK(xfer_active(&queue, &second) == 1);
    CHECK(strcmp(second.source.text, "a") == 0);
    CHECK(queue.count == 3);

    XferItem next;
    CHECK(xfer_release(&queue, &next) == 1);
    CHECK(strcmp(next.source.text, "b") == 0);
    CHECK(next.state == XFER_ITEM_ACTIVE);
    CHECK(xfer_busy(&queue));
    CHECK(xfer_depth(&queue) == 1);

    CHECK(xfer_cancel_active(&queue, &next) ==
          1);
    CHECK(strcmp(next.source.text, "c") == 0);

    CHECK(xfer_release(&queue, &next) == 1);
    CHECK(!xfer_busy(&queue));
    CHECK(queue.count == 0);
    CHECK(xfer_depth(&queue) == 0);
}

static void test_clear_queued(void)
{
    XferQueue queue;
    xfer_init(&queue);
    CHECK(push(&queue, "a", "x") == 1);
    CHECK(push(&queue, "b", "x") == 1);
    CHECK(xfer_claim(&queue, NULL) == 1);
    xfer_clear_queued(&queue);
    CHECK(queue.count == 1);
    CHECK(xfer_busy(&queue));
    CHECK(xfer_release(&queue, NULL) == 1);
    CHECK(queue.count == 0);
}

static void test_full_queue(void)
{
    XferQueue queue;
    xfer_init(&queue);

    for (int i = 0; i < XFER_QUEUE_MAX; ++i)
        CHECK(push(&queue, "same", "x") == 1);
    CHECK(push(&queue, "overflow", "x") == 0);
    CHECK(queue.count == XFER_QUEUE_MAX);
    CHECK(queue.lost == 1);
}

int main(void)
{
    test_order_and_slot();
    test_clear_queued();
    test_full_queue();

    if (s_failures != 0) {
        fprintf(stderr, "test_xfer: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_xfer: OK\n");
    return 0;
}
