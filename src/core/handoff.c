/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/handoff.h"

#include <string.h>

#ifdef HANDOFF_PLAIN
/* Test-only build used by tools/test/tsan.sh to prove that removing the
 * release/acquire ordering is caught as a data race. */
#define LOAD_ACQ(ptr) (*(ptr))
#define STORE_REL(ptr, value) (*(ptr) = (value))
#else
#define LOAD_ACQ(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define STORE_REL(ptr, value) __atomic_store_n((ptr), (value), __ATOMIC_RELEASE)
#endif

void handoff_init(Handoff *handoff)
{
    if (handoff == NULL)
        return;
    memset(handoff, 0, sizeof(*handoff));
    for (int pane = 0; pane < UI_PANE_COUNT; ++pane)
        handoff->listings[pane].reader_slot = -1;
}

int handoff_command_push(Handoff *handoff, const UiCommand *command)
{
    if (handoff == NULL || command == NULL)
        return 0;

    if (command->type == UI_CMD_STOP_SERVER) {
        handoff->critical_stop.command = *command;
        handoff->critical_stop.seq = ++handoff->cmd_seq;
        STORE_REL(&handoff->critical_stop_valid, 1);
        return 1;
    }
    if (command->type == UI_CMD_CANCEL_TRANSFER) {
        handoff->critical_cancel.command = *command;
        handoff->critical_cancel.seq = ++handoff->cmd_seq;
        STORE_REL(&handoff->critical_cancel_valid, 1);
        return 1;
    }

    uint32_t head = LOAD_ACQ(&handoff->cmd_head);
    uint32_t tail = LOAD_ACQ(&handoff->cmd_tail);
    if (head - tail >= HANDOFF_CMD_CAP) {
        handoff->cmd_lost++;
        return 0;
    }

    HandoffCommand *record = &handoff->commands[head % HANDOFF_CMD_CAP];
    record->command = *command;
    record->seq = ++handoff->cmd_seq;
    STORE_REL(&handoff->cmd_head, head + 1);
    return 1;
}

int handoff_command_pop(Handoff *handoff, UiCommand *out)
{
    if (handoff == NULL)
        return 0;

    if (LOAD_ACQ(&handoff->critical_stop_valid)) {
        if (out != NULL)
            *out = handoff->critical_stop.command;
        STORE_REL(&handoff->critical_stop_valid, 0);
        return 1;
    }
    if (LOAD_ACQ(&handoff->critical_cancel_valid)) {
        if (out != NULL)
            *out = handoff->critical_cancel.command;
        STORE_REL(&handoff->critical_cancel_valid, 0);
        return 1;
    }

    uint32_t head = LOAD_ACQ(&handoff->cmd_head);
    uint32_t tail = LOAD_ACQ(&handoff->cmd_tail);
    if (head == tail)
        return 0;

    if (out != NULL)
        *out = handoff->commands[tail % HANDOFF_CMD_CAP].command;
    STORE_REL(&handoff->cmd_tail, tail + 1);
    return 1;
}

unsigned handoff_command_lost(const Handoff *handoff)
{
    return handoff != NULL ? handoff->cmd_lost : 0;
}

int handoff_event_push(Handoff *handoff, const HandoffEvent *event)
{
    if (handoff == NULL || event == NULL)
        return 0;

    if (event->kind == HANDOFF_EVT_PROGRESS) {
        uint32_t seq = LOAD_ACQ(&handoff->progress_seq);
        STORE_REL(&handoff->progress_seq, seq + 1);
        handoff->progress = *event;
        handoff->progress.seq = ++handoff->evt_seq;
        STORE_REL(&handoff->progress_seq, seq + 2);
        return 1;
    }

    uint32_t head = LOAD_ACQ(&handoff->evt_head);
    uint32_t tail = LOAD_ACQ(&handoff->evt_tail);
    if (head - tail >= HANDOFF_EVT_CAP) {
        handoff->evt_lost++;
        return 0;
    }

    HandoffEvent *record = &handoff->events[head % HANDOFF_EVT_CAP];
    *record = *event;
    record->seq = ++handoff->evt_seq;
    STORE_REL(&handoff->evt_head, head + 1);
    return 1;
}

int handoff_event_pop(Handoff *handoff, HandoffEvent *out)
{
    if (handoff == NULL)
        return 0;

    uint32_t head = LOAD_ACQ(&handoff->evt_head);
    uint32_t tail = LOAD_ACQ(&handoff->evt_tail);
    if (head != tail) {
        HandoffEvent *record = &handoff->events[tail % HANDOFF_EVT_CAP];
        /* Drop a progress sample older than the terminal record now being
         * delivered; the dock must not return to ACTIVE after DONE/FAILED. */
        uint32_t pseq = LOAD_ACQ(&handoff->progress_seq);
        if (pseq != 0 && (pseq & 1u) == 0 &&
            handoff->progress.seq < record->seq) {
            uint32_t expected = pseq;
            __atomic_compare_exchange_n(&handoff->progress_seq, &expected, 0,
                                       0, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
        }
        if (out != NULL)
            *out = *record;
        STORE_REL(&handoff->evt_tail, tail + 1);
        return 1;
    }

    for (;;) {
        uint32_t first = LOAD_ACQ(&handoff->progress_seq);
        if (first == 0 || (first & 1u))
            return 0;
        if (out != NULL)
            *out = handoff->progress;
        uint32_t second = LOAD_ACQ(&handoff->progress_seq);
        if (first != second)
            continue;
        uint32_t expected = second;
        if (__atomic_compare_exchange_n(&handoff->progress_seq, &expected, 0,
                                       0, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE))
            return 1;
    }
}

unsigned handoff_event_lost(const Handoff *handoff)
{
    return handoff != NULL ? handoff->evt_lost : 0;
}

void handoff_latch_server(Handoff *handoff, UiServerRun run, int clients)
{
    if (handoff == NULL)
        return;
    handoff->server_run = run;
    handoff->server_clients = clients;
    STORE_REL(&handoff->server_valid, 1);
}

int handoff_server_state(Handoff *handoff, UiServerRun *run, int *clients)
{
    if (handoff == NULL || !LOAD_ACQ(&handoff->server_valid))
        return 0;
    if (run != NULL)
        *run = handoff->server_run;
    if (clients != NULL)
        *clients = handoff->server_clients;
    return 1;
}

int handoff_listing_begin(Handoff *handoff, UiPane pane)
{
    if (handoff == NULL || pane >= UI_PANE_COUNT)
        return -1;

    HandoffListings *listings = &handoff->listings[pane];
    int chosen = -1;
    uint32_t oldest = UINT32_MAX;

    for (int i = 0; i < HANDOFF_LIST_GENS; ++i) {
        HandoffListing *slot = &listings->slots[i];
        if (LOAD_ACQ(&slot->held) || LOAD_ACQ(&slot->writing))
            continue;
        if (!LOAD_ACQ(&slot->published)) {
            chosen = i;
            break;
        }
        if (slot->generation < oldest) {
            oldest = slot->generation;
            chosen = i;
        }
    }

    if (chosen < 0) {
        listings->lost++;
        return -1;
    }

    STORE_REL(&listings->slots[chosen].writing, 1);
    return chosen;
}

int handoff_listing_publish(Handoff *handoff, UiPane pane, int slot,
                            const void *rows, int row_count)
{
    if (handoff == NULL || pane >= UI_PANE_COUNT || slot < 0 ||
        slot >= HANDOFF_LIST_GENS)
        return 0;

    HandoffListings *listings = &handoff->listings[pane];
    HandoffListing *entry = &listings->slots[slot];
    entry->rows = rows;
    entry->row_count = row_count;
    entry->generation = ++listings->next_generation;
    STORE_REL(&entry->published, 1);
    STORE_REL(&entry->writing, 0);
    return 1;
}

int handoff_listing_acquire(Handoff *handoff, UiPane pane, const void **rows,
                            int *row_count, uint32_t *generation)
{
    if (handoff == NULL || pane >= UI_PANE_COUNT)
        return 0;

    HandoffListings *listings = &handoff->listings[pane];

    if (listings->reader_slot >= 0) {
        HandoffListing *held = &listings->slots[listings->reader_slot];
        if (rows != NULL)
            *rows = held->rows;
        if (row_count != NULL)
            *row_count = held->row_count;
        if (generation != NULL)
            *generation = held->generation;
        return 1;
    }

    int chosen = -1;
    uint32_t newest = 0;
    for (int i = 0; i < HANDOFF_LIST_GENS; ++i) {
        HandoffListing *slot = &listings->slots[i];
        if (LOAD_ACQ(&slot->writing) || !LOAD_ACQ(&slot->published))
            continue;
        if (chosen < 0 || slot->generation > newest) {
            chosen = i;
            newest = slot->generation;
        }
    }

    if (chosen < 0)
        return 0;

    HandoffListing *slot = &listings->slots[chosen];
    STORE_REL(&slot->held, 1);
    listings->reader_slot = chosen;
    if (rows != NULL)
        *rows = slot->rows;
    if (row_count != NULL)
        *row_count = slot->row_count;
    if (generation != NULL)
        *generation = slot->generation;
    return 1;
}

void handoff_listing_release(Handoff *handoff, UiPane pane)
{
    if (handoff == NULL || pane >= UI_PANE_COUNT)
        return;
    HandoffListings *listings = &handoff->listings[pane];
    if (listings->reader_slot < 0)
        return;
    STORE_REL(&listings->slots[listings->reader_slot].held, 0);
    listings->reader_slot = -1;
}

unsigned handoff_listing_lost(const Handoff *handoff, UiPane pane)
{
    if (handoff == NULL || pane >= UI_PANE_COUNT)
        return 0;
    return handoff->listings[pane].lost;
}

void handoff_confirm_request(Handoff *handoff, const HandoffConfirm *confirm)
{
    if (handoff == NULL || confirm == NULL)
        return;
    handoff->confirm = *confirm;
    handoff->confirm.valid = 1;
    STORE_REL(&handoff->confirm_valid, 1);
}

int handoff_confirm_take(Handoff *handoff, HandoffConfirm *out)
{
    if (handoff == NULL || !LOAD_ACQ(&handoff->confirm_valid))
        return 0;
    if (out != NULL)
        *out = handoff->confirm;
    STORE_REL(&handoff->confirm_valid, 0);
    return 1;
}

int handoff_bulk_write(Handoff *handoff, const void *data, size_t len,
                       uint32_t generation)
{
    if (handoff == NULL || data == NULL || len > HANDOFF_BULK_BYTES)
        return 0;
    if (LOAD_ACQ(&handoff->bulk.valid))
        return 0;

    memcpy(handoff->bulk.data, data, len);
    handoff->bulk.len = len;
    handoff->bulk.generation = generation;
    STORE_REL(&handoff->bulk.valid, 1);
    return 1;
}

int handoff_bulk_read(Handoff *handoff, void *out, size_t cap, size_t *len,
                      uint32_t *generation)
{
    if (handoff == NULL || !LOAD_ACQ(&handoff->bulk.valid))
        return 0;
    if (cap < handoff->bulk.len)
        return 0;

    memcpy(out, handoff->bulk.data, handoff->bulk.len);
    if (len != NULL)
        *len = handoff->bulk.len;
    if (generation != NULL)
        *generation = handoff->bulk.generation;
    STORE_REL(&handoff->bulk.valid, 0);
    return 1;
}
