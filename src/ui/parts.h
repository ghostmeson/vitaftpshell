/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_UI_PARTS_H
#define VFTP_UI_PARTS_H

#include <stddef.h>
#include <stdint.h>

#include "gfx/draw.h"

/*
 * Small shared drawing pieces. They know positions and colours, never state:
 * a screen passes the values it read from UiState. This keeps the seven screen
 * files from each reinventing a keycap or a wash.
 */

/* The settled panel chrome without the generic title: satellites get a 14 px
 * top-right chamfer, heroes a 28 px chamfer plus brackets. */
void ui_panel_frame(Rect r, int hero, uint32_t accent);

/* A 1 px rule inside a panel. */
void ui_hair(float x, float y, float w);

/* A 40 px slab button. `primary` is the yellow commit; `focused` adds the
 * 2 px white outline offset 4. */
void ui_slab(float x, float y, float w, float h, const char *label, int primary,
             int focused, float px);

/* A white wash over a panel; alpha is 0..1. */
void ui_wash(float x, float y, float w, float h, float alpha);

/* Centre a string in [x, x + w]. */
void ui_center(const char *s, float x, float w, float y, float px,
               uint32_t color);

/* A 14 px keycap and its label. `glyph` is "X", "O", "SQUARE", or "TRIANGLE".
 * Returns the x after the label. */
float ui_keycap(const char *glyph, const char *label, float x, float y);

/* A right-aligned legend run, returning the x of its left edge. */
float ui_legend_right(const char *text, float x_right, float y, float px,
                      uint32_t color);

/* Draw wrapped-free micro copy at a fixed stride. */
void ui_lines(const char *const *lines, int count, float x, float y,
              float stride, float px, uint32_t color);

/* The header identity line, shared by every screen. */
void ui_header_identity(const char *host, int port, const char *state,
                        int connected);

#endif
