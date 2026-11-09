#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vita2d.h"

/*
 * Software stand-in for vita2d, used only by tools/preview. Adapted from
 * COCKPIT (MIT); see docs/THIRD_PARTY.md.
 *
 * It implements the calls gfx/draw.c makes, with the same semantics: colours
 * are ABGR, rectangles and lines replace under alpha blending, and additive
 * mode adds instead. It is not a GXM emulator; it exists so the layout and type
 * can be looked at without a console.
 */

#define FB_W 960
#define FB_H 544

static uint32_t s_fb[FB_W * FB_H];
static int s_additive;
static int s_draw_calls;
static int s_clip_on;
static int s_clip_x0, s_clip_y0, s_clip_x1, s_clip_y1;

static uint8_t clamp_u8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

static void put(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H)
        return;
    if (s_clip_on && (x < s_clip_x0 || x > s_clip_x1 || y < s_clip_y0 ||
                      y > s_clip_y1))
        return;

    uint32_t *p = &s_fb[y * FB_W + x];
    int sr = (int)(c & 0xFF);
    int sg = (int)((c >> 8) & 0xFF);
    int sb = (int)((c >> 16) & 0xFF);
    int sa = (int)((c >> 24) & 0xFF);
    int dr = (int)(*p & 0xFF);
    int dg = (int)((*p >> 8) & 0xFF);
    int db = (int)((*p >> 16) & 0xFF);

    if (s_additive) {
        dr = clamp_u8(dr + sr * sa / 255);
        dg = clamp_u8(dg + sg * sa / 255);
        db = clamp_u8(db + sb * sa / 255);
    } else {
        dr = clamp_u8((sr * sa + dr * (255 - sa)) / 255);
        dg = clamp_u8((sg * sa + dg * (255 - sa)) / 255);
        db = clamp_u8((sb * sa + db * (255 - sa)) / 255);
    }

    *p = 0xFF000000u | ((uint32_t)db << 16) | ((uint32_t)dg << 8) | (uint32_t)dr;
}

void vita2d_draw_rectangle(float x, float y, float w, float h,
                           unsigned int color)
{
    ++s_draw_calls;
    int x0 = (int)lroundf(x);
    int y0 = (int)lroundf(y);
    int x1 = (int)lroundf(x + w);
    int y1 = (int)lroundf(y + h);

    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            put(xx, yy, color);
}

void vita2d_draw_line(float x0f, float y0f, float x1f, float y1f,
                      unsigned int color)
{
    ++s_draw_calls;
    int x0 = (int)lroundf(x0f);
    int y0 = (int)lroundf(y0f);
    int x1 = (int)lroundf(x1f);
    int y1 = (int)lroundf(y1f);
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        put(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void vita2d_set_blend_mode_add(int enable)
{
    s_additive = enable ? 1 : 0;
}

void vita2d_enable_clipping(void)
{
    s_clip_on = 1;
}

void vita2d_disable_clipping(void)
{
    s_clip_on = 0;
}

void vita2d_set_clip_rectangle(int x_min, int y_min, int x_max, int y_max)
{
    s_clip_x0 = x_min;
    s_clip_y0 = y_min;
    s_clip_x1 = x_max;
    s_clip_y1 = y_max;
}

void preview_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
    s_additive = 0;
    s_draw_calls = 0;
    s_clip_on = 0;
}

int preview_draw_calls(void)
{
    return s_draw_calls;
}

const uint32_t *preview_pixels(void)
{
    return s_fb;
}

int preview_width(void)
{
    return FB_W;
}

int preview_height(void)
{
    return FB_H;
}
