/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_UI_HEADER_H
#define VFTP_UI_HEADER_H

#include "core/uistate.h"

/* The shared header: wordmark, screen name, connection identity, server. */
void header_draw(const UiState *state);

#endif
