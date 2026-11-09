/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Portions adapted from COCKPIT, Copyright (c) 2026 Upakul Talukdar,
 * licensed under the MIT License. See LICENSES/COCKPIT-MIT.txt.
 */

#include "gfx/draw.h"

#include <math.h>
#include <stddef.h>

#include <vita2d.h>

/*
 * Adapted from COCKPIT (MIT). This is the only file that includes vita2d.h.
 * See docs/THIRD_PARTY.md.
 */

int draw_init(void)
{
    vita2d_init();
    vita2d_set_clear_color(0xFF000000u); /* C_FIELD; theme.h includes us. */
    return 1;
}

void draw_shutdown(void)
{
    vita2d_fini();
}

void draw_begin_frame(void)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
}

void draw_present(void)
{
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

uint32_t draw_dim(uint32_t c, float f)
{
    if (f <= 0.0f)
        return 0x00000000u;
    if (f > 1.0f)
        f = 1.0f;

    uint32_t r = (uint32_t)((float)(c & 0xFF) * f);
    uint32_t g = (uint32_t)((float)((c >> 8) & 0xFF) * f);
    uint32_t b = (uint32_t)((float)((c >> 16) & 0xFF) * f);
    uint32_t a = (c >> 24) & 0xFF;

    return (a << 24) | (b << 16) | (g << 8) | r;
}

void draw_rect(float x, float y, float w, float h, uint32_t c)
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    vita2d_draw_rectangle(x, y, w, h, c);
}

void draw_rects(const Rect *rects, int count, uint32_t c)
{
    if (rects == NULL || count <= 0)
        return;

    for (int i = 0; i < count; ++i)
        draw_rect(rects[i].x, rects[i].y, rects[i].w, rects[i].h, c);
}

void draw_line(float x0, float y0, float x1, float y1, uint32_t c)
{
    vita2d_draw_line(x0, y0, x1, y1, c);
}

void draw_tri(float x0, float y0, float x1, float y1, float x2, float y2,
              uint32_t c)
{
    const float px[3] = {x0, x1, x2};
    const float py[3] = {y0, y1, y2};

    float min_y = y0 < y1 ? y0 : y1;
    if (y2 < min_y)
        min_y = y2;

    float max_y = y0 > y1 ? y0 : y1;
    if (y2 > max_y)
        max_y = y2;

    int y_start = (int)floorf(min_y);
    int y_end = (int)ceilf(max_y);

    for (int y = y_start; y < y_end; ++y) {
        float sy = (float)y + 0.5f;
        float lo = 0.0f;
        float hi = 0.0f;
        int hits = 0;

        for (int e = 0; e < 3; ++e) {
            float ax = px[e];
            float ay = py[e];
            float bx = px[(e + 1) % 3];
            float by = py[(e + 1) % 3];

            if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                float t = (sy - ay) / (by - ay);
                float x = ax + t * (bx - ax);

                if (hits == 0) {
                    lo = x;
                    hi = x;
                } else {
                    if (x < lo)
                        lo = x;
                    if (x > hi)
                        hi = x;
                }
                ++hits;
            }
        }

        if (hi > lo)
            draw_rect(lo, (float)y, hi - lo, 1.0f, c);
    }
}

void draw_set_additive(int on)
{
    vita2d_set_blend_mode_add(on ? 1 : 0);
}

void draw_set_clip(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        draw_clear_clip();
        return;
    }

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, y, x + w, y + h);
}

void draw_clear_clip(void)
{
    vita2d_disable_clipping();
}
