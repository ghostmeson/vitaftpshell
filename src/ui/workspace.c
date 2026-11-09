/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/workspace.h"

#include <stdio.h>
#include <string.h>

#include "core/fmt.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/dock.h"
#include "ui/parts.h"

#define PANE_Y 76.0f
#define PANE_TITLE_Y 8.0f
#define PANE_PATH_Y 26.0f
#define PANE_RULE_Y 43.0f
#define PANE_ROWS_Y 48.0f
#define PANE_INSET 14.0f
#define PANE_CHAMFER 14.0f
#define ROW_MARK_Y 12.0f

static void pane_frame(const LayoutRect *r, int active)
{
    draw_rect((float)r->x, (float)r->y, (float)r->w, (float)r->h, C_PANEL);
    draw_tri((float)(r->x + r->w) - PANE_CHAMFER, (float)r->y, (float)(r->x + r->w),
             (float)r->y + PANE_CHAMFER, (float)(r->x + r->w), (float)r->y,
             C_FIELD);

    uint32_t outline = draw_dim(C_CYAN, 0.45f);
    draw_line((float)r->x, (float)r->y,
              (float)(r->x + r->w) - PANE_CHAMFER, (float)r->y, outline);
    draw_line((float)(r->x + r->w) - PANE_CHAMFER, (float)r->y,
              (float)(r->x + r->w), (float)r->y + PANE_CHAMFER, outline);
    draw_line((float)(r->x + r->w), (float)r->y + PANE_CHAMFER,
              (float)(r->x + r->w), (float)(r->y + r->h), outline);
    draw_line((float)(r->x + r->w), (float)(r->y + r->h), (float)r->x,
              (float)(r->y + r->h), outline);
    draw_line((float)r->x, (float)(r->y + r->h), (float)r->x, (float)r->y,
              outline);

    if (active) {
        draw_rect((float)r->x, (float)r->y, 2.0f, (float)r->h, C_YELLOW);
    } else {
        ui_wash((float)r->x + 2.0f, (float)r->y + PANE_RULE_Y + 1.0f,
                (float)r->w - 4.0f, (float)r->h - PANE_RULE_Y - 1.0f, 0.025f);
    }
}

static int pane_risk(const UiRow *row)
{
    return row->risk != 0;
}

static int row_path(UiPane pane, const UiPaneState *pane_state,
                    const char *name, char *out, size_t cap)
{
    if (pane == UI_PANE_LOCAL) {
        Path joined;
        if (path_join(pane_state->path, name, &joined) != PATH_OK)
            return 0;
        int n = snprintf(out, cap, "%s", joined.text);
        return n > 0 && (size_t)n < cap;
    }

    size_t len = strlen(pane_state->path);
    int slash = len > 0 && pane_state->path[len - 1] == '/';
    int n;
    if (strcmp(pane_state->path, "/") == 0)
        n = snprintf(out, cap, "/%s", name);
    else if (slash)
        n = snprintf(out, cap, "%s%s", pane_state->path, name);
    else
        n = snprintf(out, cap, "%s/%s", pane_state->path, name);
    return n > 0 && (size_t)n < cap;
}

static void risk_diamond(float x, float y)
{
    draw_tri(x + 3.0f, y, x + 6.0f, y + 3.0f, x, y + 3.0f, C_YELLOW);
    draw_tri(x, y + 3.0f, x + 6.0f, y + 3.0f, x + 3.0f, y + 6.0f, C_YELLOW);
}

static void band(const UiState *state, UiPane pane, const LayoutRect *r)
{
    const UiPaneState *p = &state->panes[pane];
    int active = state->active_pane == pane;
    uint32_t title_color = active ? C_YELLOW : C_DIM;
    uint32_t path_color = active ? C_CYAN : C_CYAN_DIM;

    if (p->session_lost)
        path_color = C_DIM;

    const char *title = pane == UI_PANE_LOCAL ? "LOCAL" : "REMOTE";
    font_text_tracked(title, (float)r->x + PANE_INSET,
                      (float)r->y + PANE_TITLE_Y, PX_LABEL, title_color, 2.0f);

    float right = (float)(r->x + r->w) - PANE_INSET;
    float mark_left = right;

    if (p->mark_count > 0) {
        char size[32];
        char count[32];
        fmt_size(size, sizeof(size), p->marked_bytes, p->marked_bytes_known);
        snprintf(count, sizeof(count), "%d MARKED", p->mark_count);
        font_text_right(size, right, (float)r->y + 11.5f, PX_MICRO, C_WHITE);
        float size_w = font_width(size, PX_MICRO);
        font_text_right(count, right - size_w - 12.0f, (float)r->y + 11.5f,
                        PX_MICRO, C_YELLOW);
        mark_left = right - size_w - 12.0f - font_width(count, PX_MICRO);
    }

    if (p->truncated) {
        float first_x = mark_left - 12.0f - font_width("FIRST 2000", PX_MICRO);
        if (p->mark_count == 0)
            first_x = right - font_width("FIRST 2000", PX_MICRO);
        font_text("FIRST 2000", first_x, (float)r->y + 11.5f, PX_MICRO,
                  C_YELLOW);
    }

    if (pane == UI_PANE_LOCAL) {
        font_text(p->path, (float)r->x + PANE_INSET,
                  (float)r->y + PANE_PATH_Y, PX_MICRO, path_color);
        if (p->free_known) {
            char free_text[32];
            snprintf(free_text, sizeof(free_text), "%s", p->free_text);
            font_text_right(free_text, right, (float)r->y + PANE_PATH_Y,
                            PX_MICRO, C_WHITE);
            float fw = font_width(free_text, PX_MICRO);
            font_text_right("FREE", right - fw - 8.0f,
                            (float)r->y + PANE_PATH_Y, PX_MICRO, C_DIM);
        }
    } else {
        if (p->site[0] != '\0') {
            font_text(p->site, (float)r->x + PANE_INSET,
                      (float)r->y + PANE_PATH_Y, PX_MICRO, C_DIM);
            float sx = (float)r->x + PANE_INSET +
                       font_width(p->site, PX_MICRO) + 8.0f;
            font_text(p->path, sx, (float)r->y + PANE_PATH_Y, PX_MICRO,
                      path_color);
        } else {
            font_text(p->path, (float)r->x + PANE_INSET,
                      (float)r->y + PANE_PATH_Y, PX_MICRO, path_color);
        }
    }

    draw_rect((float)r->x + PANE_INSET, (float)r->y + PANE_RULE_Y,
              (float)r->w - 2.0f * PANE_INSET, 1.0f, C_HAIR);
}

static void row_draw(const UiState *state, UiPane pane, const LayoutRect *r,
                     int index)
{
    const UiPaneState *p = &state->panes[pane];
    const UiRow *row = &p->rows[index];
    int active = state->active_pane == pane;
    int selected = index == p->selected;
    int marked = 0;
    char full[PATH_MAX_LEN];

    if (row->type != UI_ROW_UP &&
        row_path(pane, p, row->name, full, sizeof(full)) &&
        ui_state_is_marked(state, pane, full))
        marked = 1;

    float row_y = (float)r->y + PANE_ROWS_Y +
                  (float)(index - p->scroll) * (float)LAYOUT_ROW_H;

    if (selected) {
        ui_wash((float)r->x + 1.0f, row_y, (float)r->w - 2.0f,
                (float)LAYOUT_ROW_H, active ? 0.06f : 0.025f);
        draw_rect((float)r->x + LAYOUT_ROW_CURSOR_X, row_y, 2.0f,
                  (float)LAYOUT_ROW_H, active ? C_WHITE : C_HAIR);
    }

    if (marked)
        draw_rect((float)r->x + LAYOUT_ROW_MARK_X, row_y + ROW_MARK_Y, 6.0f,
                  6.0f, C_YELLOW);
    if (pane_risk(row))
        risk_diamond((float)r->x + LAYOUT_ROW_RISK_X, row_y + ROW_MARK_Y);

    float value_right = (float)(r->x + r->w) - PANE_INSET;
    char value[32];
    uint32_t value_color = C_DIM;
    if (row->type == UI_ROW_UP) {
        snprintf(value, sizeof(value), "UP");
    } else if (row->type == UI_ROW_DIR) {
        snprintf(value, sizeof(value), "DIR");
    } else {
        fmt_size(value, sizeof(value), row->size, row->size_known);
        value_color = row->size_known ? C_WHITE : C_DIM;
    }

    float name_x = (float)r->x + LAYOUT_ROW_NAME_X;
    float name_max = value_right - font_width(value, PX_MICRO) - 12.0f -
                     name_x;
    if (name_max < 24.0f)
        name_max = 24.0f;
    char fitted[PATH_NAME_MAX + 4];
    font_fit(row->name, PX_LABEL, name_max, fitted, sizeof(fitted));

    uint32_t name_color = row->type == UI_ROW_UP ? C_DIM : C_WHITE;
    font_text(fitted, name_x, row_y + 8.0f, PX_LABEL, name_color);
    font_text_right(value, value_right, row_y + 11.5f, PX_MICRO, value_color);
}

static void rows_draw(const UiState *state, UiPane pane, const LayoutRect *r)
{
    const UiPaneState *p = &state->panes[pane];

    if (p->rows == NULL || p->row_count <= 0)
        return;

    int shown = 0;
    for (int i = p->scroll;
         i < p->row_count && shown < LAYOUT_ROWS_VISIBLE; ++i, ++shown)
        row_draw(state, pane, r, i);

    if (p->row_count > LAYOUT_ROWS_VISIBLE) {
        float track_y = (float)r->y + PANE_ROWS_Y;
        float track_h =
            (float)LAYOUT_ROWS_VISIBLE * (float)LAYOUT_ROW_H;
        float thumb = track_h * (float)LAYOUT_ROWS_VISIBLE /
                      (float)p->row_count;
        if (thumb < 24.0f)
            thumb = 24.0f;
        float max_scroll = (float)(p->row_count - LAYOUT_ROWS_VISIBLE);
        float pos = (track_h - thumb) * (float)p->scroll /
                    (max_scroll > 0.0f ? max_scroll : 1.0f);
        draw_rect((float)(r->x + r->w) - 6.0f, track_y, 2.0f, track_h,
                  C_HAIR);
        draw_rect((float)(r->x + r->w) - 6.0f, track_y + pos, 2.0f, thumb,
                  C_DIM);
    }
}

static void centered_state(UiPane pane, const LayoutRect *r)
{
    (void)pane;
    float x = (float)r->x;
    float w = (float)r->w;
    float top = (float)r->y + PANE_ROWS_Y;
    float bottom = (float)(r->y + r->h);
    float y = top + (bottom - top) * 0.5f - 20.0f;

    ui_center("SESSION LOST", x, w, y, PX_LABEL, C_YELLOW);
    ui_center("THE SERVER CLOSED THE CONTROL CHANNEL", x, w, y + 22.0f,
              PX_MICRO, C_DIM);

    const char *keys = "X RECONNECT  O DISMISS";
    float kw = font_width(keys, PX_MICRO);
    float kx = x + (w - kw) * 0.5f;
    font_text(keys, kx, y + 44.0f, PX_MICRO, C_DIM);
    font_text("X", kx, y + 44.0f, PX_MICRO, C_WHITE);
    font_text("O", kx + font_width("X RECONNECT  ", PX_MICRO), y + 44.0f,
              PX_MICRO, C_WHITE);
}

static void not_connected(const LayoutRect *r)
{
    float x = (float)r->x;
    float w = (float)r->w;
    float y = (float)r->y + 150.0f;
    ui_center("NOT CONNECTED", x, w, y, PX_LABEL, C_DIM);
    ui_center("TRIANGLE SITES TO CONNECT", x, w, y + 24.0f, PX_MICRO,
              C_DIM);
}

static void pane_draw(const UiState *state, UiPane pane, const LayoutRect *r)
{
    const UiPaneState *p = &state->panes[pane];
    int active = state->active_pane == pane;

    pane_frame(r, active);
    band(state, pane, r);

    if (p->session_lost) {
        centered_state(pane, r);
    } else if (p->loading) {
        ui_center("READING", (float)r->x, (float)r->w,
                  (float)r->y + 180.0f, PX_LABEL, C_WHITE);
    } else if (pane == UI_PANE_REMOTE && !p->connected) {
        not_connected(r);
    } else if (p->empty) {
        ui_center("EMPTY FOLDER", (float)r->x, (float)r->w,
                  (float)r->y + 180.0f, PX_LABEL, C_DIM);
    } else {
        rows_draw(state, pane, r);
    }
}

static void legend_standard(void)
{
    float x = (float)LAYOUT_MARGIN;
    x = ui_keycap("X", "OPEN/SEND", x, 510.0f);
    x = ui_keycap("O", "UP", x, 510.0f);
    x = ui_keycap("SQUARE", "MARK", x, 510.0f);
    x = ui_keycap("TRIANGLE", "SITES", x, 510.0f);
    font_text("L/R", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("L/R", PX_MICRO) + 6.0f;
    font_text("PANE", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("PANE", PX_MICRO) + 20.0f;
    font_text("START", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("START", PX_MICRO) + 6.0f;
    font_text("SERVER", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("SERVER", PX_MICRO) + 20.0f;
    font_text("SELECT", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("SELECT", PX_MICRO) + 6.0f;
    font_text("ACTIONS", x, 512.0f, PX_MICRO, C_DIM);
}

static void legend_reconnect(void)
{
    float x = (float)LAYOUT_MARGIN;
    x = ui_keycap("X", "RECONNECT", x, 510.0f);
    x = ui_keycap("O", "DISMISS", x, 510.0f);
    font_text("L/R", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("L/R", PX_MICRO) + 6.0f;
    font_text("PANE", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("PANE", PX_MICRO) + 20.0f;
    font_text("START", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("START", PX_MICRO) + 6.0f;
    font_text("SERVER", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("SERVER", PX_MICRO) + 20.0f;
    font_text("SELECT", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("SELECT", PX_MICRO) + 6.0f;
    font_text("ACTIONS", x, 512.0f, PX_MICRO, C_DIM);
}

void workspace_draw(const UiState *state)
{
    if (state == NULL)
        return;

    unsigned count = 0;
    const LayoutRect *r = layout_regions(LAYOUT_WORKSPACE, &count);
    if (count < 5)
        return;

    pane_draw(state, UI_PANE_LOCAL, &r[1]);
    pane_draw(state, UI_PANE_REMOTE, &r[2]);

    dock_draw(state);

    if (state->panes[state->active_pane].session_lost)
        legend_reconnect();
    else
        legend_standard();
}
