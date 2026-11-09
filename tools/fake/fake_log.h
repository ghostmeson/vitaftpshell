#ifndef VFTP_FAKE_LOG_H
#define VFTP_FAKE_LOG_H

#include <stddef.h>

#include "platform/log.h"

#define FAKE_LOG_LINES 64
#define FAKE_LOG_LINE_MAX 512

void fake_log_reset(void);
LogPort *fake_log_port(void);

void fake_log_set_capacity(size_t bytes);
unsigned fake_log_count(void);
const char *fake_log_line(unsigned index);
unsigned fake_log_rotations(void);

#endif
