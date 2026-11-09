/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Portions adapted from COCKPIT, Copyright (c) 2026 Upakul Talukdar,
 * licensed under the MIT License. See LICENSES/COCKPIT-MIT.txt.
 */

#ifndef VFTP_GFX_DRAW_H
#define VFTP_GFX_DRAW_H

#include <stdint.h>

/*
 * Renderer primitives. Adapted from COCKPIT (MIT); see docs/THIRD_PARTY.md.
 *
 * Rectangles, lines, triangles, additive blending, and clipping. The renderer
 * uses exactly two GPU primitives, vita2d_draw_rectangle and vita2d_draw_line;
 * batching through vita2d_draw_array produced nothing on hardware (COCKPIT D31).
 */

typedef struct {
    float x, y, w, h;
} Rect;

/*
 * Frame lifetime. These are the only vita2d calls outside the primitives, so
 * the composition root never names vita2d and the host shim can stand in.
 */
int draw_init(void);
void draw_shutdown(void);
void draw_begin_frame(void);
void draw_present(void);

uint32_t draw_dim(uint32_t c, float f);

void draw_rect(float x, float y, float w, float h, uint32_t c);
void draw_rects(const Rect *rects, int count, uint32_t c);
void draw_line(float x0, float y0, float x1, float y1, uint32_t c);
void draw_tri(float x0, float y0, float x1, float y1, float x2, float y2,
              uint32_t c);

void draw_set_additive(int on);

void draw_set_clip(int x, int y, int w, int h);
void draw_clear_clip(void);

#endif
