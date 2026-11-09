/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Portions adapted from COCKPIT, Copyright (c) 2026 Upakul Talukdar,
 * licensed under the MIT License. See LICENSES/COCKPIT-MIT.txt.
 */

#ifndef VFTP_GFX_FONT_H
#define VFTP_GFX_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "gfx/draw.h"

/*
 * The hand-built 5x7 bitmap face. Adapted from COCKPIT (MIT); see
 * docs/THIRD_PARTY.md.
 *
 * A glyph is 5*px wide and 7*px tall; it advances 6*px. The face has no `?` or
 * `!`: a UI title is a statement (`DELETE 3 ITEMS`, `DISCARD CHANGES`).
 */

#define FONT_GLYPH_W 5
#define FONT_GLYPH_H 7
#define FONT_ADVANCE 6
#define FONT_MAX_RUNS 24

void font_text(const char *s, float x, float y, float px, uint32_t c);
void font_text_tracked(const char *s, float x, float y, float px, uint32_t c,
                       float extra);
void font_text_right(const char *s, float x_right, float y, float px,
                     uint32_t c);

float font_width(const char *s, float px);

int font_rects(const char *s, float x, float y, float px, Rect *out, int max);

/*
 * Host-test trace hook. The render suites assert that a required label was
 * drawn at the settled position and colour; the glyph rectangles alone cannot
 * name the string. The app leaves this NULL, so the hook costs one branch.
 */
typedef void (*FontTraceFn)(const char *text, float x, float y, float px,
                            uint32_t color);
void font_set_trace(FontTraceFn fn);

/*
 * Copy the longest prefix of `s` that fits `max_w` at `px`, appending `..` when
 * it was truncated. The result never exceeds `max_w`; a glyph the face does not
 * have advances without drawing.
 */
void font_fit(const char *s, float px, float max_w, char *out, size_t out_n);

#endif
