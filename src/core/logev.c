/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/logev.h"

#include <stdio.h>
#include <string.h>

static int safe_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
           c == '~' || c == '/';
}

static int append_field(char *buf, size_t n, size_t *used, const char *key,
                        const char *value)
{
    static const char HEX[] = "0123456789ABCDEF";
    int wrote = snprintf(buf + *used, n - *used, "\t%s=", key);
    if (wrote < 0 || (size_t)wrote >= n - *used)
        return 0;
    *used += (size_t)wrote;

    for (const unsigned char *p = (const unsigned char *)value; *p != '\0';
         ++p) {
        char chunk[4];
        size_t len = 1;
        if (safe_char((char)*p)) {
            chunk[0] = (char)*p;
        } else {
            chunk[0] = '%';
            chunk[1] = HEX[*p >> 4];
            chunk[2] = HEX[*p & 0x0F];
            len = 3;
        }
        if (*used + len >= n)
            return 0;
        memcpy(buf + *used, chunk, len);
        *used += len;
    }
    buf[*used] = '\0';
    return 1;
}

static int append_uint(char *buf, size_t n, size_t *used, const char *key,
                       uint64_t value)
{
    int wrote = snprintf(buf + *used, n - *used, "\t%s=%llu", key,
                         (unsigned long long)value);
    if (wrote < 0 || (size_t)wrote >= n - *used)
        return 0;
    *used += (size_t)wrote;
    return 1;
}

void logev_init(LogRecord *record, LogEvent event)
{
    if (record == NULL)
        return;
    memset(record, 0, sizeof(*record));
    record->event = event;
    record->port = -1;
    record->clients = -1;
    record->build = -1;
    record->value = LOGEV_NO_VALUE;
}

const char *logev_event_name(LogEvent event)
{
    switch (event) {
    case LOGEV_CONNECT:
        return "connect";
    case LOGEV_CONNECT_FAIL:
        return "connect_fail";
    case LOGEV_LOGIN:
        return "login";
    case LOGEV_LIST:
        return "list";
    case LOGEV_TRANSFER_START:
        return "transfer_start";
    case LOGEV_TRANSFER_DONE:
        return "transfer_done";
    case LOGEV_TRANSFER_FAIL:
        return "transfer_fail";
    case LOGEV_SERVER_START:
        return "server_start";
    case LOGEV_SERVER_STOP:
        return "server_stop";
    case LOGEV_SERVER_CLIENT:
        return "server_client";
    case LOGEV_SAVE:
        return "save";
    case LOGEV_APP_START:
        return "app_start";
    case LOGEV_APP_STOP:
        return "app_stop";
    case LOGEV_SCREEN:
        return "screen";
    case LOGEV_KEY:
        return "key";
    case LOGEV_COMMAND:
        return "command";
    case LOGEV_IME:
        return "ime";
    case LOGEV_QUEUE:
        return "queue";
    case LOGEV_ERROR:
        return "error";
    case LOGEV_LOG:
        return "log";
    case LOGEV_SETTINGS:
        return "settings";
    default:
        return "unknown";
    }
}

int logev_format(char *buf, size_t n, uint64_t now_ms, const LogRecord *record)
{
    if (buf == NULL || n == 0 || record == NULL)
        return 0;

    int wrote = snprintf(buf, n, "%llu\t%s", (unsigned long long)now_ms,
                         logev_event_name(record->event));
    if (wrote < 0 || (size_t)wrote >= n)
        return 0;
    size_t used = (size_t)wrote;

    if (record->site != NULL &&
        !append_field(buf, n, &used, "site", record->site))
        return 0;
    if (record->host != NULL &&
        !append_field(buf, n, &used, "host", record->host))
        return 0;
    if (record->path != NULL &&
        !append_field(buf, n, &used, "path", record->path))
        return 0;
    if (record->stage != NULL &&
        !append_field(buf, n, &used, "stage", record->stage))
        return 0;
    if (record->cls != NULL &&
        !append_field(buf, n, &used, "cls", record->cls))
        return 0;
    if (record->screen != NULL &&
        !append_field(buf, n, &used, "screen", record->screen))
        return 0;
    if (record->key != NULL &&
        !append_field(buf, n, &used, "key", record->key))
        return 0;
    if (record->command != NULL &&
        !append_field(buf, n, &used, "cmd", record->command))
        return 0;
    if (record->detail != NULL &&
        !append_field(buf, n, &used, "detail", record->detail))
        return 0;
    if (record->version != NULL &&
        !append_field(buf, n, &used, "ver", record->version))
        return 0;
    if (record->build >= 0 &&
        !append_uint(buf, n, &used, "build", (uint64_t)record->build))
        return 0;
    if (record->value != LOGEV_NO_VALUE &&
        !append_uint(buf, n, &used, "value", (uint64_t)record->value))
        return 0;
    if (record->repeat &&
        !append_uint(buf, n, &used, "repeat", 1))
        return 0;
    if (record->masked &&
        !append_uint(buf, n, &used, "masked", 1))
        return 0;

    if (record->event == LOGEV_TRANSFER_START ||
        record->event == LOGEV_TRANSFER_DONE ||
        record->event == LOGEV_TRANSFER_FAIL) {
        if (!append_uint(buf, n, &used, "bytes", record->bytes))
            return 0;
        if (!append_uint(buf, n, &used, "total", record->total))
            return 0;
    }

    if (record->port >= 0 &&
        !append_uint(buf, n, &used, "port", (uint64_t)record->port))
        return 0;
    if (record->clients >= 0 &&
        !append_uint(buf, n, &used, "clients", (uint64_t)record->clients))
        return 0;

    return (int)used;
}

int logev_should_rotate(uint64_t current_size, size_t line_len)
{
    return current_size > 0 && current_size + line_len + 1 > LOGEV_CAP;
}

LogResult logev_emit(LogPort *log, uint64_t now_ms, const LogRecord *record)
{
    if (log == NULL || record == NULL)
        return LOG_ERROR;

    char line[LOGEV_LINE_MAX];
    int len = logev_format(line, sizeof(line), now_ms, record);
    if (len <= 0)
        return LOG_DROPPED;

    LogResult result = log->append(log->ctx, line, (size_t)len);
    if (result == LOG_ROTATE) {
        if (log->rotate(log->ctx) != LOG_OK)
            return LOG_ERROR;
        result = log->append(log->ctx, line, (size_t)len);
        if (result == LOG_ROTATE)
            return LOG_DROPPED; /* larger than an empty file: never loop */
    }
    return result;
}
