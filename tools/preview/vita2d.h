/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef PREVIEW_VITA2D_H
#define PREVIEW_VITA2D_H

/*
 * Host stand-in for the vita2d entry points gfx/draw.c calls. The preview
 * rasteriser implements these into a plain 960x544 ABGR buffer.
 */

void vita2d_draw_rectangle(float x, float y, float w, float h,
                           unsigned int color);
void vita2d_draw_line(float x0, float y0, float x1, float y1,
                      unsigned int color);
void vita2d_set_blend_mode_add(int enable);

void vita2d_enable_clipping(void);
void vita2d_disable_clipping(void);
void vita2d_set_clip_rectangle(int x_min, int y_min, int x_max, int y_max);

/* Preview-only accessors. */
void preview_clear(void);
const unsigned int *preview_pixels(void);
int preview_width(void);
int preview_height(void);
int preview_draw_calls(void);

#endif
