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

static void latch(XferQueue *queue, const XferItem *item, OpCause cause,
                  OpStage stage)
{
    /* A slow reader keeps the first unread outcome; a second one is counted,
     * not allowed to replace it. */
    if (queue->terminal_valid) {
        queue->lost++;
        return;
    }
    queue->terminal = *item;
    queue->terminal_cause = cause;
    queue->terminal_stage = stage;
    queue->terminal_valid = 1;
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

    XferItem finished = queue->items[0];
    finished.state = outcome;
    latch(queue, &finished, cause, stage);
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

    XferItem cancelled = queue->items[0];
    cancelled.state = XFER_ITEM_CANCELLED;
    latch(queue, &cancelled, cause, stage);
    drop_first(queue);

    if (next != NULL)
        memset(next, 0, sizeof(*next));
    if (queue->count > 0 && next != NULL)
        *next = queue->items[0];
    return 1;
}

int xfer_set_conflict(XferQueue *queue, XferConflict decision)
{
    if (queue == NULL || queue->conflict_decided)
        return 0;
    if (decision != XFER_CONFLICT_OVERWRITE_ALL &&
        decision != XFER_CONFLICT_SKIP_EXISTING &&
        decision != XFER_CONFLICT_CANCEL)
        return 0;
    queue->conflict = decision;
    queue->conflict_decided = 1;
    return 1;
}

XferConflict xfer_conflict(const XferQueue *queue)
{
    return queue != NULL ? queue->conflict : XFER_CONFLICT_NONE;
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

int xfer_take_terminal(XferQueue *queue, XferItem *out, OpCause *cause,
                       OpStage *stage)
{
    if (queue == NULL || !queue->terminal_valid)
        return 0;
    if (out != NULL)
        *out = queue->terminal;
    if (cause != NULL)
        *cause = queue->terminal_cause;
    if (stage != NULL)
        *stage = queue->terminal_stage;
    queue->terminal_valid = 0;
    return 1;
}
