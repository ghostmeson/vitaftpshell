/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/xfer.h"

#include <string.h>

static void drop_first(XferQueue *queue)
{
    if (queue->count <= 1) {
        queue->count = 0;
        queue->active = 0;
        return;
    }
    memmove(&queue->items[0], &queue->items[1],
            sizeof(queue->items[0]) * (size_t)(queue->count - 1));
    queue->count--;
    queue->active = 1;
    queue->items[0].state = XFER_ITEM_ACTIVE;
}

void xfer_init(XferQueue *queue)
{
    if (queue == NULL)
        return;
    memset(queue, 0, sizeof(*queue));
    queue->active = 0;
}

int xfer_enqueue(XferQueue *queue, const XferItem *item)
{
    if (queue == NULL || item == NULL || queue->count >= XFER_QUEUE_MAX) {
        if (queue != NULL)
            queue->lost++;
        return 0;
    }
    queue->items[queue->count] = *item;
    queue->items[queue->count].state = XFER_ITEM_QUEUED;
    queue->count++;
    return 1;
}

int xfer_busy(const XferQueue *queue)
{
    return queue != NULL && queue->active != 0;
}

int xfer_depth(const XferQueue *queue)
{
    if (queue == NULL || queue->count == 0)
        return 0;
    return queue->active ? queue->count - 1 : queue->count;
}

int xfer_active(const XferQueue *queue, XferItem *out)
{
    if (queue == NULL || !queue->active || queue->count == 0)
        return 0;
    if (out != NULL)
        *out = queue->items[0];
    return 1;
}

int xfer_claim(XferQueue *queue, XferItem *out)
{
    if (queue == NULL || queue->active || queue->count == 0)
        return 0;
    queue->items[0].state = XFER_ITEM_ACTIVE;
    queue->active = 1;
    if (out != NULL)
        *out = queue->items[0];
    return 1;
}

int xfer_release(XferQueue *queue, XferItemState outcome, OpCause cause,
                 OpStage stage, XferItem *next)
{
    if (queue == NULL || !queue->active || queue->count == 0)
        return 0;
    if (outcome == XFER_ITEM_QUEUED || outcome == XFER_ITEM_ACTIVE)
        outcome = XFER_ITEM_DONE;

    (void)cause;
    (void)stage;
    drop_first(queue);

    if (next != NULL)
        memset(next, 0, sizeof(*next));
    if (queue->count > 0 && next != NULL)
        *next = queue->items[0];
    return 1;
}

int xfer_cancel_active(XferQueue *queue, OpCause cause, OpStage stage,
                       XferItem *next)
{
    if (queue == NULL || !queue->active || queue->count == 0)
        return 0;

    (void)cause;
    (void)stage;
    drop_first(queue);

    if (next != NULL)
        memset(next, 0, sizeof(*next));
    if (queue->count > 0 && next != NULL)
        *next = queue->items[0];
    return 1;
}

void xfer_clear_queued(XferQueue *queue)
{
    if (queue == NULL)
        return;
    if (queue->active && queue->count > 0)
        queue->count = 1;
    else {
        queue->count = 0;
        queue->active = 0;
    }
}
