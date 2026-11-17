/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_XFER_H
#define VFTP_CORE_XFER_H

#include <stdint.h>

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
    XFER_DIR_LOCAL_DELETE,
    XFER_DIR_LOCAL_RENAME,
    XFER_DIR_LOCAL_MKDIR,
    XFER_DIR_SERVER_DOWNLOAD,
    XFER_DIR_SERVER_UPLOAD
} XferDirection;

typedef enum {
    XFER_CONFLICT_NONE = 0,
    XFER_CONFLICT_OVERWRITE_ALL,
    XFER_CONFLICT_SKIP_EXISTING,
    XFER_CONFLICT_CANCEL
} XferConflict;

/*
 * KTD5: a folder job's progress and terminal events name the phase that is
 * running. COPY keeps the direction word; the other phases have their own
 * present-participle word in the dock.
 */
typedef enum {
    XFER_PHASE_NONE = 0,
    XFER_PHASE_SCAN,
    XFER_PHASE_COPY,
    XFER_PHASE_VERIFY,
    XFER_PHASE_DELETE
} XferPhase;

typedef enum {
    XFER_ITEM_QUEUED = 0,
    XFER_ITEM_ACTIVE
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
 * Finish the active item. The next queued item starts immediately and is
 * returned through `next` when present. The caller owns the outcome: the
 * worker logs it and the batch counters record it, so the queue stores no
 * terminal copy.
 */
int xfer_release(XferQueue *queue, XferItem *next);

/* Cancel the active item. Queued items keep their order. */
int xfer_cancel_active(XferQueue *queue, XferItem *next);

/* R40: clear every queued item, keeping an active transfer. */
void xfer_clear_queued(XferQueue *queue);

#endif
