/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_UI_TEXTENTRY_H
#define VFTP_UI_TEXTENTRY_H

#include "core/uistate.h"

/* The IME is a system surface. This draws only the unavailable notice. */
void textentry_draw(const UiState *state);

#endif
