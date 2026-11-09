#ifndef VFTP_PLATFORM_LOG_H
#define VFTP_PLATFORM_LOG_H

#include <stddef.h>

/*
 * Session-log contract.
 *
 * `append` writes one complete line or refuses it whole. A refusal returns
 * LOG_ROTATE; the caller rotates once and retries the same line. A line that
 * does not fit even an empty file returns LOG_DROPPED, never a truncated field.
 */
typedef enum {
    LOG_OK = 0,
    LOG_ROTATE,
    LOG_DROPPED,
    LOG_ERROR
} LogResult;

typedef struct LogPort LogPort;

struct LogPort {
    void *ctx;
    LogResult (*append)(void *ctx, const char *line, size_t len);
    LogResult (*rotate)(void *ctx);
};

#endif
