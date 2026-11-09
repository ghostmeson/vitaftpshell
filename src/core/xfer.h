/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_XFER_H
#define VFTP_CORE_XFER_H

#include <stdint.h>

#include "core/cause.h"
#include "core/path.h"

/*
 * One transfer slot, one ordered batch (R38-R41, R43). The queue is policy
 * only: it holds descriptors, never files or sockets. The client, the server,
 * and the local executor all ask this one busy fact before claiming the slot,
 * so there is never a second writer.
 */

#define XFER_QUEUE_MAX 64

typedef enum {
    XFER_DIR_DOWNLOAD = 0,
    XFER_DIR_UPLOAD,
    XFER_DIR_LOCAL_COPY,
    XFER_DIR_LOCAL_MOVE,
    XFER_DIR_SERVER_DOWNLOAD,
    XFER_DIR_SERVER_UPLOAD
} XferDirection;

typedef enum {
    XFER_CONFLICT_NONE = 0,
    XFER_CONFLICT_OVERWRITE_ALL,
    XFER_CONFLICT_SKIP_EXISTING,
    XFER_CONFLICT_CANCEL
} XferConflict;

typedef enum {
    XFER_ITEM_QUEUED = 0,
    XFER_ITEM_ACTIVE,
    XFER_ITEM_DONE,
    XFER_ITEM_FAILED,
    XFER_ITEM_CANCELLED
} XferItemState;

typedef struct {
    XferDirection direction;
    Path source;
    Path destination;
    uint64_t total;
    int total_known;
    XferItemState state;
} XferItem;

typedef struct {
    XferItem items[XFER_QUEUE_MAX];
    int count;
    int active; /* 1 when items[0] owns the slot */
    XferConflict conflict;
    int conflict_decided;
    int terminal_valid;
    XferItem terminal;
    OpCause terminal_cause;
    OpStage terminal_stage;
    unsigned lost;
} XferQueue;

void xfer_init(XferQueue *queue);

/* Append in selection order. Returns 0 when the queue is full. */
int xfer_enqueue(XferQueue *queue, const XferItem *item);

/* One busy fact for both roles. */
int xfer_busy(const XferQueue *queue);
int xfer_depth(const XferQueue *queue);
int xfer_active(const XferQueue *queue, XferItem *out);

/* Claim the first queued item. Returns 0 when busy or empty. */
int xfer_claim(XferQueue *queue, XferItem *out);

/*
 * Finish the active item and latch its terminal outcome. The next queued item
 * starts immediately and is returned through `next` when present.
 */
int xfer_release(XferQueue *queue, XferItemState outcome, OpCause cause,
                 OpStage stage, XferItem *next);

/* Cancel the active item. Queued items keep their order. */
int xfer_cancel_active(XferQueue *queue, OpCause cause, OpStage stage,
                       XferItem *next);

/* R41: one decision per batch, recorded once and never re-asked. */
int xfer_set_conflict(XferQueue *queue, XferConflict decision);
XferConflict xfer_conflict(const XferQueue *queue);

/* A slow reader sees a terminal outcome exactly once. */
int xfer_take_terminal(XferQueue *queue, XferItem *out, OpCause *cause,
                       OpStage *stage);

#endif
