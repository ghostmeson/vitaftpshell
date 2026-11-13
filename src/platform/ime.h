/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_PLATFORM_IME_H
#define VFTP_PLATFORM_IME_H

#include <stddef.h>

/*
 * System text-entry contract. The app opens the dialog with a title, initial
 * value, password flag, and maximum length; it polls once per frame so the
 * frame loop keeps running; the result is UTF-8 or cancellation.
 */

#define IME_TEXT_MAX 256

typedef struct {
    const char *title;
    const char *initial;
    int password;
    size_t max_length;
} ImeRequest;

typedef struct {
    char text[IME_TEXT_MAX];
} ImeResponse;

typedef enum {
    IME_BEGIN_RUNNING = 0,
    IME_BEGIN_UNAVAILABLE,
    IME_BEGIN_ERROR
} ImeBeginResult;

typedef enum {
    IME_POLL_RUNNING = 0,
    IME_POLL_DONE,
    IME_POLL_CANCELLED,
    IME_POLL_ERROR
} ImePollResult;

typedef struct TextInputPort TextInputPort;

struct TextInputPort {
    void *ctx;
    ImeBeginResult (*begin)(void *ctx, const ImeRequest *request);
    ImePollResult (*poll)(void *ctx, ImeResponse *out);
    void (*end)(void *ctx);
    /* Raw platform code from the last failed begin or poll, 0 when none.
     * Policy never interprets it; the app writes it to the session log. */
    int (*last_error)(void *ctx);
};

#endif
