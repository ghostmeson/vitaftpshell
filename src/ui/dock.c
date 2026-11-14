/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/dock.h"

#include <stdio.h>
#include <string.h>

#include "core/fmt.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/parts.h"

#define DOCK_X 18.0f
#define DOCK_Y 440.0f
#define DOCK_W 924.0f
#define DOCK_H 64.0f
#define DOCK_WORD_X 32.0f
#define DOCK_WORD_Y 450.0f
#define DOCK_RIGHT 928.0f
#define DOCK_LINE3_Y 483.0f
#define DOCK_BAR_Y 473.0f
#define DOCK_BAR_W 896.0f
#define DOCK_BAR_H 4.0f

static void cut_top_right(Rect r, float c)
{
    draw_tri(r.x + r.w - c, r.y, r.x + r.w, r.y + c, r.x + r.w, r.y, C_FIELD);
}

static void dock_frame(int focused)
{
    Rect r = {DOCK_X, DOCK_Y, DOCK_W, DOCK_H};
    draw_rect(r.x, r.y, r.w, r.h, C_PANEL);
    cut_top_right(r, 14.0f);

    uint32_t outline = draw_dim(C_CYAN, 0.45f);
    draw_line(r.x, r.y, r.x + r.w - 14.0f, r.y, outline);
    draw_line(r.x + r.w - 14.0f, r.y, r.x + r.w, r.y + 14.0f, outline);
    draw_line(r.x + r.w, r.y + 14.0f, r.x + r.w, r.y + r.h, outline);
    draw_line(r.x + r.w, r.y + r.h, r.x, r.y + r.h, outline);
    draw_line(r.x, r.y + r.h, r.x, r.y, outline);

    if (focused)
        draw_line(r.x + 1.0f, r.y + 1.0f, r.x + r.w - 1.0f, r.y + 1.0f,
                  C_WHITE);
}

static void right_parts(const char *a, uint32_t ac, const char *b, uint32_t bc)
{
    float rx = DOCK_RIGHT;
    if (b != NULL && b[0] != '\0') {
        float w = font_width(b, PX_MICRO);
        font_text_right(b, rx, 453.0f, PX_MICRO, bc);
        rx -= w + 12.0f;
    }
    if (a != NULL && a[0] != '\0')
        font_text_right(a, rx, 453.0f, PX_MICRO, ac);
}

void dock_panel_draw(const DockPanel *p)
{
    if (p == NULL)
        return;

    dock_frame(p->focused);

    font_text_tracked(p->word != NULL ? p->word : "", DOCK_WORD_X, DOCK_WORD_Y,
                      PX_LABEL, p->word_color, 2.0f);

    float word_w = font_width(p->word != NULL ? p->word : "", PX_LABEL);
    if (p->word != NULL && p->word[0] != '\0')
        word_w += (float)(strlen(p->word) - 1) * 2.0f; /* tracked 2 */
    float x = DOCK_WORD_X + word_w + 16.0f;

    if (p->subject != NULL && p->subject[0] != '\0') {
        font_text(p->subject, x, DOCK_WORD_Y, PX_LABEL, C_WHITE);
        x += font_width(p->subject, PX_LABEL);
        if (p->path != NULL && p->path[0] != '\0')
            font_text(p->path, x + 10.0f, 453.5f, PX_MICRO, C_CYAN);
    }

    right_parts(p->right_a, p->right_a_color, p->right_b, p->right_b_color);

    if (p->progress_valid) {
        draw_rect(DOCK_WORD_X, DOCK_BAR_Y, DOCK_BAR_W, DOCK_BAR_H, C_HAIR);
        float fill = p->progress;
        if (fill < 0.0f)
            fill = 0.0f;
        if (fill > 1.0f)
            fill = 1.0f;
        draw_rect(DOCK_WORD_X, DOCK_BAR_Y, DOCK_BAR_W * fill, DOCK_BAR_H,
                  p->bar_color);
    }

    if (p->left != NULL && p->left[0] != '\0') {
        font_text(p->left, DOCK_WORD_X, DOCK_LINE3_Y, PX_MICRO, p->left_color);
        if (p->left2 != NULL && p->left2[0] != '\0') {
            float lx = DOCK_WORD_X + font_width(p->left, PX_MICRO) + 24.0f;
            font_text(p->left2, lx, DOCK_LINE3_Y, PX_MICRO, C_WHITE);
        }
    }
    float right = DOCK_RIGHT;
    if (p->badge != NULL && p->badge[0] != '\0') {
        float bw = font_width(p->badge, PX_MICRO);
        font_text_right(p->badge, DOCK_RIGHT, DOCK_LINE3_Y, PX_MICRO,
                        p->badge_color);
        right -= bw + 16.0f;
    }
    if (p->right_text != NULL && p->right_text[0] != '\0')
        font_text_right(p->right_text, right, DOCK_LINE3_Y, PX_MICRO,
                        p->right_text_color);
}

static const char *direction_word(XferDirection direction)
{
    switch (direction) {
    case XFER_DIR_UPLOAD:
        return "UPLOAD";
    case XFER_DIR_DOWNLOAD:
        return "DOWNLOAD";
    case XFER_DIR_LOCAL_COPY:
        return "COPY";
    case XFER_DIR_LOCAL_MOVE:
        return "MOVE";
    case XFER_DIR_LOCAL_DELETE:
        return "DELETE";
    case XFER_DIR_LOCAL_RENAME:
        return "RENAME";
    case XFER_DIR_LOCAL_MKDIR:
        return "NEW FOLDER";
    case XFER_DIR_SERVER_DOWNLOAD:
        return "DOWNLOAD";
    case XFER_DIR_SERVER_UPLOAD:
        return "UPLOAD";
    default:
        return "TRANSFER";
    }
}

static const char *direction_done_word(XferDirection direction)
{
    switch (direction) {
    case XFER_DIR_UPLOAD:
    case XFER_DIR_SERVER_UPLOAD:
        return "UPLOADED";
    case XFER_DIR_DOWNLOAD:
    case XFER_DIR_SERVER_DOWNLOAD:
        return "DOWNLOADED";
    case XFER_DIR_LOCAL_COPY:
        return "COPIED";
    case XFER_DIR_LOCAL_MOVE:
        return "MOVED";
    case XFER_DIR_LOCAL_DELETE:
        return "DELETED";
    case XFER_DIR_LOCAL_RENAME:
        return "RENAMED";
    case XFER_DIR_LOCAL_MKDIR:
        return "FOLDER CREATED";
    default:
        return "DONE";
    }
}

static const char *pane_name(UiPane pane)
{
    return pane == UI_PANE_LOCAL ? "LOCAL" : "REMOTE";
}

static void idle_dock(const UiState *state, DockPanel *p)
{
    const UiPaneState *pane = &state->panes[state->active_pane];
    int no_session = !state->panes[UI_PANE_REMOTE].connected;

    p->word = "IDLE";
    p->word_color = C_DIM;
    p->subject = no_session ? "REMOTE" : pane_name(state->active_pane);
    p->path = pane->path;
    p->progress_valid = 0;
    p->left_color = C_DIM;

    static char marks[32];
    static char size[32];
    if (pane->mark_count > 0) {
        fmt_count(marks, sizeof(marks), (unsigned)pane->mark_count, "MARKED",
                  "MARKED");
        p->right_a = marks;
        p->right_a_color = C_YELLOW;
        fmt_size(size, sizeof(size), pane->marked_bytes,
                 pane->marked_bytes_known);
        p->right_b = size;
        p->right_b_color = C_WHITE;
    } else {
        p->right_a = "NO MARKS";
        p->right_a_color = C_DIM;
        p->right_b = NULL;
    }

    static char left[64];
    if (state->status[0] != '\0') {
        p->left = state->status;
        p->left_color = C_YELLOW;
    } else if (state->clip_mode != UI_CLIP_NONE) {
        snprintf(left, sizeof(left), "%s %d - TRIANGLE TO PASTE",
                 state->clip_mode == UI_CLIP_MOVE ? "MOVE" : "COPY",
                 state->clip_count);
        p->left = left;
        p->left_color = C_CYAN;
    } else if (no_session) {
        p->left = "NO SELECTION";
    } else if (pane->mark_count > 0) {
        snprintf(left, sizeof(left), "X TO %s %d ITEM%s",
                 state->active_pane == UI_PANE_LOCAL ? "UPLOAD" : "DOWNLOAD",
                 pane->mark_count, pane->mark_count == 1 ? "" : "S");
        p->left = left;
    } else {
        const UiRow *row = ui_state_selected_row(state, state->active_pane);
        if (row != NULL && row->type == UI_ROW_FILE) {
            snprintf(left, sizeof(left), "X TO %s",
                     state->active_pane == UI_PANE_LOCAL ? "UPLOAD"
                                                         : "DOWNLOAD");
        } else {
            snprintf(left, sizeof(left), "X TO OPEN");
        }
        p->left = left;
    }
}

static void active_dock(const UiDock *d, DockPanel *p)
{
    static char total_text[96];
    static char percent[16];
    static char rate[32];
    static char index[32];

    p->word = direction_word(d->direction);
    p->word_color = C_YELLOW;
    p->subject = d->name;
    p->path = NULL;
    p->progress_valid = d->total_known && d->total > 0;
    p->progress = p->progress_valid ? (float)((double)d->done / (double)d->total)
                                    : 0.0f;
    p->bar_color = C_YELLOW;

    if (d->item_index > 0 && d->item_total > 0) {
        snprintf(index, sizeof(index), "%d OF %d", d->item_index,
                 d->item_total);
        p->right_a = index;
        p->right_a_color = C_DIM;
    }

    char done[32];
    char total[32];
    fmt_size(done, sizeof(done), d->done, 1);
    fmt_size(total, sizeof(total), d->total, d->total_known);
    snprintf(total_text, sizeof(total_text), "%s OF %s", done, total);
    fmt_percent(percent, sizeof(percent), d->done, d->total, d->total_known);
    fmt_rate(rate, sizeof(rate), d->rate, d->rate_known);

    p->left = total_text;
    p->left_color = C_WHITE;
    p->left2 = percent;
    p->right_text = rate;
    p->right_text_color = C_WHITE;
}

static void done_dock(const UiDock *d, DockPanel *p)
{
    static char subject[64];
    static char line[192];
    static char rate[40];
    static char skipped_text[32];

    int batch = d->item_total > 1;
    int done = d->item_index;
    int failed = d->failed;
    int skipped = d->skipped;
    int cancelled = d->cancelled;

    if (skipped > 0 && done == 0 && failed == 0 && cancelled == 0) {
        p->word = "SKIPPED";
        p->word_color = C_DIM;
    } else if (cancelled > 0) {
        p->word = "CANCELLED";
        p->word_color = C_WHITE;
    } else if (done == 0 && failed > 0) {
        /* Nothing succeeded: the past-tense operation word would claim it did,
         * and "COPIED" in red next to a failure reads as a contradiction. */
        p->word = "FAILED";
        p->word_color = C_RED;
    } else {
        p->word = direction_done_word(d->direction);
        if (failed > 0)
            p->word_color = C_YELLOW;
        else
            p->word_color = C_GREEN;
    }

    if (batch) {
        if (cancelled > 0 || failed > 0)
            snprintf(subject, sizeof(subject), "%d OF %d ITEMS", done,
                     d->item_total);
        else
            snprintf(subject, sizeof(subject), "%d ITEMS", d->item_total);
        p->subject = subject;
    } else {
        p->subject = d->name;
    }

    p->progress_valid = 1;
    p->progress = 1.0f;
    p->bar_color = C_YELLOW;

    if (batch && skipped > 0) {
        snprintf(skipped_text, sizeof(skipped_text), "%d SKIPPED", skipped);
        p->right_a = skipped_text;
        p->right_a_color = C_YELLOW;
    }

    char size[32];
    fmt_size(size, sizeof(size), d->total_known ? d->total : d->done, 1);
    if (failed > 0 && d->message[0] != '\0') {
        snprintf(line, sizeof(line), "%s", d->message);
    } else if (d->elapsed_known) {
        unsigned seconds = (unsigned)(d->elapsed_ms / 1000u);
        char hms[16];
        theme_fmt_hms(hms, sizeof(hms), (int)seconds);
        snprintf(line, sizeof(line), "%s IN %s", size, hms);
    } else {
        snprintf(line, sizeof(line), "%s", size);
    }
    p->left = line;
    p->left_color = C_WHITE;

    if (d->rate_known) {
        fmt_rate(rate, sizeof(rate), d->rate, 1);
        static char avg[48];
        snprintf(avg, sizeof(avg), "%s AVG", rate);
        p->right_text = avg;
        p->right_text_color = C_WHITE;
    }
}

static void failed_dock(const UiDock *d, DockPanel *p)
{
    static char line[192];
    static char index[32];

    p->word = "FAILED";
    p->word_color = C_RED;
    p->subject = d->name;
    p->progress_valid = d->total_known && d->total > 0;
    p->progress = p->progress_valid ? (float)((double)d->done / (double)d->total)
                                    : 0.0f;
    p->bar_color = C_DIM;

    if (d->item_index > 0 && d->item_total > 0) {
        snprintf(index, sizeof(index), "%d OF %d", d->item_index,
                 d->item_total);
        p->right_a = index;
        p->right_a_color = C_DIM;
    }

    snprintf(line, sizeof(line), "%s",
             d->message[0] != '\0' ? d->message : "TRANSFER STOPPED");
    p->left = line;
    p->left_color = C_WHITE;
    p->right_text = "NO PARTIAL KEPT";
    p->right_text_color = C_DIM;
}

static void badge_fill(const UiDock *d, char *out, size_t cap)
{
    out[0] = '\0';
    if (d->waiting) {
        snprintf(out, cap, "LOCAL WAITING");
        return;
    }
    if (d->cancelled) {
        snprintf(out, cap, "LOCAL CANCELLING");
        return;
    }
    if (!d->active && d->state == UI_DOCK_FAILED) {
        snprintf(out, cap, "LOCAL FAILED");
        return;
    }
    if (!d->active) {
        snprintf(out, cap, "LOCAL DONE");
        return;
    }
    char name[128];
    font_fit(d->name, PX_MICRO, 160.0f, name, sizeof(name));
    if (d->item_total > 0)
        snprintf(out, cap, "LOCAL %d/%d %s", d->item_index, d->item_total,
                 name);
    else
        snprintf(out, cap, "LOCAL %s", name);
}

static uint32_t badge_color(const UiDock *d)
{
    if (d->waiting)
        return C_YELLOW;
    if (d->cancelled)
        return C_WHITE;
    if (!d->active && d->state == UI_DOCK_FAILED)
        return C_RED;
    if (d->active)
        return C_CYAN;
    return C_DIM;
}

void dock_draw(const UiState *state)
{
    if (state == NULL)
        return;

    const UiDock *full = &state->dock;
    if (state->dock_primary == UI_DOCK_LANE_LOCAL && state->local_shown)
        full = &state->local_dock;

    DockPanel p;
    memset(&p, 0, sizeof(p));
    p.focused = state->dock.focused;

    if (full->state == UI_DOCK_ACTIVE && full->active)
        active_dock(full, &p);
    else if (full->state == UI_DOCK_DONE)
        done_dock(full, &p);
    else if (full->state == UI_DOCK_FAILED)
        failed_dock(full, &p);
    else
        idle_dock(state, &p);

    static char badge[256];
    if (state->dock.active && state->local_dock.active && state->local_shown) {
        badge_fill(&state->local_dock, badge, sizeof(badge));
        p.badge = badge;
        p.badge_color = badge_color(&state->local_dock);
    }

    dock_panel_draw(&p);
}

void dock_badge_draw(const UiState *state)
{
    if (state == NULL || !state->local_shown)
        return;

    const UiDock *d = &state->local_dock;
    int running = d->active || d->state == UI_DOCK_DONE ||
                  d->state == UI_DOCK_FAILED;
    if (!running)
        return;

    static char badge[256];
    badge_fill(d, badge, sizeof(badge));
    font_text_right(badge, DOCK_RIGHT, DOCK_LINE3_Y, PX_MICRO,
                    badge_color(d));
}
