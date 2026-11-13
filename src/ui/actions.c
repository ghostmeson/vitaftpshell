/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/actions.h"

#include <stdio.h>
#include <string.h>

#include "core/fmt.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/dock.h"
#include "ui/parts.h"

static const char *action_label(UiAction action)
{
    switch (action) {
    case UI_ACTION_COPY:
        return "COPY";
    case UI_ACTION_MOVE:
        return "MOVE";
    case UI_ACTION_PASTE:
        return "PASTE";
    case UI_ACTION_CLEAR_CLIPBOARD:
        return "CLEAR";
    case UI_ACTION_UPLOAD:
        return "UPLOAD";
    case UI_ACTION_DOWNLOAD:
        return "DOWNLOAD";
    case UI_ACTION_NEW_FOLDER:
        return "NEW FOLDER";
    case UI_ACTION_RENAME:
        return "RENAME";
    case UI_ACTION_DELETE:
        return "DELETE";
    case UI_ACTION_REFRESH:
        return "REFRESH";
    case UI_ACTION_DISCONNECT:
        return "DISCONNECT";
    default:
        return "";
    }
}

static const char *action_desc(UiAction action, UiClipMode clip)
{
    switch (action) {
    case UI_ACTION_COPY:
        return "TO CLIPBOARD";
    case UI_ACTION_MOVE:
        return "TO CLIPBOARD";
    case UI_ACTION_PASTE:
        return clip == UI_CLIP_MOVE ? "MOVE HERE" : "COPY HERE";
    case UI_ACTION_CLEAR_CLIPBOARD:
        return "DISCARD THE CLIPBOARD";
    case UI_ACTION_UPLOAD:
        return "TO REMOTE PANE";
    case UI_ACTION_DOWNLOAD:
        return "TO LOCAL PANE";
    case UI_ACTION_NEW_FOLDER:
        return "CREATE HERE";
    case UI_ACTION_RENAME:
        return "CHANGE THE NAME";
    case UI_ACTION_DELETE:
        return "CANNOT BE UNDONE";
    case UI_ACTION_REFRESH:
        return "READ THE FOLDER AGAIN";
    case UI_ACTION_DISCONNECT:
        return "CLOSE THE SESSION";
    default:
        return "";
    }
}

static const char *action_consequence(UiAction action, UiClipMode clip)
{
    switch (action) {
    case UI_ACTION_COPY:
        return "PUTS THE MARKS ON THE CLIPBOARD. NAVIGATE, THEN PASTE.";
    case UI_ACTION_MOVE:
        return "PUTS THE MARKS ON THE CLIPBOARD. NAVIGATE, THEN PASTE.";
    case UI_ACTION_PASTE:
        return clip == UI_CLIP_MOVE
                   ? "MOVES THE CLIPBOARD INTO THIS FOLDER."
                   : "COPIES THE CLIPBOARD INTO THIS FOLDER.";
    case UI_ACTION_CLEAR_CLIPBOARD:
        return "DISCARDS THE COPIED ITEMS.";
    case UI_ACTION_UPLOAD:
        return "SENDS THE SELECTION TO THE REMOTE PANE.";
    case UI_ACTION_DOWNLOAD:
        return "RECEIVES THE ITEM INTO THE LOCAL PANE.";
    case UI_ACTION_NEW_FOLDER:
        return "CREATES A FOLDER IN THIS FOLDER.";
    case UI_ACTION_RENAME:
        return "RENAMES THE SELECTED ITEM.";
    case UI_ACTION_DELETE:
        return "DELETES PERMANENTLY. CANNOT BE UNDONE.";
    case UI_ACTION_REFRESH:
        return "UPDATES THE REMOTE LISTING.";
    case UI_ACTION_DISCONNECT:
        return "ENDS THE FTP SESSION AND CLEARS THE REMOTE PANE.";
    default:
        return "";
    }
}

static const char *last_segment(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == ':')
            name = p + 1;
    }
    return name;
}

static void ledger_draw(const UiState *state, const LayoutRect *r)
{
    const UiActions *actions = &state->actions;
    UiPane pane = (UiPane)actions->pane;

    Rect panel = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(panel, 0, C_CYAN);

    font_text_tracked(pane == UI_PANE_LOCAL ? "LOCAL ACTIONS" : "REMOTE ACTIONS",
                      32.0f, 84.0f, PX_LABEL, C_DIM, 2.0f);
    font_text(actions->path, 32.0f, 102.0f, PX_MICRO, C_CYAN);

    char band[40];
    uint32_t band_color = C_DIM;
    if (state->clip_mode != UI_CLIP_NONE) {
        snprintf(band, sizeof(band), "%s %d",
                 state->clip_mode == UI_CLIP_MOVE ? "MOVE" : "COPY",
                 state->clip_count);
        band_color = C_YELLOW;
    } else if (actions->marked > 0) {
        snprintf(band, sizeof(band), "%d MARKED", actions->marked);
        band_color = C_YELLOW;
    } else if ((actions->reason[UI_ACTION_DOWNLOAD][0] != '\0' &&
                strcmp(actions->reason[UI_ACTION_DOWNLOAD], "UNSAFE NAME") ==
                    0) ||
               (actions->reason[UI_ACTION_DELETE][0] != '\0' &&
                strcmp(actions->reason[UI_ACTION_DELETE], "UNSAFE NAME") == 0)) {
        snprintf(band, sizeof(band), "UNSAFE NAME");
        band_color = C_YELLOW;
    } else if (!actions->has_selection) {
        snprintf(band, sizeof(band), "NO SELECTION");
        band_color = C_DIM;
    } else if (actions->reason[UI_ACTION_DELETE][0] != '\0' &&
               (strcmp(actions->reason[UI_ACTION_DELETE], "VOLUME ROOT") == 0 ||
                strcmp(actions->reason[UI_ACTION_DELETE],
                       "PROTECTED PATH") == 0)) {
        snprintf(band, sizeof(band), "%s",
                 actions->reason[UI_ACTION_DELETE]);
        band_color = C_YELLOW;
    } else {
        snprintf(band, sizeof(band), "1 ITEM");
    }
    font_text_right(band, 556.0f, 86.0f, PX_MICRO, band_color);

    ui_hair(32.0f, 119.0f, 512.0f);

    int count = actions->visible_count;
    if (count <= 0 || count > UI_ACTION_COUNT) {
        count = UI_ACTION_COUNT;
    }

    for (int i = 0; i < count; ++i) {
        UiAction action =
            (UiAction)(actions->visible_count > 0 ? actions->visible[i] : i);
        float y = 124.0f + (float)i * 44.0f;
        int selected = action == (UiAction)actions->selected;
        int enabled = actions->enabled[action];

        if (selected) {
            ui_wash((float)r->x + 1.0f, y, (float)r->w - 2.0f, 44.0f, 0.06f);
            draw_rect((float)r->x + 8.0f, y, 2.0f, 44.0f, C_WHITE);
        }

        font_text(action_label(action), 50.0f, y + 6.0f, PX_LABEL,
                  enabled ? C_WHITE : C_DIM);

        const char *desc =
            enabled ? action_desc(action, state->clip_mode)
                    : actions->reason[action];
        uint32_t desc_color = enabled
                                  ? (action == UI_ACTION_DELETE ? C_YELLOW
                                                                : C_DIM)
                                  : C_DIM;
        font_text(desc != NULL && desc[0] != '\0'
                      ? desc
                      : action_desc(action, state->clip_mode),
                  50.0f, y + 23.0f, PX_MICRO, desc_color);
    }
}

static int target_size(const UiState *state, const UiActions *actions,
                       uint64_t *bytes, int *known)
{
    if (actions->marked > 0) {
        const UiPaneState *pane = &state->panes[state->active_pane];
        *bytes = pane->marked_bytes;
        *known = pane->marked_bytes_known;
        return 1;
    }

    const UiRow *row = ui_state_selected_row(state, state->active_pane);
    if (row == NULL) {
        *bytes = 0;
        *known = 0;
        return 0;
    }
    *bytes = row->size;
    *known = row->size_known;
    return 1;
}

static void target_draw(const UiState *state, const LayoutRect *r)
{
    const UiActions *actions = &state->actions;
    UiPane pane = (UiPane)actions->pane;

    Rect panel = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(panel, 0, C_CYAN);

    font_text("TARGET", 596.0f, 84.0f, PX_MICRO, C_DIM);
    font_text(pane == UI_PANE_LOCAL ? "LOCAL" : "REMOTE", 596.0f, 108.0f,
              PX_LABEL, C_WHITE);
    font_text(actions->dest, 596.0f, 126.0f, PX_MICRO, C_CYAN);

    int protected_root = 0;
    for (int i = 0; i < UI_ACTION_COUNT; ++i) {
        if (actions->reason[i][0] != '\0' &&
            (strcmp(actions->reason[i], "VOLUME ROOT") == 0 ||
             strcmp(actions->reason[i], "PROTECTED PATH") == 0))
            protected_root = 1;
    }

    uint64_t bytes = 0;
    int known = 0;
    int have_size = target_size(state, actions, &bytes, &known);

    UiAction selected = (UiAction)actions->selected;
    if (selected == UI_ACTION_PASTE && state->clip_mode != UI_CLIP_NONE) {
        char count[32];
        snprintf(count, sizeof(count), "%d", state->clip_count);
        font_text(count, 596.0f, 148.0f, PX_PRIMARY, C_WHITE);
        font_text("CLIPBOARD", 596.0f, 169.0f, PX_MICRO, C_DIM);
    } else if (selected == UI_ACTION_REFRESH) {
        font_text("--", 596.0f, 148.0f, PX_VALUE, C_DIM);
        font_text("FOLDER", 596.0f, 178.0f, PX_LABEL, C_DIM);
    } else if (selected == UI_ACTION_DISCONNECT) {
        font_text("--", 596.0f, 148.0f, PX_VALUE, C_DIM);
        font_text("SESSION", 596.0f, 178.0f, PX_LABEL, C_DIM);
    } else if (!actions->has_selection && actions->marked == 0) {
        font_text("--", 596.0f, 148.0f, PX_VALUE, C_DIM);
        font_text("NO SELECTION", 596.0f, 178.0f, PX_LABEL, C_DIM);
    } else if (protected_root) {
        font_text("--", 596.0f, 148.0f, PX_VALUE, C_DIM);
        font_text(actions->reason[UI_ACTION_DELETE], 596.0f, 178.0f, PX_LABEL,
                  C_YELLOW);
    } else if (actions->marked > 0) {
        char count[32];
        snprintf(count, sizeof(count), "%d", actions->marked);
        font_text(count, 596.0f, 148.0f, PX_PRIMARY, C_WHITE);
        font_text("MARKED", 596.0f, 169.0f, PX_MICRO, C_DIM);
        char total[32];
        fmt_size(total, sizeof(total), bytes, known);
        font_text_right(total, 928.0f, 169.0f, PX_MICRO, C_WHITE);
    } else {
        const char *name = last_segment(actions->path);
        char fitted[PATH_NAME_MAX + 4];
        font_fit(name, PX_VALUE, 332.0f, fitted, sizeof(fitted));
        font_text(fitted, 596.0f, 148.0f, PX_VALUE, C_WHITE);
        char size[32];
        fmt_size(size, sizeof(size), bytes, known && have_size);
        font_text(size, 596.0f, 178.0f, PX_LABEL, known ? C_WHITE : C_DIM);
    }

    ui_hair(596.0f, 210.0f, 332.0f);

    font_text("DESTINATION", 596.0f, 224.0f, PX_MICRO, C_DIM);
    const char *destination =
        selected == UI_ACTION_DISCONNECT
            ? "--"
            : ((selected == UI_ACTION_COPY || selected == UI_ACTION_MOVE)
                   ? "CLIPBOARD"
                   : (pane == UI_PANE_LOCAL ? actions->dest : actions->other));
    font_text_right(destination, 928.0f, 224.0f, PX_MICRO, C_CYAN);

    font_text(action_label(selected), 596.0f, 300.0f, PX_LABEL, C_WHITE);
    font_text(action_consequence(selected, state->clip_mode), 596.0f, 320.0f,
              PX_MICRO, C_DIM);

    if (actions->marked > 1)
        font_text("MARKS STAY AFTER THE OPERATION", 596.0f, 400.0f, PX_MICRO,
                  C_DIM);
}

static void dock_actions(const UiState *state)
{
    const UiActions *actions = &state->actions;
    DockPanel p;
    memset(&p, 0, sizeof(p));

    p.word = "READY";
    p.word_color = C_DIM;
    UiAction dock_selected = (UiAction)actions->selected;
    p.subject = action_label(dock_selected);
    p.path = dock_selected == UI_ACTION_DISCONNECT
                 ? "--"
                 : ((dock_selected == UI_ACTION_COPY ||
                     dock_selected == UI_ACTION_MOVE)
                        ? "CLIPBOARD"
                        : actions->dest);
    if (state->clip_mode != UI_CLIP_NONE) {
        p.right_a = "CLIPBOARD";
        p.right_a_color = C_YELLOW;
    } else if (actions->marked > 0) {
        p.right_a = "MARKED";
        p.right_a_color = C_YELLOW;
    } else if (!actions->has_selection) {
        p.right_a = "NO SELECTION";
        p.right_a_color = C_DIM;
    } else {
        p.right_a = "1 ITEM";
        p.right_a_color = C_DIM;
    }

    UiAction selected = (UiAction)actions->selected;
    if (state->status[0] != '\0') {
        p.left = state->status;
        p.left_color = C_YELLOW;
    } else if (!actions->enabled[selected] &&
               actions->reason[selected][0] != '\0') {
        p.left = actions->reason[selected];
        p.left_color = C_YELLOW;
    } else if (state->clip_mode != UI_CLIP_NONE &&
               selected == UI_ACTION_PASTE) {
        p.left = "X TO PASTE";
        p.left_color = C_WHITE;
    } else {
        p.left = "X TO RUN";
        p.left_color = C_WHITE;
    }

    dock_panel_draw(&p);
}

static void legend_draw(void)
{
    float x = (float)LAYOUT_MARGIN;
    x = ui_keycap("X", "RUN", x, 510.0f);
    x = ui_keycap("O", "CLOSE", x, 510.0f);
    font_text("UP/DOWN", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("UP/DOWN", PX_MICRO) + 6.0f;
    font_text("ACTION", x, 512.0f, PX_MICRO, C_DIM);
}

void actions_draw(const UiState *state)
{
    if (state == NULL)
        return;

    unsigned count = 0;
    const LayoutRect *r = layout_regions(LAYOUT_ACTIONS, &count);
    if (count < 5)
        return;

    ledger_draw(state, &r[1]);
    target_draw(state, &r[2]);
    dock_actions(state);
    legend_draw();
}
