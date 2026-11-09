/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/render.h"

#include "gfx/draw.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/actions.h"
#include "ui/confirm.h"
#include "ui/editor.h"
#include "ui/header.h"
#include "ui/server.h"
#include "ui/sites.h"
#include "ui/textentry.h"
#include "ui/workspace.h"

const char *ui_screen_name(UiScreen screen)
{
    switch (screen) {
    case UI_SCREEN_WORKSPACE:
        return "WORKSPACE";
    case UI_SCREEN_SITES:
        return "SITES";
    case UI_SCREEN_SITE_EDITOR:
        return "SITE EDITOR";
    case UI_SCREEN_SERVER:
        return "SERVER";
    case UI_SCREEN_ACTIONS:
        return "ACTIONS";
    case UI_SCREEN_CONFIRM:
        return "CONFIRM";
    case UI_SCREEN_TEXT_ENTRY:
        return "TEXT ENTRY";
    default:
        return "WORKSPACE";
    }
}

static void draw_screen(const UiState *state, UiScreen screen)
{
    switch (screen) {
    case UI_SCREEN_SITES:
        sites_draw(state);
        break;
    case UI_SCREEN_SITE_EDITOR:
        editor_draw(state);
        break;
    case UI_SCREEN_SERVER:
        server_draw(state);
        break;
    case UI_SCREEN_ACTIONS:
        actions_draw(state);
        break;
    case UI_SCREEN_CONFIRM:
        confirm_draw(state);
        break;
    case UI_SCREEN_TEXT_ENTRY:
        draw_screen(state, state->editor_return);
        textentry_draw(state);
        break;
    case UI_SCREEN_WORKSPACE:
    default:
        workspace_draw(state);
        break;
    }
}

void ui_render(const UiState *state)
{
    if (state == NULL)
        return;

    draw_rect(0, 0, (float)LAYOUT_SCREEN_W, (float)LAYOUT_SCREEN_H, C_FIELD);
    header_draw(state);
    draw_screen(state, state->screen);

    /* The IME is a system surface. When it cannot open, the calling screen
     * stays behind and this notice names the unchanged field. */
    if (state->text.unavailable && state->screen != UI_SCREEN_CONFIRM)
        textentry_draw(state);
}
