#include "fake_log.h"

#include <string.h>

static char s_lines[FAKE_LOG_LINES][FAKE_LOG_LINE_MAX];
static size_t s_lengths[FAKE_LOG_LINES];
static unsigned s_count;
static size_t s_capacity;
static size_t s_used;
static unsigned s_rotations;
static LogPort s_port;

static LogResult fake_append(void *ctx, const char *line, size_t len)
{
    (void)ctx;
    if (line == NULL || len >= FAKE_LOG_LINE_MAX)
        return LOG_DROPPED;
    if (s_capacity > 0 && len + 1 > s_capacity)
        return LOG_DROPPED; /* cannot fit even an empty file */
    if (s_capacity > 0 && s_used + len + 1 > s_capacity)
        return LOG_ROTATE;
    if (s_count == FAKE_LOG_LINES)
        return LOG_DROPPED;

    memcpy(s_lines[s_count], line, len);
    s_lines[s_count][len] = '\0';
    s_lengths[s_count] = len;
    s_count++;
    s_used += len + 1;
    return LOG_OK;
}

static LogResult fake_rotate(void *ctx)
{
    (void)ctx;
    s_rotations++;
    s_count = 0;
    s_used = 0;
    return LOG_OK;
}

void fake_log_reset(void)
{
    memset(s_lines, 0, sizeof(s_lines));
    memset(s_lengths, 0, sizeof(s_lengths));
    s_count = 0;
    s_capacity = 0;
    s_used = 0;
    s_rotations = 0;
    s_port.ctx = NULL;
    s_port.append = fake_append;
    s_port.rotate = fake_rotate;
}

LogPort *fake_log_port(void)
{
    return &s_port;
}

void fake_log_set_capacity(size_t bytes)
{
    s_capacity = bytes;
}

unsigned fake_log_count(void)
{
    return s_count;
}

const char *fake_log_line(unsigned index)
{
    if (index >= s_count)
        return NULL;
    return s_lines[index];
}

unsigned fake_log_rotations(void)
{
    return s_rotations;
}
