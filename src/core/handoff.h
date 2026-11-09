/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_HANDOFF_H
#define VFTP_CORE_HANDOFF_H

#include <stddef.h>
#include <stdint.h>

#include "core/cause.h"
#include "core/uiact.h"
#include "core/uistate.h"

/*
 * The cross-thread protocol (U15): fixed-record command and event rings,
 * latched terminal facts, three listing generations per pane, and one bulk
 * lane for the site book. Policy only; no platform call.
 *
 * Indices use release/acquire so the consumer sees a published record, not a
 * half-written one. The command ring may overflow (the lost count says so),
 * but a server stop and a cancel have dedicated slots and are never lost.
 */

#define HANDOFF_CMD_CAP 64
#define HANDOFF_EVT_CAP 128
#define HANDOFF_LIST_GENS 3
#define HANDOFF_BULK_BYTES 8192

typedef struct {
    UiCommand command;
    uint32_t seq;
} HandoffCommand;

typedef enum {
    HANDOFF_EVT_PROGRESS = 0,
    HANDOFF_EVT_STATE,
    HANDOFF_EVT_TRANSFER_DONE,
    HANDOFF_EVT_SERVER_STATE,
    HANDOFF_EVT_ERROR
} HandoffEventKind;

typedef struct {
    HandoffEventKind kind;
    uint32_t seq;
    uint64_t bytes;
    uint64_t total;
    int total_known;
    int ok;
    UiServerRun server_run;
    int clients;
    OpCause cause;
    OpStage stage;
    char path[PATH_MAX_LEN];
    char detail[128];
} HandoffEvent;

typedef struct {
    const void *rows;
    int row_count;
    uint32_t generation;
    int published;
    int held;
    int writing;
} HandoffListing;

typedef struct {
    HandoffListing slots[HANDOFF_LIST_GENS];
    uint32_t next_generation;
    int reader_slot;
    unsigned lost;
} HandoffListings;

typedef struct {
    unsigned char data[HANDOFF_BULK_BYTES];
    size_t len;
    uint32_t generation;
    int valid;
} HandoffBulk;

typedef struct {
    HandoffCommand commands[HANDOFF_CMD_CAP];
    uint32_t cmd_head;
    uint32_t cmd_tail;
    unsigned cmd_lost;
    uint32_t cmd_seq;

    int critical_stop_valid;
    HandoffCommand critical_stop;
    int critical_cancel_valid;
    HandoffCommand critical_cancel;

    HandoffEvent events[HANDOFF_EVT_CAP];
    uint32_t evt_head;
    uint32_t evt_tail;
    unsigned evt_lost;
    uint32_t evt_seq;

    /* Progress is a latch, not a ring entry: the newest sample wins. */
    HandoffEvent progress;
    uint32_t progress_seq; /* seqlock: odd while the producer writes */

    int server_valid;
    UiServerRun server_run;
    int server_clients;

    HandoffListings listings[UI_PANE_COUNT];
    HandoffBulk bulk;
} Handoff;

void handoff_init(Handoff *handoff);

int handoff_command_push(Handoff *handoff, const UiCommand *command);
int handoff_command_pop(Handoff *handoff, UiCommand *out);
unsigned handoff_command_lost(const Handoff *handoff);

int handoff_event_push(Handoff *handoff, const HandoffEvent *event);
int handoff_event_pop(Handoff *handoff, HandoffEvent *out);
unsigned handoff_event_lost(const Handoff *handoff);

void handoff_latch_server(Handoff *handoff, UiServerRun run, int clients);
int handoff_server_state(Handoff *handoff, UiServerRun *run, int *clients);

/* The writer reserves a slot, fills it, then publishes. -1 when all are held. */
int handoff_listing_begin(Handoff *handoff, UiPane pane);
int handoff_listing_publish(Handoff *handoff, UiPane pane, int slot,
                            const void *rows, int row_count);
/* The reader keeps one generation until it releases it at a frame boundary. */
int handoff_listing_acquire(Handoff *handoff, UiPane pane, const void **rows,
                            int *row_count, uint32_t *generation);
void handoff_listing_release(Handoff *handoff, UiPane pane);
unsigned handoff_listing_lost(const Handoff *handoff, UiPane pane);

int handoff_bulk_write(Handoff *handoff, const void *data, size_t len,
                       uint32_t generation);
int handoff_bulk_read(Handoff *handoff, void *out, size_t cap, size_t *len,
                      uint32_t *generation);

#endif
