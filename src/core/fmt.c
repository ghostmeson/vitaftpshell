/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/fmt.h"

#include <stdio.h>
#include <string.h>

static const char *const UNITS[] = {"KB", "MB", "GB", "TB"};

static void format_scaled(char *buf, size_t n, uint64_t value)
{
    if (value < 1024) {
        snprintf(buf, n, "%llu B", (unsigned long long)value);
        return;
    }

    uint64_t unit = 1024;
    int u = 0;
    while (u < 3 && value >= unit * 1024) {
        unit *= 1024;
        u++;
    }

    uint64_t whole = value / unit;
    uint64_t frac = (value % unit) * 10 / unit;
    char text[64];
    snprintf(text, sizeof(text), "%llu.%llu %s", (unsigned long long)whole,
             (unsigned long long)frac, UNITS[u]);
    size_t len = strlen(text);
    if (len >= n)
        len = n - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';
}

void fmt_size(char *buf, size_t n, uint64_t bytes, int known)
{
    if (buf == NULL || n == 0)
        return;
    if (!known) {
        snprintf(buf, n, "--");
        return;
    }
    format_scaled(buf, n, bytes);
}

void fmt_rate(char *buf, size_t n, uint64_t bytes_per_second, int known)
{
    if (buf == NULL || n == 0)
        return;
    if (!known) {
        snprintf(buf, n, "--");
        return;
    }

    char rate[32];
    format_scaled(rate, sizeof(rate), bytes_per_second);
    snprintf(buf, n, "%s/S", rate);
}

void fmt_percent(char *buf, size_t n, uint64_t done, uint64_t total,
                 int total_known)
{
    if (buf == NULL || n == 0)
        return;
    if (!total_known || total == 0) {
        snprintf(buf, n, "--");
        return;
    }

    if (done >= total) {
        snprintf(buf, n, "100%%");
        return;
    }

    snprintf(buf, n, "%llu%%", (unsigned long long)(done * 100 / total));
}

void fmt_copy(char *dst, size_t n, const char *src)
{
    if (dst == NULL || n == 0)
        return;
    if (src == NULL)
        src = "";
    size_t len = strlen(src);
    if (len >= n)
        len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void fmt_count(char *buf, size_t n, unsigned count, const char *singular,
               const char *plural)
{
    if (buf == NULL || n == 0)
        return;
    snprintf(buf, n, "%u %s", count, count == 1 ? singular : plural);
}
