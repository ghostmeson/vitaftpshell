/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/sites.h"

#include <stdio.h>
#include <string.h>

#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/dock.h"
#include "ui/parts.h"

#define SITES_ROWS_VISIBLE 6

static void card_draw(const UiState *state, const LayoutRect *r)
{
    const UiSiteList *sites = &state->sites;
    const UiSiteRow *site = NULL;
    if (sites->count > 0 && sites->selected >= 0 &&
        sites->selected < sites->count)
        site = &sites->rows[sites->selected];

    Rect card = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(card, 1, C_CYAN);

    font_text("SELECTED SITE", 32.0f, 86.0f, PX_MICRO, C_DIM);

    if (site == NULL) {
        font_text("NO SITE", 32.0f, 100.0f, PX_VALUE, C_DIM);
        return;
    }

    char name[128];
    font_fit(site->name, PX_VALUE, 540.0f, name, sizeof(name));
    font_text(name, 32.0f, 100.0f, PX_VALUE, C_WHITE);

    char host[192];
    char fitted_host[192];
    snprintf(host, sizeof(host), "%s:%u", site->host,
             (unsigned)site->port);
    font_fit(host, PX_MICRO, 540.0f, fitted_host, sizeof(fitted_host));
    font_text(fitted_host, 32.0f, 128.0f, PX_MICRO, C_CYAN);

    font_text("LOGIN", 600.0f, 100.0f, PX_LABEL, C_DIM);
    font_text_right(site->anonymous
                        ? "ANONYMOUS"
                        : (site->username[0] != '\0' ? site->username : "--"),
                    928.0f, 100.0f, PX_LABEL, C_WHITE);

    font_text("PASSWORD", 600.0f, 130.0f, PX_LABEL, C_DIM);
    font_text_right(site->anonymous
                        ? "NOT NEEDED"
                        : (site->password_set ? "STORED" : "NOT SET"),
                    928.0f, 130.0f, PX_LABEL, C_WHITE);

    if (sites->login_failed) {
        char footer[192];
        char fitted_footer[192];
        snprintf(footer, sizeof(footer), "LOGIN FAILED  -  %s",
                 sites->reply[0] != '\0' ? sites->reply : "NO REPLY");
        font_fit(footer, PX_MICRO, 700.0f, fitted_footer,
                 sizeof(fitted_footer));
        font_text(fitted_footer, 32.0f, 160.0f, PX_MICRO, C_YELLOW);
    } else if (site->anonymous) {
        font_text("NO PASSWORD STORED", 32.0f, 160.0f, PX_MICRO, C_DIM);
    } else {
        font_text("PASSWORDS ARE STORED UNENCRYPTED", 32.0f, 160.0f,
                  PX_MICRO, C_DIM);
    }

    font_text_right("SQUARE TO EDIT", 928.0f, 160.0f, PX_MICRO, C_DIM);
}

static void row_draw(const UiSiteList *sites, const LayoutRect *r, int index,
                     int position)
{
    const UiSiteRow *site = &sites->rows[index];
    float y = 246.0f + (float)position * 30.0f;
    int selected = index == sites->selected;

    if (selected) {
        ui_wash((float)r->x + 1.0f, y, (float)r->w - 2.0f, 30.0f, 0.06f);
        draw_rect((float)r->x + 8.0f, y, 2.0f, 30.0f, C_WHITE);
    }

    char name[128];
    font_fit(site->name, PX_LABEL, 268.0f, name, sizeof(name));
    font_text(name, (float)r->x + 32.0f, y + 8.0f, PX_LABEL, C_WHITE);

    char host[192];
    char fitted_host[192];
    snprintf(host, sizeof(host), "%s:%u", site->host, (unsigned)site->port);
    font_fit(host, PX_MICRO, 520.0f, fitted_host, sizeof(fitted_host));
    font_text(fitted_host, (float)r->x + 300.0f, y + 11.5f, PX_MICRO,
              C_CYAN_DIM);

    const char *badge = site->anonymous ? "ANON" : "LOGIN";
    font_text_right(badge, (float)(r->x + r->w) - 14.0f, y + 11.5f, PX_MICRO,
                    site->anonymous ? C_DIM : C_CYAN);
}

static void table_draw(const UiState *state, const LayoutRect *r)
{
    const UiSiteList *sites = &state->sites;

    Rect table = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(table, 0, C_CYAN);

    font_text_tracked("ALL SITES", 32.0f, 206.0f, PX_LABEL, C_DIM, 2.0f);
    if (sites->save_unavailable)
        font_text("SAVE UNAVAILABLE", 178.0f, 206.0f, PX_LABEL, C_YELLOW);

    char count[32];
    snprintf(count, sizeof(count), "%d %s", sites->count,
             sites->save_unavailable ? "IN MEMORY" : "SAVED");
    font_text_right(count, 928.0f, 209.5f, PX_MICRO, C_DIM);

    ui_hair(32.0f, 241.0f, 896.0f);

    int first = sites->first;
    if (first < 0)
        first = 0;
    if (first > sites->count - SITES_ROWS_VISIBLE)
        first = sites->count > SITES_ROWS_VISIBLE
                    ? sites->count - SITES_ROWS_VISIBLE
                    : 0;
    for (int i = first; i < sites->count && i < first + SITES_ROWS_VISIBLE;
         ++i)
        row_draw(sites, r, i, i - first);

    if (sites->count > SITES_ROWS_VISIBLE) {
        float track_h = (float)SITES_ROWS_VISIBLE * 30.0f;
        float thumb = track_h * (float)SITES_ROWS_VISIBLE /
                      (float)sites->count;
        if (thumb < 24.0f)
            thumb = 24.0f;
        float max_first = (float)(sites->count - SITES_ROWS_VISIBLE);
        float pos = (track_h - thumb) * (float)first /
                    (max_first > 0.0f ? max_first : 1.0f);
        draw_rect((float)(r->x + r->w) - 6.0f, 246.0f, 2.0f, track_h, C_HAIR);
        draw_rect((float)(r->x + r->w) - 6.0f, 246.0f + pos, 2.0f, thumb,
                  C_DIM);
    }
}

static void empty_draw(const UiState *state, const LayoutRect *r)
{
    Rect panel = {18.0f, 76.0f, 924.0f, 352.0f};
    ui_panel_frame(panel, 0, C_CYAN);

    font_text_tracked("ALL SITES", 32.0f, 84.0f, PX_LABEL, C_DIM, 2.0f);
    font_text_right(state->sites.save_unavailable ? "0 IN MEMORY" : "0 SAVED",
                    928.0f, 87.5f, PX_MICRO, C_DIM);
    ui_hair(32.0f, 119.0f, 896.0f);

    float y = 76.0f + (352.0f - 44.0f) * 0.5f - 8.0f;
    ui_center("NO SITES", 18.0f, 924.0f, y, PX_LABEL, C_DIM);
    ui_center("TRIANGLE TO ADD A SITE", 18.0f, 924.0f, y + 22.0f, PX_MICRO,
              C_DIM);
    (void)r;
}

static void dock_sites(const UiState *state)
{
    DockPanel p;
    memset(&p, 0, sizeof(p));

    const UiSiteList *sites = &state->sites;
    const UiSiteRow *site = NULL;
    if (sites->count > 0 && sites->selected >= 0 &&
        sites->selected < sites->count)
        site = &sites->rows[sites->selected];

    if (site == NULL) {
        p.word = "IDLE";
        p.word_color = C_DIM;
        p.subject = "NO SITES";
        p.left = "TRIANGLE TO ADD A SITE";
        p.left_color = C_DIM;
    } else {
        static char host[192];
        p.word = "READY";
        p.word_color = C_DIM;
        p.subject = site->name;
        snprintf(host, sizeof(host), "%s:%u", site->host,
                 (unsigned)site->port);
        p.path = host;
        p.right_a = site->anonymous ? "ANON" : "LOGIN";
        p.right_a_color = site->anonymous ? C_DIM : C_CYAN;
        p.left = "X TO CONNECT";
        p.left_color = C_DIM;
    }

    dock_panel_draw(&p);
}

static void legend_draw(void)
{
    float x = (float)LAYOUT_MARGIN;
    x = ui_keycap("X", "CONNECT", x, 510.0f);
    x = ui_keycap("SQUARE", "EDIT", x, 510.0f);
    x = ui_keycap("TRIANGLE", "NEW", x, 510.0f);
    font_text("SELECT", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("SELECT", PX_MICRO) + 8.0f;
    font_text("DELETE", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("DELETE", PX_MICRO) + 20.0f;
    x = ui_keycap("O", "CLOSE", x, 510.0f);
    font_text("START", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("START", PX_MICRO) + 6.0f;
    font_text("SERVER", x, 512.0f, PX_MICRO, C_DIM);
}

void sites_draw(const UiState *state)
{
    if (state == NULL)
        return;

    unsigned count = 0;
    const LayoutRect *r = layout_regions(LAYOUT_SITES, &count);
    if (count < 5)
        return;

    if (state->sites.count == 0)
        empty_draw(state, &r[1]);
    else {
        card_draw(state, &r[1]);
        table_draw(state, &r[2]);
    }

    dock_sites(state);
    legend_draw();
}
