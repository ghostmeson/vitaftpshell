/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/header.h"

#include <stdio.h>

#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/parts.h"
#include "ui/render.h"

#define HEADER_RIGHT 942.0f
#define HEADER_NAME_Y 42.0f
#define HEADER_SERVER_Y 45.5f

static uint32_t alpha_color(uint32_t color, float alpha)
{
    if (alpha <= 0.0f)
        return 0;
    if (alpha > 1.0f)
        alpha = 1.0f;
    uint32_t a = (uint32_t)(alpha * 255.0f + 0.5f);
    return (a << 24) | (color & 0x00FFFFFFu);
}

static void wordmark(void)
{
    const char *text = "VITAFTPSHELL";
    const float x = (float)LAYOUT_MARGIN;
    const float y = 8.0f;
    const float px = PX_HERO;

    draw_set_additive(1);
    font_text(text, x - 2.0f, y, px, alpha_color(C_YELLOW, 0.10f));
    font_text(text, x + 2.0f, y, px, alpha_color(C_YELLOW, 0.10f));
    font_text(text, x, y - 2.0f, px, alpha_color(C_YELLOW, 0.10f));
    font_text(text, x, y + 2.0f, px, alpha_color(C_YELLOW, 0.10f));
    draw_set_additive(0);

    /* Chroma: one offset per side, as in the settled header. */
    font_text(text, x - 2.0f, y, px, alpha_color(C_YELLOW, 0.45f));
    font_text(text, x + 2.0f, y, px, alpha_color(C_CYAN, 0.35f));
    font_text(text, x, y, px, C_YELLOW);
}

static void server_line(const UiState *state)
{
    int on = state->server.run != UI_SERVER_OFF;
    const char *word = state->server.run == UI_SERVER_STARTING
                           ? "STARTING"
                           : (on ? "ON" : "OFF");
    float rx = HEADER_RIGHT;

    if (state->server.run == UI_SERVER_ON) {
        char clients[24];
        if (state->server.clients == 1)
            snprintf(clients, sizeof(clients), "1 CLIENT");
        else
            snprintf(clients, sizeof(clients), "%d CLIENTS",
                     state->server.clients);
        float w = font_width(clients, PX_MICRO);
        font_text(clients, rx - w, HEADER_SERVER_Y, PX_MICRO, C_WHITE);
        rx -= w + 12.0f;
    }

    float word_w = font_width(word, PX_MICRO);
    uint32_t word_color = state->server.run == UI_SERVER_STARTING
                              ? C_YELLOW
                              : (on ? C_GREEN : C_DIM);
    font_text(word, rx - word_w, HEADER_SERVER_Y, PX_MICRO, word_color);
    float server_right = rx - word_w - 12.0f;
    font_text_right("SERVER", server_right, HEADER_SERVER_Y, PX_MICRO, C_DIM);
}

void header_draw(const UiState *state)
{
    if (state == NULL)
        return;

    wordmark();
    font_text_tracked(ui_screen_name(state->screen), (float)LAYOUT_MARGIN,
                      HEADER_NAME_Y, PX_LABEL, C_WHITE, 1.0f);

    int connected = state->panes[UI_PANE_REMOTE].connected;
    ui_header_identity(state->site_host, state->site_port, "CONNECTED",
                       connected);
    server_line(state);
}
