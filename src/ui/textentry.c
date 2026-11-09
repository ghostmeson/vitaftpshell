/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/textentry.h"

#include "gfx/font.h"
#include "gfx/theme.h"
#include "ui/parts.h"

#define TEXT_X 180.0f
#define TEXT_Y 180.0f
#define TEXT_W 600.0f
#define TEXT_H 184.0f

void textentry_draw(const UiState *state)
{
    if (state == NULL || !state->text.unavailable)
        return;

    draw_rect(0.0f, 0.0f, 960.0f, 544.0f, 0xB8000000u);

    Rect panel = {TEXT_X, TEXT_Y, TEXT_W, TEXT_H};
    ui_panel_frame(panel, 0, C_YELLOW);

    font_text("TEXT ENTRY UNAVAILABLE", 210.0f, 200.0f, PX_VALUE, C_YELLOW);
    font_text("FIELD UNCHANGED", 210.0f, 240.0f, PX_LABEL, C_DIM);
    font_text("CIRCLE TO RETURN", 210.0f, 330.0f, PX_MICRO, C_DIM);
}
