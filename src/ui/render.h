/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_UI_RENDER_H
#define VFTP_UI_RENDER_H

#include "core/uistate.h"

/*
 * The one entry point the app calls. It dispatches the current screen, draws
 * the shared header, and draws the text-entry notice when the IME is missing.
 * It reads state and calls gfx; it writes nothing.
 */
void ui_render(const UiState *state);

const char *ui_screen_name(UiScreen screen);

#endif
