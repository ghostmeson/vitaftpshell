#include "gfx/theme.h"

#include <math.h>
#include <stdio.h>

#include "gfx/font.h"

/*
 * Adapted from COCKPIT (MIT). The sparkline and metrics panels are dropped
 * because this app has no history buffer. See docs/THIRD_PARTY.md.
 */

#define PANEL_CHAMFER_HERO 28.0f
#define PANEL_CHAMFER_SATELLITE 14.0f
#define BRACKET_LEN 16.0f
#define BRACKET_THICK 2.0f
#define TITLE_BAR_W 3.0f
#define TITLE_BAR_GAP 9.0f
#define OUTLINE_DIM 0.45f
#define RULE_DIM 0.25f

#define METER_CELLS 10
#define METER_GAP 2.0f
#define METER_ABSENT_DIM 0.7f

#define HAZARD_STRIPE_W 9.0f
#define HAZARD_PERIOD 22.0f

#define SWEEP_PERIOD 5.0f
#define SWEEP_CORE_DIM 0.12f
#define SWEEP_HALO_DIM 0.05f

#define READOUT_BUF 24

static void cut_top_right(Rect r, float c)
{
    draw_tri(r.x + r.w - c, r.y, r.x + r.w, r.y + c, r.x + r.w, r.y, C_FIELD);
}

static void cut_bottom_left(Rect r, float c)
{
    draw_tri(r.x, r.y + r.h - c, r.x + c, r.y + r.h, r.x, r.y + r.h, C_FIELD);
}

void theme_panel(Rect r, const char *title, uint32_t accent,
                 PanelChrome chrome)
{
    int hero = (chrome == CHROME_HERO);
    int inline_title = (chrome == CHROME_STRIP || chrome == CHROME_LIST);
    float c = hero ? PANEL_CHAMFER_HERO : PANEL_CHAMFER_SATELLITE;
    uint32_t outline = draw_dim(accent, OUTLINE_DIM);

    draw_rect(r.x, r.y, r.w, r.h, C_PANEL);

    /*
     * The chamfer is painted rather than assembled as a polygon: the corner is
     * filled with C_PANEL and then cut back to the field. This is only valid
     * because panels never overlap, which is a property of the layout table.
     */
    cut_top_right(r, c);
    if (hero)
        cut_bottom_left(r, c);

    draw_line(r.x, r.y, r.x + r.w - c, r.y, outline);
    draw_line(r.x + r.w - c, r.y, r.x + r.w, r.y + c, outline);
    draw_line(r.x + r.w, r.y + c, r.x + r.w, r.y + r.h, outline);

    if (hero) {
        draw_line(r.x + r.w, r.y + r.h, r.x + c, r.y + r.h, outline);
        draw_line(r.x + c, r.y + r.h, r.x, r.y + r.h - c, outline);
        draw_line(r.x, r.y + r.h - c, r.x, r.y, outline);

        draw_rect(r.x, r.y, BRACKET_LEN, BRACKET_THICK, accent);
        draw_rect(r.x, r.y, BRACKET_THICK, BRACKET_LEN, accent);
        draw_rect(r.x + r.w - BRACKET_LEN, r.y + r.h - BRACKET_THICK,
                  BRACKET_LEN, BRACKET_THICK, accent);
        draw_rect(r.x + r.w - BRACKET_THICK, r.y + r.h - BRACKET_LEN,
                  BRACKET_THICK, BRACKET_LEN, accent);
    } else {
        draw_line(r.x + r.w, r.y + r.h, r.x, r.y + r.h, outline);
        draw_line(r.x, r.y + r.h, r.x, r.y, outline);
    }

    float title_h = LINE_H(PANEL_TITLE_PX);
    float title_y = (chrome == CHROME_STRIP)
                        ? r.y + (r.h - title_h) * 0.5f
                        : r.y + PANEL_TITLE_Y;

    draw_rect(r.x + PANEL_PAD, title_y, TITLE_BAR_W, title_h, accent);
    font_text_tracked(title, r.x + PANEL_PAD + TITLE_BAR_GAP, title_y,
                      PANEL_TITLE_PX, accent, PANEL_TITLE_TRACK);

    if (!inline_title)
        draw_rect(r.x + PANEL_PAD, r.y + PANEL_RULE_Y, r.w - 2.0f * PANEL_PAD,
                  1.0f, draw_dim(accent, RULE_DIM));
}

void theme_readout_px(const char *label, const char *value, int valid, float x,
                      float y, float w, float label_px, float value_px)
{
    font_text(label, x, y + LINE_H(value_px) - LINE_H(label_px), label_px,
              C_DIM);
    font_text_right(value, x + w, y, value_px, valid ? C_WHITE : C_DIM);
}

void theme_readout(const char *label, const char *value, int valid, float x,
                   float y, float w)
{
    theme_readout_px(label, value, valid, x, y, w, PX_LABEL, PX_VALUE);
}

void theme_readout_int(const char *label, int value, int valid, float x,
                       float y, float w)
{
    char buf[READOUT_BUF];
    int ok = theme_fmt_int(buf, sizeof(buf), value, valid);
    theme_readout(label, buf, ok, x, y, w);
}

void theme_meter(float x, float y, float w, float h, float fill, int valid,
                 uint32_t accent)
{
    if (fill < 0.0f)
        fill = 0.0f;
    if (fill > 1.0f)
        fill = 1.0f;

    int lit = valid ? (int)ceilf(fill * (float)METER_CELLS) : 0;
    if (lit > METER_CELLS)
        lit = METER_CELLS;

    uint32_t empty = valid ? C_HAIR : draw_dim(C_HAIR, METER_ABSENT_DIM);
    float cw = (w - (float)(METER_CELLS - 1) * METER_GAP) / (float)METER_CELLS;

    for (int i = 0; i < METER_CELLS; ++i)
        draw_rect(x + (float)i * (cw + METER_GAP), y, cw, h,
                  i < lit ? accent : empty);
}

void theme_hazard(float x, float y, float w, float h)
{
    draw_rect(x, y, w, h, C_FIELD);

    for (float px = x - h; px < x + w + h; px += HAZARD_PERIOD) {
        draw_tri(px, y + h, px + h, y, px + h + HAZARD_STRIPE_W, y, C_YELLOW);
        draw_tri(px, y + h, px + h + HAZARD_STRIPE_W, y, px + HAZARD_STRIPE_W,
                 y + h, C_YELLOW);
    }
}

void theme_sweep(float x, float y, float w, float h, float t)
{
    float p = fmodf(t, SWEEP_PERIOD) / SWEEP_PERIOD;
    if (p < 0.0f)
        p += 1.0f;

    float sx = x + p * w;

    draw_set_additive(1);
    draw_rect(sx - 1.0f, y, 3.0f, h, draw_dim(C_CYAN, SWEEP_HALO_DIM));
    draw_rect(sx, y, 1.0f, h, draw_dim(C_CYAN, SWEEP_CORE_DIM));
    draw_set_additive(0);
}

void theme_fmt_tenths(char *buf, size_t n, int tenths)
{
    if (buf == NULL || n == 0)
        return;

    int v = tenths < 0 ? -tenths : tenths;
    snprintf(buf, n, "%s%d.%d", tenths < 0 ? "-" : "", v / 10, v % 10);
}

int theme_fmt_int(char *buf, size_t n, int value, int valid)
{
    if (buf == NULL || n == 0)
        return 0;

    if (!valid) {
        snprintf(buf, n, "%s", THEME_ABSENT);
        return 0;
    }

    snprintf(buf, n, "%d", value);
    return 1;
}

void theme_fmt_hms(char *buf, size_t n, int total_seconds)
{
    if (buf == NULL || n == 0)
        return;

    if (total_seconds < 0)
        total_seconds = 0;

    snprintf(buf, n, "%02d:%02d:%02d", total_seconds / 3600,
             (total_seconds / 60) % 60, total_seconds % 60);
}
