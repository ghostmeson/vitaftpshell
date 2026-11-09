/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_FAKE_IME_H
#define VFTP_FAKE_IME_H

#include "platform/ime.h"

#define FAKE_IME_QUEUE 8

typedef enum {
    FAKE_IME_TEXT = 0,
    FAKE_IME_CANCEL,
    FAKE_IME_UNAVAILABLE,
    FAKE_IME_ERROR
} FakeImeOutcome;

void fake_ime_reset(void);
TextInputPort *fake_ime_port(void);

void fake_ime_queue_text(const char *text);
void fake_ime_queue_cancel(void);
void fake_ime_queue_unavailable(void);
void fake_ime_queue_error(void);

#endif
