/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_LOGEV_H
#define VFTP_CORE_LOGEV_H

#include <stddef.h>
#include <stdint.h>

#include "platform/log.h"

/*
 * Session-log policy. A record has typed fields only: there is deliberately no
 * raw-reply field, because a hostile server can echo a credential back in a
 * 530. Passwords are never a parameter here, so they cannot reach a line.
 *
 * One tab-separated record: monotonic milliseconds, event name, then the
 * key=value fields that apply. At 256 KiB the caller rotates once and retries.
 * The adapter keeps three generations: session.log, session.old.log, and
 * session.old2.log.
 */

#define LOGEV_CAP 262144
/* A path can percent-encode to three bytes per source byte. */
#define LOGEV_LINE_MAX 4096
#define LOGEV_NO_VALUE INT32_MIN

typedef enum {
    LOGEV_CONNECT = 0,
    LOGEV_CONNECT_FAIL,
    LOGEV_LOGIN,
    LOGEV_LIST,
    LOGEV_TRANSFER_START,
    LOGEV_TRANSFER_DONE,
    LOGEV_TRANSFER_FAIL,
    LOGEV_SERVER_START,
    LOGEV_SERVER_STOP,
    LOGEV_SERVER_CLIENT,
    LOGEV_SAVE,
    LOGEV_APP_START,
    LOGEV_APP_STOP,
    LOGEV_SCREEN,
    LOGEV_KEY,
    LOGEV_COMMAND,
    LOGEV_IME,
    LOGEV_QUEUE,
    LOGEV_ERROR,
    LOGEV_LOG,
    LOGEV_SETTINGS,
    LOGEV_EVENT_COUNT
} LogEvent;

typedef struct {
    LogEvent event;

    const char *site;
    const char *host;
    const char *path;
    const char *stage;
    const char *cls;

    /* New diagnostic fields. All are optional; detail is a mapped reason, never
     * a raw server reply or a field value. */
    const char *screen;
    const char *key;
    const char *command;
    const char *detail;
    const char *version;
    int build;
    int value;      /* LOGEV_NO_VALUE when absent */
    int repeat;
    int masked;

    uint64_t bytes;
    uint64_t total;
    int port;    /* -1 when absent */
    int clients; /* -1 when absent */
} LogRecord;

void logev_init(LogRecord *record, LogEvent event);
const char *logev_event_name(LogEvent event);
int logev_format(char *buf, size_t n, uint64_t now_ms, const LogRecord *record);
LogResult logev_emit(LogPort *log, uint64_t now_ms, const LogRecord *record);
int logev_should_rotate(uint64_t current_size, size_t line_len);

#endif
