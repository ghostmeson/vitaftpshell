/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/editor.h"

#include <stdio.h>
#include <string.h>

#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/dock.h"
#include "ui/parts.h"

static const char *field_labels[UI_FIELD_COUNT] = {
    "NAME", "HOST", "PORT", "ANONYMOUS", "USERNAME", "PASSWORD",
};

static void card_draw(const UiState *state, const LayoutRect *r)
{
    const UiEditor *editor = &state->editor;

    Rect card = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(card, 1, C_CYAN);

    font_text("SITE PREVIEW", 32.0f, 86.0f, PX_MICRO, C_DIM);

    char name[128];
    if (editor->name[0] != '\0') {
        font_fit(editor->name, PX_VALUE, 540.0f, name, sizeof(name));
    } else {
        snprintf(name, sizeof(name), "NEW SITE");
    }
    font_text(name, 32.0f, 100.0f, PX_VALUE,
              editor->name[0] != '\0' ? C_WHITE : C_DIM);

    char host[320];
    char fitted_host[192];
    if (editor->host[0] != '\0')
        snprintf(host, sizeof(host), "%s:%d", editor->host, editor->port);
    else
        snprintf(host, sizeof(host), "--");
    font_fit(host, PX_MICRO, 540.0f, fitted_host, sizeof(fitted_host));
    font_text(fitted_host, 32.0f, 128.0f, PX_MICRO, C_CYAN);

    font_text("LOGIN", 600.0f, 100.0f, PX_LABEL, C_DIM);
    font_text_right(editor->anonymous
                        ? "ANONYMOUS"
                        : (editor->username[0] != '\0' ? editor->username
                                                       : "--"),
                    928.0f, 100.0f, PX_LABEL, C_WHITE);

    font_text("PASSWORD", 600.0f, 130.0f, PX_LABEL, C_DIM);
    const char *password;
    if (editor->anonymous)
        password = "NOT NEEDED";
    else if (!editor->password_set)
        password = "NOT SET";
    else
        password = editor->new_site ? "SET" : "STORED";
    font_text_right(password, 928.0f, 130.0f, PX_LABEL, C_WHITE);

    if (editor->error[0] != '\0') {
        font_text(editor->error, 32.0f, 160.0f, PX_MICRO, C_YELLOW);
    } else if (!editor->anonymous && editor->dirty && editor->password_set) {
        font_text("PASSWORDS ARE STORED UNENCRYPTED", 32.0f, 160.0f,
                  PX_MICRO, C_YELLOW);
    } else if (!editor->anonymous && !editor->password_set) {
        font_text("NO PASSWORD STORED", 32.0f, 160.0f, PX_MICRO, C_DIM);
    } else if (editor->anonymous) {
        font_text("NO PASSWORD STORED", 32.0f, 160.0f, PX_MICRO, C_DIM);
    } else {
        font_text("PASSWORDS ARE STORED UNENCRYPTED", 32.0f, 160.0f,
                  PX_MICRO, C_DIM);
    }

    font_text_right(editor->dirty ? "UNSAVED" : "NO CHANGES", 928.0f, 160.0f,
                    PX_MICRO, editor->dirty ? C_WHITE : C_DIM);
}

static void field_value(const UiEditor *editor, int field, char *out,
                        size_t cap, int *valid)
{
    *valid = 1;
    out[0] = '\0';

    switch ((UiField)field) {
    case UI_FIELD_NAME:
        snprintf(out, cap, "%s", editor->name);
        *valid = editor->name[0] != '\0';
        break;
    case UI_FIELD_HOST:
        snprintf(out, cap, "%s", editor->host);
        *valid = editor->host[0] != '\0';
        break;
    case UI_FIELD_PORT:
        snprintf(out, cap, "%d", editor->port);
        break;
    case UI_FIELD_ANONYMOUS:
        snprintf(out, cap, "%s", editor->anonymous ? "ON" : "OFF");
        break;
    case UI_FIELD_USERNAME:
        if (editor->anonymous) {
            snprintf(out, cap, "NOT USED");
            *valid = 0;
        } else {
            snprintf(out, cap, "%s", editor->username);
            *valid = editor->username[0] != '\0';
        }
        break;
    case UI_FIELD_PASSWORD:
        if (editor->anonymous) {
            snprintf(out, cap, "NOT USED");
            *valid = 0;
        } else if (editor->password_set) {
            snprintf(out, cap, "..........");
            *valid = 0;
        } else {
            *valid = 0;
        }
        break;
    default:
        break;
    }

    if (out[0] == '\0')
        snprintf(out, cap, "--");
}

static void fields_draw(const UiState *state, const LayoutRect *r)
{
    const UiEditor *editor = &state->editor;

    Rect panel = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(panel, 0, C_CYAN);

    font_text_tracked("SITE FIELDS", 32.0f, 206.0f, PX_LABEL, C_DIM, 2.0f);
    ui_hair(32.0f, 241.0f, 896.0f);

    for (int i = 0; i < UI_FIELD_COUNT; ++i) {
        float y = 246.0f + (float)i * 30.0f;
        int focused = editor->field == i;
        int error = focused && editor->error[0] != '\0';

        if (focused) {
            ui_wash(26.0f, y, 902.0f, 30.0f, 0.06f);
            draw_rect(26.0f, y, 2.0f, 30.0f, C_WHITE);
        }

        uint32_t label_color = error ? C_YELLOW : (focused ? C_WHITE : C_DIM);
        font_text(field_labels[i], 50.0f, y + 8.0f, PX_LABEL, label_color);

        char value[256];
        int valid = 1;
        field_value(editor, i, value, sizeof(value), &valid);
        uint32_t value_color;
        if (error)
            value_color = C_YELLOW;
        else if ((UiField)i == UI_FIELD_ANONYMOUS)
            value_color = editor->anonymous ? C_GREEN : C_DIM;
        else
            value_color = valid ? C_WHITE : C_DIM;
        font_text_right(value, 928.0f, y + 8.0f, PX_LABEL, value_color);
    }
}

static void dock_editor(const UiState *state)
{
    DockPanel p;
    memset(&p, 0, sizeof(p));

    const UiEditor *editor = &state->editor;
    p.word = "EDITING";
    p.word_color = C_YELLOW;
    p.subject = editor->name[0] != '\0' ? editor->name : "NEW SITE";
    p.left = editor->dirty ? "SQUARE TO SAVE  -  CIRCLE TO CANCEL"
                           : "CIRCLE TO CLOSE";
    p.left_color = C_WHITE;
    p.right_a = editor->dirty ? "UNSAVED" : "NO CHANGES";
    p.right_a_color = editor->dirty ? C_WHITE : C_DIM;

    dock_panel_draw(&p);
}

static void legend_draw(void)
{
    float x = (float)LAYOUT_MARGIN;
    x = ui_keycap("X", "EDIT FIELD", x, 510.0f);
    x = ui_keycap("SQUARE", "SAVE", x, 510.0f);
    (void)ui_keycap("O", "CANCEL", x, 510.0f);
}

void editor_draw(const UiState *state)
{
    if (state == NULL)
        return;

    unsigned count = 0;
    const LayoutRect *r = layout_regions(LAYOUT_SITE_EDITOR, &count);
    if (count < 5)
        return;

    card_draw(state, &r[1]);
    fields_draw(state, &r[2]);
    dock_editor(state);
    legend_draw();
}
