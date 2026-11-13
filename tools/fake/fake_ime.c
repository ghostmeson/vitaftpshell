/* SPDX-License-Identifier: GPL-3.0-only */

#include "fake_ime.h"

#include <string.h>

typedef struct {
    FakeImeOutcome outcome;
    char text[IME_TEXT_MAX];
} FakeImeEntry;

static FakeImeEntry s_queue[FAKE_IME_QUEUE];
static unsigned s_head;
static unsigned s_count;
static FakeImeEntry s_current;
static int s_running;
static TextInputPort s_port;

static ImeBeginResult fake_begin(void *ctx, const ImeRequest *request)
{
    (void)ctx;
    (void)request;

    if (s_count == 0)
        return IME_BEGIN_ERROR;

    s_current = s_queue[s_head];
    s_head = (s_head + 1) % FAKE_IME_QUEUE;
    s_count--;

    if (s_current.outcome == FAKE_IME_UNAVAILABLE)
        return IME_BEGIN_UNAVAILABLE;
    if (s_current.outcome == FAKE_IME_ERROR)
        return IME_BEGIN_ERROR;

    s_running = 1;
    return IME_BEGIN_RUNNING;
}

static ImePollResult fake_poll(void *ctx, ImeResponse *out)
{
    (void)ctx;
    if (!s_running)
        return IME_POLL_ERROR;

    s_running = 0;
    if (s_current.outcome == FAKE_IME_TEXT) {
        strncpy(out->text, s_current.text, sizeof(out->text) - 1);
        out->text[sizeof(out->text) - 1] = '\0';
        return IME_POLL_DONE;
    }
    if (s_current.outcome == FAKE_IME_CANCEL)
        return IME_POLL_CANCELLED;
    return IME_POLL_ERROR;
}

static void fake_end(void *ctx)
{
    (void)ctx;
    s_running = 0;
    memset(&s_current, 0, sizeof(s_current));
}

static int fake_last_error(void *ctx)
{
    (void)ctx;
    return 0;
}

void fake_ime_reset(void)
{
    memset(s_queue, 0, sizeof(s_queue));
    memset(&s_current, 0, sizeof(s_current));
    s_head = 0;
    s_count = 0;
    s_running = 0;
    s_port.ctx = NULL;
    s_port.begin = fake_begin;
    s_port.poll = fake_poll;
    s_port.end = fake_end;
    s_port.last_error = fake_last_error;
}

TextInputPort *fake_ime_port(void)
{
    return &s_port;
}

static void queue(FakeImeOutcome outcome, const char *text)
{
    if (s_count == FAKE_IME_QUEUE)
        return;
    unsigned slot = (s_head + s_count) % FAKE_IME_QUEUE;
    memset(&s_queue[slot], 0, sizeof(s_queue[slot]));
    s_queue[slot].outcome = outcome;
    if (text != NULL) {
        strncpy(s_queue[slot].text, text, sizeof(s_queue[slot].text) - 1);
        s_queue[slot].text[sizeof(s_queue[slot].text) - 1] = '\0';
    }
    s_count++;
}

void fake_ime_queue_text(const char *text)
{
    queue(FAKE_IME_TEXT, text);
}

void fake_ime_queue_cancel(void)
{
    queue(FAKE_IME_CANCEL, NULL);
}

void fake_ime_queue_unavailable(void)
{
    queue(FAKE_IME_UNAVAILABLE, NULL);
}

void fake_ime_queue_error(void)
{
    queue(FAKE_IME_ERROR, NULL);
}
