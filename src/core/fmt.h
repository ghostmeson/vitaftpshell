/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_FMT_H
#define VFTP_CORE_FMT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Display formatting. A value the source cannot report is `--`, never a zero
 * or a guess. Sizes use 1024-based units with one truncated tenth.
 */

void fmt_size(char *buf, size_t n, uint64_t bytes, int known);
void fmt_rate(char *buf, size_t n, uint64_t bytes_per_second, int known);
void fmt_percent(char *buf, size_t n, uint64_t done, uint64_t total,
                 int total_known);
void fmt_count(char *buf, size_t n, unsigned count, const char *singular,
               const char *plural);

#endif
