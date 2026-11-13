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
#define HANDOFF_BULK_BYTES 16384
#define HANDOFF_CONFIRM_ITEMS 16
#define HANDOFF_LOG_CAP 96

typedef struct {
    UiCommand command;
    uint32_t seq;
} HandoffCommand;

typedef enum {
    HANDOFF_EVT_PROGRESS = 0,
    HANDOFF_EVT_STATE,
    HANDOFF_EVT_TRANSFER_STARTED,
    HANDOFF_EVT_TRANSFER_DONE,
    HANDOFF_EVT_SERVER_STATE,
    HANDOFF_EVT_ERROR,
    HANDOFF_EVT_SESSION,
    HANDOFF_EVT_LISTING_BEGIN,
    HANDOFF_EVT_LISTING_END,
    HANDOFF_EVT_SITE_BOOK,
    HANDOFF_EVT_SERVER_CLIENT,
    HANDOFF_EVT_TRANSFER_CANCELLED,
    HANDOFF_EVT_EDIT_SITE
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
    UiPane pane;
    int truncated;
    int connected;
    char op[32];
    int item_index;
    int item_total;
    uint64_t rate;
    int rate_known;
    uint64_t elapsed_ms;
    int elapsed_known;
    uint64_t free_bytes;
    int free_known;
    int port;
    XferDirection direction;
    int value;
    char address[32];
    int address_known;
    Site site;
} HandoffEvent;

typedef struct {
    char name[PATH_NAME_MAX];
    uint64_t size;
    int size_known;
} HandoffConfirmItem;

typedef struct {
    int valid;
    UiConfirmKind kind;
    int item_count;
    int existing_count;
    int high_risk;
    char risk_path[PATH_MAX_LEN];
    char context[128];
    HandoffConfirmItem items[HANDOFF_CONFIRM_ITEMS];
} HandoffConfirm;

typedef enum {
    HANDOFF_LOG_SCREEN = 0,
    HANDOFF_LOG_KEY,
    HANDOFF_LOG_COMMAND,
    HANDOFF_LOG_IME
} HandoffLogKind;

typedef struct {
    HandoffLogKind kind;
    char screen[24];
    char key[16];
    char command[24];
    char path[PATH_MAX_LEN];
    char detail[96];
    int repeat;
    int masked;
    int value;
    int value_set;
} HandoffLogRecord;

typedef struct {
    HandoffLogRecord records[HANDOFF_LOG_CAP];
    uint32_t head;
    uint32_t tail;
    unsigned lost;
    uint32_t seq;
} HandoffLog;

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

    HandoffLog log;
    int confirm_valid;
    HandoffConfirm confirm;
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

int handoff_log_push(Handoff *handoff, const HandoffLogRecord *record);
int handoff_log_pop(Handoff *handoff, HandoffLogRecord *out);
unsigned handoff_log_lost(const Handoff *handoff);

void handoff_confirm_request(Handoff *handoff, const HandoffConfirm *confirm);
int handoff_confirm_take(Handoff *handoff, HandoffConfirm *out);

int handoff_bulk_write(Handoff *handoff, const void *data, size_t len,
                       uint32_t generation);
int handoff_bulk_read(Handoff *handoff, void *out, size_t cap, size_t *len,
                      uint32_t *generation);

#endif
