/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/parts.h"

#include <stdio.h>
#include <string.h>

#include "gfx/font.h"
#include "gfx/theme.h"

void ui_panel_frame(Rect r, int hero, uint32_t accent)
{
    float c = hero ? 28.0f : 14.0f;
    uint32_t outline = draw_dim(accent, 0.45f);

    draw_rect(r.x, r.y, r.w, r.h, C_PANEL);

    draw_tri(r.x + r.w - c, r.y, r.x + r.w, r.y + c, r.x + r.w, r.y, C_FIELD);
    if (hero)
        draw_tri(r.x, r.y + r.h - c, r.x + c, r.y + r.h, r.x, r.y + r.h,
                 C_FIELD);

    draw_line(r.x, r.y, r.x + r.w - c, r.y, outline);
    draw_line(r.x + r.w - c, r.y, r.x + r.w, r.y + c, outline);
    draw_line(r.x + r.w, r.y + c, r.x + r.w, r.y + r.h, outline);

    if (hero) {
        draw_line(r.x + r.w, r.y + r.h, r.x + c, r.y + r.h, outline);
        draw_line(r.x + c, r.y + r.h, r.x, r.y + r.h - c, outline);
        draw_line(r.x, r.y + r.h - c, r.x, r.y, outline);

        draw_rect(r.x, r.y, 16.0f, 2.0f, accent);
        draw_rect(r.x, r.y, 2.0f, 16.0f, accent);
        draw_rect(r.x + r.w - 16.0f, r.y + r.h - 2.0f, 16.0f, 2.0f, accent);
        draw_rect(r.x + r.w - 2.0f, r.y + r.h - 16.0f, 2.0f, 16.0f, accent);
    } else {
        draw_line(r.x + r.w, r.y + r.h, r.x, r.y + r.h, outline);
        draw_line(r.x, r.y + r.h, r.x, r.y, outline);
    }
}

void ui_hair(float x, float y, float w)
{
    draw_rect(x, y, w, 1.0f, C_HAIR);
}

void ui_slab(float x, float y, float w, float h, const char *label, int primary,
             int focused, float px)
{
    uint32_t fill = primary ? C_YELLOW : 0xFF1A1514u; /* #14151A in ABGR */
    uint32_t text_color = primary ? C_FIELD : C_WHITE;

    if (focused) {
        draw_line(x - 4.0f, y - 4.0f, x + w + 4.0f, y - 4.0f, C_WHITE);
        draw_line(x + w + 4.0f, y - 4.0f, x + w + 4.0f, y + h + 4.0f, C_WHITE);
        draw_line(x + w + 4.0f, y + h + 4.0f, x - 4.0f, y + h + 4.0f, C_WHITE);
        draw_line(x - 4.0f, y + h + 4.0f, x - 4.0f, y - 4.0f, C_WHITE);
    }

    draw_rect(x, y, w, h, fill);
    draw_rect(x, y, w, 1.0f,
              primary ? THEME_RGBA8(0xFF, 0xF2, 0x7A, 0xFF) : C_HAIR);
    draw_rect(x, y + h - 3.0f, w, 3.0f,
              primary ? THEME_RGBA8(0xC9, 0xBD, 0x08, 0xFF) : C_FIELD);

    ui_center(label, x, w, y + (h - 7.0f * px) * 0.5f, px, text_color);
}

void ui_wash(float x, float y, float w, float h, float alpha)
{
    if (alpha <= 0.0f || w <= 0.0f || h <= 0.0f)
        return;
    if (alpha > 1.0f)
        alpha = 1.0f;
    uint32_t a = (uint32_t)(alpha * 255.0f + 0.5f);
    draw_rect(x, y, w, h, (a << 24) | 0x00FFFFFFu);
}

void ui_center(const char *s, float x, float w, float y, float px,
               uint32_t color)
{
    if (s == NULL)
        return;
    font_text(s, x + (w - font_width(s, px)) * 0.5f, y, px, color);
}

static void keycap_shape(const char *glyph, float x, float y)
{
    if (glyph == NULL)
        return;

    if (strcmp(glyph, "SQUARE") == 0) {
        draw_rect(x + 4.0f, y + 4.0f, 6.0f, 6.0f, C_WHITE);
    } else if (strcmp(glyph, "TRIANGLE") == 0) {
        draw_tri(x + 7.0f, y + 3.0f, x + 11.0f, y + 10.0f, x + 3.0f, y + 10.0f,
                 C_WHITE);
    } else {
        font_text(glyph, x + 4.5f, y + 3.5f, 1.0f, C_WHITE);
    }
}

float ui_keycap(const char *glyph, const char *label, float x, float y)
{
    draw_rect(x, y, 14.0f, 14.0f, C_HAIR);
    draw_rect(x + 1.0f, y + 1.0f, 12.0f, 12.0f, C_FIELD);
    keycap_shape(glyph, x, y);

    float label_x = x + 22.0f;
    font_text(label, label_x, y + 2.0f, PX_MICRO, C_DIM);
    return label_x + font_width(label, PX_MICRO) + 18.0f;
}

float ui_legend_right(const char *text, float x_right, float y, float px,
                      uint32_t color)
{
    float w = font_width(text, px);
    font_text(text, x_right - w, y, px, color);
    return x_right - w;
}

void ui_lines(const char *const *lines, int count, float x, float y,
              float stride, float px, uint32_t color)
{
    if (lines == NULL)
        return;
    for (int i = 0; i < count; ++i) {
        if (lines[i] != NULL)
            font_text(lines[i], x, y + (float)i * stride, px, color);
    }
}

void ui_header_identity(const char *host, int port, const char *state,
                        int connected)
{
    const char *label = connected ? state : "NOT CONNECTED";
    if (label == NULL || label[0] == '\0')
        label = connected ? "CONNECTED" : "NOT CONNECTED";

    float state_w = font_width(label, PX_MICRO);
    float state_x = 942.0f - state_w;
    font_text(label, state_x, 25.5f, PX_MICRO, connected ? C_GREEN : C_DIM);

    if (!connected || host == NULL || host[0] == '\0')
        return;

    char port_text[8];
    snprintf(port_text, sizeof(port_text), ":%d", port);
    float port_w = font_width(port_text, PX_MICRO);
    float port_x = state_x - 12.0f - port_w;
    font_text(port_text, port_x, 25.5f, PX_MICRO, C_CYAN_DIM);

    float host_w = font_width(host, PX_LABEL);
    float host_x = port_x - host_w;
    if (host_x < 600.0f)
        host_x = 600.0f;
    font_text(host, host_x, 22.0f, PX_LABEL, C_CYAN);
}
