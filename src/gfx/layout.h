/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_GFX_LAYOUT_H
#define VFTP_GFX_LAYOUT_H

/*
 * The settled geometry from docs/DESIGN.md. Panels and screens read these
 * constants; they do not invent a coordinate. Any change here is a design
 * change and must land in docs/DESIGN.md first.
 */

#define LAYOUT_SCREEN_W 960
#define LAYOUT_SCREEN_H 544

#define LAYOUT_MARGIN 18
#define LAYOUT_GUTTER 12
#define LAYOUT_DOCK_GAP 12
#define LAYOUT_LEGEND_GAP 8

#define LAYOUT_HEADER_H 64
#define LAYOUT_PANE_W 456
#define LAYOUT_PANE_H 352
#define LAYOUT_DOCK_H 64
#define LAYOUT_LEGEND_H 16
#define LAYOUT_DOCK_Y 440
#define LAYOUT_LEGEND_Y 510

#define LAYOUT_ROW_H 30
#define LAYOUT_ROWS_VISIBLE 10
#define LAYOUT_ROW_NAME_X 40
#define LAYOUT_ROW_VALUE_INSET 14
#define LAYOUT_ROW_MARK_X 19
#define LAYOUT_ROW_RISK_X 27
#define LAYOUT_ROW_CURSOR_X 8
#define LAYOUT_ROW_SCROLL_X 6

typedef struct {
    int x, y, w, h;
} LayoutRect;

typedef enum {
    LAYOUT_WORKSPACE = 0,
    LAYOUT_SITES,
    LAYOUT_SITE_EDITOR,
    LAYOUT_SERVER,
    LAYOUT_ACTIONS,
    LAYOUT_CONFIRM,
    LAYOUT_TEXT_ENTRY,
    LAYOUT_SURFACE_COUNT
} LayoutSurface;

/* Returns the surface's settled regions, or NULL with count 0. */
const LayoutRect *layout_regions(LayoutSurface surface, unsigned *count);
const char *layout_surface_name(LayoutSurface surface);

#endif
