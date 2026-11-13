/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/confirm.h"

#include <stdio.h>
#include <string.h>

#include "core/fmt.h"

#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/parts.h"

#define CONFIRM_X 180.0f
#define CONFIRM_Y 122.0f
#define CONFIRM_W 600.0f
#define CONFIRM_H 300.0f
#define CONFIRM_ITEM_X 210.0f
#define CONFIRM_ITEM_TOP 190.0f
#define CONFIRM_ITEM_PITCH 24.0f
#define CONFIRM_ITEM_VIEW 5

static void item_size(const UiConfirmItem *item, char *out, size_t cap,
                      uint32_t *color)
{
    if (item->size_known) {
        fmt_size(out, cap, item->size, 1);
        *color = C_WHITE;
    } else {
        snprintf(out, cap, "DIR");
        *color = C_DIM;
    }
}

static void items_draw(const UiConfirm *confirm)
{
    static char sizes[CONFIRM_ITEM_VIEW][32];
    static char names[CONFIRM_ITEM_VIEW][PATH_NAME_MAX + 4];

    int shown = 0;
    for (int i = 0; i < CONFIRM_ITEM_VIEW; ++i) {
        int index = confirm->item_scroll + i;
        if (index >= confirm->item_count)
            break;
        const UiConfirmItem *item = &confirm->items[index];
        float y = CONFIRM_ITEM_TOP + (float)i * CONFIRM_ITEM_PITCH;

        font_fit(item->name, PX_LABEL, 500.0f, names[i], sizeof(names[i]));
        font_text(names[i], CONFIRM_ITEM_X, y, PX_LABEL, C_WHITE);

        uint32_t color = C_WHITE;
        item_size(item, sizes[i], sizeof(sizes[i]), &color);
        font_text_right(sizes[i], 750.0f, y, PX_MICRO, color);
        shown++;
    }

    if (confirm->item_count > CONFIRM_ITEM_VIEW) {
        float track = (float)CONFIRM_ITEM_VIEW * CONFIRM_ITEM_PITCH;
        float thumb = track * (float)CONFIRM_ITEM_VIEW /
                      (float)confirm->item_count;
        if (thumb < 24.0f)
            thumb = 24.0f;
        float max_scroll = (float)(confirm->item_count - CONFIRM_ITEM_VIEW);
        float pos = (track - thumb) * (float)confirm->item_scroll /
                    (max_scroll > 0.0f ? max_scroll : 1.0f);
        draw_rect(758.0f, CONFIRM_ITEM_TOP, 2.0f, track, C_HAIR);
        draw_rect(758.0f, CONFIRM_ITEM_TOP + pos, 2.0f, thumb, C_DIM);
    }

    if (shown > 0) {
        uint64_t total = 0;
        int known = 0;
        for (int i = 0; i < confirm->item_count; ++i) {
            if (confirm->items[i].size_known) {
                total += confirm->items[i].size;
                known = 1;
            }
        }
        char line[64];
        if (known) {
            char size[32];
            fmt_size(size, sizeof(size), total, 1);
            snprintf(line, sizeof(line), "%d ITEMS - %s KNOWN",
                     confirm->item_count, size);
        } else {
            snprintf(line, sizeof(line), "%d ITEMS", confirm->item_count);
        }
        font_text(line, CONFIRM_ITEM_X, 314.0f, PX_MICRO, C_DIM);
    }
}

static void body_draw(const UiConfirm *confirm)
{
    char line[192];
    float y = 196.0f;

    switch (confirm->kind) {
    case UI_CONFIRM_CONFLICT:
        if (confirm->existing_count > 0 && confirm->item_count > 0) {
            snprintf(line, sizeof(line),
                     "%d OF %d DESTINATIONS ALREADY EXIST.",
                     confirm->existing_count, confirm->item_count);
        } else {
            snprintf(line, sizeof(line), "DESTINATIONS ALREADY EXIST.");
        }
        font_text(line, CONFIRM_ITEM_X, y, PX_LABEL, C_WHITE);
        font_text("SKIP KEEPS THE EXISTING FILES.", CONFIRM_ITEM_X, y + 24.0f,
                  PX_LABEL, C_DIM);
        font_text("OVERWRITE REPLACES THEM.", CONFIRM_ITEM_X, y + 48.0f,
                  PX_LABEL, C_DIM);
        break;
    case UI_CONFIRM_CANCEL_XFER:
        snprintf(line, sizeof(line), "%s",
                 confirm->context[0] != '\0' ? confirm->context
                                             : "THE ACTIVE TRANSFER");
        font_text(line, CONFIRM_ITEM_X, y, PX_LABEL, C_WHITE);
        font_text("THE PARTIAL FILE IS REMOVED.", CONFIRM_ITEM_X, y + 24.0f,
                  PX_LABEL, C_DIM);
        break;
    case UI_CONFIRM_STOP_SERVER:
        snprintf(line, sizeof(line), "%s",
                 confirm->context[0] != '\0' ? confirm->context
                                             : "THE ACTIVE CLIENT");
        font_text(line, CONFIRM_ITEM_X, y, PX_LABEL, C_WHITE);
        font_text("THE WRITE IS INTERRUPTED.", CONFIRM_ITEM_X, y + 24.0f,
                  PX_LABEL, C_DIM);
        font_text("THE PARTIAL FILE IS REMOVED.", CONFIRM_ITEM_X, y + 48.0f,
                  PX_LABEL, C_DIM);
        break;
    case UI_CONFIRM_DISCARD:
        font_text("UNSAVED CHANGES ARE LOST.", CONFIRM_ITEM_X, y, PX_LABEL,
                  C_YELLOW);
        break;
    case UI_CONFIRM_HIGH_RISK:
        font_text("A BAD EDIT CAN STOP THE CONSOLE BOOTING.", CONFIRM_ITEM_X,
                  y, PX_LABEL, C_WHITE);
        font_text("THERE IS NO UNDO.", CONFIRM_ITEM_X, y + 24.0f, PX_LABEL,
                  C_YELLOW);
        break;
    case UI_CONFIRM_DELETE_SITE:
        font_text("THE SITE IS REMOVED FROM THE BOOK.", CONFIRM_ITEM_X, y,
                  PX_LABEL, C_DIM);
        break;
    default:
        break;
    }
}

static void options_draw(const UiConfirm *confirm)
{
    if (confirm->choice_count == 3) {
        static const float xs[3] = {210.0f, 395.0f, 580.0f};
        for (int i = 0; i < 3; ++i) {
            const char *label = "";
            switch (confirm->choices[i]) {
            case UI_CHOICE_CANCEL:
                label = "CANCEL";
                break;
            case UI_CHOICE_SKIP:
                label = "SKIP EXISTING";
                break;
            case UI_CHOICE_OVERWRITE:
                label = "OVERWRITE ALL";
                break;
            default:
                break;
            }
            ui_slab(xs[i], 366.0f, 170.0f, 40.0f, label,
                    confirm->choices[i] == UI_CHOICE_OVERWRITE,
                    confirm->choice_focus == i, PX_MICRO);
        }
        return;
    }

    static const float xs[2] = {260.0f, 500.0f};
    for (int i = 0; i < 2 && i < confirm->choice_count; ++i) {
        const char *label = "";
        int primary = 0;
        switch (confirm->choices[i]) {
        case UI_CHOICE_CANCEL:
            label = "CANCEL";
            break;
        case UI_CHOICE_DELETE:
            label = "DELETE";
            primary = 1;
            break;
        case UI_CHOICE_KEEP:
            label = "KEEP";
            break;
        case UI_CHOICE_DISCARD:
            label = "DISCARD";
            primary = 1;
            break;
        case UI_CHOICE_CONTINUE:
            label = "CONTINUE";
            primary = 1;
            break;
        case UI_CHOICE_STOP:
            label = "STOP SERVER";
            primary = 1;
            break;
        case UI_CHOICE_SKIP:
            label = "SKIP";
            break;
        case UI_CHOICE_OVERWRITE:
            label = "OVERWRITE";
            primary = 1;
            break;
        default:
            break;
        }
        if (confirm->kind == UI_CONFIRM_CANCEL_XFER && i == 0)
            label = "KEEP DOWNLOADING";
        if (confirm->kind == UI_CONFIRM_STOP_SERVER && i == 0)
            label = "KEEP RUNNING";
        if (confirm->kind == UI_CONFIRM_DISCARD && i == 0)
            label = "KEEP EDITING";

        ui_slab(xs[i], 366.0f, 200.0f, 40.0f, label, primary,
                confirm->choice_focus == i, PX_LABEL);
    }
}

static void legend_draw(const UiConfirm *confirm)
{
    float x = (float)LAYOUT_MARGIN;
    x = ui_keycap("X", "SELECT", x, 510.0f);
    x = ui_keycap("O", "CANCEL", x, 510.0f);
    if (confirm->item_count > CONFIRM_ITEM_VIEW) {
        font_text("UP/DOWN", x, 512.0f, PX_MICRO, C_DIM);
        x += font_width("UP/DOWN", PX_MICRO) + 6.0f;
        font_text("SCROLL", x, 512.0f, PX_MICRO, C_DIM);
        x += font_width("SCROLL", PX_MICRO) + 20.0f;
    }
    font_text("LEFT/RIGHT", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("LEFT/RIGHT", PX_MICRO) + 6.0f;
    font_text("CHOOSE", x, 512.0f, PX_MICRO, C_DIM);
}

void confirm_draw(const UiState *state)
{
    if (state == NULL)
        return;

    const UiConfirm *confirm = &state->confirm;

    Rect panel = {CONFIRM_X, CONFIRM_Y, CONFIRM_W, CONFIRM_H};
    ui_panel_frame(panel, 0, C_YELLOW);

    font_text(confirm->title, CONFIRM_ITEM_X, 140.0f, PX_VALUE, C_YELLOW);

    if (confirm->high_risk && confirm->risk_path[0] != '\0') {
        font_text(confirm->risk_path, CONFIRM_ITEM_X, 168.0f, PX_MICRO,
                  C_CYAN_DIM);
    } else if (confirm->context[0] != '\0') {
        font_text(confirm->context, CONFIRM_ITEM_X, 168.0f, PX_MICRO,
                  C_CYAN_DIM);
    }

    if (confirm->items != NULL && confirm->item_count > 0)
        items_draw(confirm);
    else
        body_draw(confirm);

    if (confirm->warning[0] != '\0' && confirm->kind != UI_CONFIRM_HIGH_RISK)
        font_text(confirm->warning, CONFIRM_ITEM_X, 336.0f, PX_LABEL, C_YELLOW);

    options_draw(confirm);
    legend_draw(confirm);
}
