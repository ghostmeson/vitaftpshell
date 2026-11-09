#ifndef VFTP_GFX_THEME_H
#define VFTP_GFX_THEME_H

#include <stddef.h>
#include <stdint.h>

#include "gfx/draw.h"
#include "gfx/font.h"

/*
 * The visual vocabulary: palette, panel chrome, readouts, meters. Adapted from
 * COCKPIT (MIT); see docs/THIRD_PARTY.md.
 *
 * Nothing here decides what is shown, only how a given thing looks. A panel
 * that hard-codes a colour or a corner size is a bug: change the theme instead.
 * The values come from docs/DESIGN.md and nowhere else.
 */

/* vita2d packs colour as ABGR in a uint32; red is the low byte. */
#define THEME_RGBA8(r, g, b, a)                                      \
    ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | \
     (uint32_t)(r))

#define C_FIELD THEME_RGBA8(0x00, 0x00, 0x00, 0xFF)
#define C_PANEL THEME_RGBA8(0x0D, 0x0E, 0x12, 0xFF)
#define C_HAIR THEME_RGBA8(0x1C, 0x1D, 0x24, 0xFF)
#define C_DIM THEME_RGBA8(0x6E, 0x70, 0x7E, 0xFF)
#define C_WHITE THEME_RGBA8(0xED, 0xED, 0xF0, 0xFF)
#define C_YELLOW THEME_RGBA8(0xFC, 0xEE, 0x0A, 0xFF)
#define C_CYAN THEME_RGBA8(0x2A, 0xD8, 0xE8, 0xFF)
#define C_GREEN THEME_RGBA8(0x3B, 0xE0, 0x7A, 0xFF)
#define C_GREEN_DIM THEME_RGBA8(0x2F, 0xA8, 0x5C, 0xFF)
#define C_RED THEME_RGBA8(0xFF, 0x2E, 0x4D, 0xFF)
#define C_CYAN_DIM THEME_RGBA8(0x19, 0x81, 0x8B, 0xFF)

typedef enum {
    CHROME_HERO = 0,
    CHROME_SATELLITE,
    CHROME_STRIP,
    CHROME_LIST
} PanelChrome;

#define PANEL_PAD 14.0f
#define PANEL_INSET 34.0f
#define PANEL_TITLE_PX 2.0f
#define PANEL_TITLE_TRACK 2.0f
#define PANEL_TITLE_Y 11.0f
#define PANEL_RULE_Y 36.0f
#define PANEL_BODY_Y (PANEL_RULE_Y + SP_4)
#define STRIP_BODY_X 110.0f

/* Font scale ladder. No size between these. */
#define PX_MICRO 1.5f
#define PX_LABEL 2.0f
#define PX_VALUE 3.0f
#define PX_HERO 4.0f
#define PX_PRIMARY 5.0f

#define LINE_H(px) ((px) * (float)FONT_GLYPH_H)

/* Spacing scale: 4 px base, dividing the 12 px gutter and 18 px margin. */
#define SP_1 4.0f
#define SP_2 8.0f
#define SP_3 12.0f
#define SP_4 16.0f
#define SP_5 24.0f
#define SP_6 32.0f

#define READOUT_STRIDE (LINE_H(PX_VALUE) + SP_4)
#define THEME_ABSENT "--"

void theme_panel(Rect r, const char *title, uint32_t accent,
                 PanelChrome chrome);
void theme_readout(const char *label, const char *value, int valid, float x,
                   float y, float w);
void theme_readout_px(const char *label, const char *value, int valid, float x,
                      float y, float w, float label_px, float value_px);
void theme_readout_int(const char *label, int value, int valid, float x,
                       float y, float w);
void theme_meter(float x, float y, float w, float h, float fill, int valid,
                 uint32_t accent);
void theme_hazard(float x, float y, float w, float h);
void theme_sweep(float x, float y, float w, float h, float t);

void theme_fmt_tenths(char *buf, size_t n, int tenths);
int theme_fmt_int(char *buf, size_t n, int value, int valid);
void theme_fmt_hms(char *buf, size_t n, int total_seconds);

#endif
