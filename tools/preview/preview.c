/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "core/uistate.h"
#include "gfx/draw.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/render.h"
#include "vita2d.h"

/*
 * Host preview. Every settled state is built from a UiState and drawn through
 * the real screen layer and the software vita2d shim, then written as a PNG.
 * This is evidence that the screens render from state alone, not a golden
 * image.
 *
 * Run with tools/preview/run.sh [output-dir].
 */

static int s_files;

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data,
                      uint32_t len)
{
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, sizeof(hdr), f);
    if (len > 0)
        fwrite(data, 1, len, f);

    uint32_t c = (uint32_t)crc32(0L, Z_NULL, 0);
    c = (uint32_t)crc32(c, (const Bytef *)type, 4);
    if (len > 0)
        c = (uint32_t)crc32(c, data, len);

    uint8_t cb[4];
    be32(cb, c);
    fwrite(cb, 1, sizeof(cb), f);
}

static int write_png(const char *path)
{
    int w = preview_width();
    int h = preview_height();
    const uint32_t *fb = preview_pixels();

    size_t stride = 1 + (size_t)w * 3;
    size_t raw_len = (size_t)h * stride;
    uint8_t *raw = malloc(raw_len);
    if (raw == NULL)
        return 0;

    for (int y = 0; y < h; ++y) {
        uint8_t *row = raw + (size_t)y * stride;
        row[0] = 0;
        for (int x = 0; x < w; ++x) {
            uint32_t c = fb[(size_t)y * (size_t)w + (size_t)x];
            row[1 + x * 3 + 0] = (uint8_t)(c & 0xFF);
            row[1 + x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
            row[1 + x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
        }
    }

    uLongf comp_len = compressBound((uLong)raw_len);
    uint8_t *comp = malloc(comp_len);
    if (comp == NULL) {
        free(raw);
        return 0;
    }
    if (compress2(comp, &comp_len, raw, (uLong)raw_len, 9) != Z_OK) {
        free(raw);
        free(comp);
        return 0;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(raw);
        free(comp);
        return 0;
    }

    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(SIG, 1, sizeof(SIG), f);

    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    png_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    png_chunk(f, "IDAT", comp, (uint32_t)comp_len);
    png_chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(raw);
    free(comp);
    return 1;
}

static int layout_is_inside(void)
{
    for (int s = 0; s < LAYOUT_SURFACE_COUNT; ++s) {
        unsigned n = 0;
        const LayoutRect *r = layout_regions((LayoutSurface)s, &n);
        for (unsigned i = 0; i < n; ++i) {
            if (r[i].x < 0 || r[i].y < 0 || r[i].w < 0 || r[i].h < 0 ||
                r[i].x + r[i].w > LAYOUT_SCREEN_W ||
                r[i].y + r[i].h > LAYOUT_SCREEN_H) {
                fprintf(stderr, "layout: %s region %u escapes the field\n",
                        layout_surface_name((LayoutSurface)s), i);
                return 0;
            }
        }
    }
    return 1;
}

static const char *s_out_dir;

static void save_state(const char *name, const UiState *state)
{
    char path[1024];
    preview_clear();
    preview_trace_reset();
    ui_render(state);
    if (preview_draw_calls() == 0) {
        fprintf(stderr, "preview: %s drew nothing\n", name);
        return;
    }
    snprintf(path, sizeof(path), "%s/%s.png", s_out_dir, name);
    if (!write_png(path)) {
        fprintf(stderr, "preview: could not write %s\n", path);
        return;
    }
    s_files++;
    printf("preview: %s (%d draw calls)\n", path, preview_draw_calls());
}

static UiRow s_rows[16];
static UiMark s_local_marks[16];
static UiMark s_remote_marks[16];

static void row(int i, const char *name, UiRowType type, uint64_t size,
                int known, int risk)
{
    memset(&s_rows[i], 0, sizeof(s_rows[i]));
    snprintf(s_rows[i].name, sizeof(s_rows[i].name), "%s", name);
    s_rows[i].type = (uint8_t)type;
    s_rows[i].size = size;
    s_rows[i].size_known = known;
    s_rows[i].risk = (uint8_t)risk;
}

static void base(UiState *state)
{
    memset(state, 0, sizeof(*state));
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_local_marks, 0, sizeof(s_local_marks));
    memset(s_remote_marks, 0, sizeof(s_remote_marks));

    ui_state_init(state, s_local_marks, 16, s_remote_marks, 16);

    row(0, "..", UI_ROW_UP, 0, 0, 0);
    row(1, "VITAFTPSHELL", UI_ROW_DIR, 0, 0, 0);
    row(2, "VITASHELL", UI_ROW_DIR, 0, 0, 0);
    row(3, "ADRENALINE", UI_ROW_DIR, 0, 0, 0);
    row(4, "EMULATORS", UI_ROW_DIR, 0, 0, 0);
    row(5, "README", UI_ROW_FILE, 2458, 1, 0);
    row(6, "EMACS-29.4.TAR.XZ", UI_ROW_FILE, 124465971, 1, 0);
    row(7, "EMACS-29.3.TAR.XZ", UI_ROW_FILE, 121844121, 1, 0);
    row(8, "SHA256SUMS", UI_ROW_FILE, 1126, 1, 0);

    ui_state_set_rows(state, UI_PANE_LOCAL, s_rows, 9, 0);
    ui_state_set_rows(state, UI_PANE_REMOTE, s_rows, 9, 0);

    UiPaneState *local = &state->panes[UI_PANE_LOCAL];
    snprintf(local->path, sizeof(local->path), "ux0:data");
    snprintf(local->free_text, sizeof(local->free_text), "23.4 GB");
    local->free_known = 1;
    local->selected = 1;

    UiPaneState *remote = &state->panes[UI_PANE_REMOTE];
    remote->connected = 1;
    snprintf(remote->path, sizeof(remote->path), "/GNU/");
    snprintf(remote->site, sizeof(remote->site), "FTP.GNU.ORG");
    remote->selected = 4;

    snprintf(state->site_name, sizeof(state->site_name), "FTP.GNU.ORG");
    snprintf(state->site_host, sizeof(state->site_host), "ftp.gnu.org");
    state->site_port = 21;
    state->site_known = 1;

    state->server.run = UI_SERVER_ON;
    state->server.clients = 1;
    state->server.port = 1337;
    snprintf(state->server.address, sizeof(state->server.address),
             "192.168.1.10");
    state->server.address_known = 1;
}

static void workspace_states(void)
{
    UiState state;

    base(&state);
    state.active_pane = UI_PANE_REMOTE;
    state.dock.state = UI_DOCK_IDLE;
    snprintf(state.dock.message, sizeof(state.dock.message), "IDLE");
    state.panes[UI_PANE_REMOTE].mark_count = 2;
    snprintf(s_remote_marks[0].path.text, sizeof(s_remote_marks[0].path.text),
             "/GNU/README");
    s_remote_marks[0].size = 2458;
    s_remote_marks[0].size_known = 1;
    snprintf(s_remote_marks[1].path.text, sizeof(s_remote_marks[1].path.text),
             "/GNU/EMACS-29.4.TAR.XZ");
    s_remote_marks[1].size = 124465971;
    s_remote_marks[1].size_known = 1;
    state.panes[UI_PANE_REMOTE].marked_bytes = 124468429;
    state.panes[UI_PANE_REMOTE].marked_bytes_known = 1;
    save_state("workspace-idle", &state);

    base(&state);
    state.active_pane = UI_PANE_REMOTE;
    state.dock.state = UI_DOCK_ACTIVE;
    state.dock.active = 1;
    state.dock.direction = XFER_DIR_DOWNLOAD;
    snprintf(state.dock.name, sizeof(state.dock.name), "EMACS-29.4.TAR.XZ");
    state.dock.done = 52219084;
    state.dock.total = 124465971;
    state.dock.total_known = 1;
    state.dock.item_index = 1;
    state.dock.item_total = 2;
    state.dock.rate = 1887437;
    state.dock.rate_known = 1;
    state.dock.elapsed_ms = 27600;
    state.dock.elapsed_known = 1;
    save_state("workspace-active", &state);

    base(&state);
    state.active_pane = UI_PANE_REMOTE;
    state.dock.state = UI_DOCK_DONE;
    state.dock.direction = XFER_DIR_DOWNLOAD;
    snprintf(state.dock.name, sizeof(state.dock.name), "EMACS-29.4.TAR.XZ");
    state.dock.done = 124465971;
    state.dock.total = 124465971;
    state.dock.total_known = 1;
    state.dock.item_index = 2;
    state.dock.item_total = 2;
    state.dock.elapsed_ms = 64000;
    state.dock.elapsed_known = 1;
    state.dock.rate = 1887437;
    state.dock.rate_known = 1;
    save_state("workspace-done", &state);

    base(&state);
    state.active_pane = UI_PANE_REMOTE;
    state.dock.state = UI_DOCK_FAILED;
    state.dock.direction = XFER_DIR_DOWNLOAD;
    snprintf(state.dock.name, sizeof(state.dock.name), "EMACS-29.4.TAR.XZ");
    snprintf(state.dock.message, sizeof(state.dock.message),
             "DATA CONNECTION TIMED OUT  -  X TO RETRY");
    state.dock.done = 52219084;
    state.dock.total = 124465971;
    state.dock.total_known = 1;
    state.dock.item_index = 1;
    state.dock.item_total = 2;
    state.dock.focused = 1;
    save_state("workspace-failed", &state);

    base(&state);
    state.active_pane = UI_PANE_LOCAL;
    state.panes[UI_PANE_LOCAL].empty = 1;
    state.panes[UI_PANE_LOCAL].row_count = 0;
    state.panes[UI_PANE_LOCAL].rows = NULL;
    state.dock.state = UI_DOCK_IDLE;
    save_state("workspace-empty", &state);

    base(&state);
    state.active_pane = UI_PANE_REMOTE;
    state.panes[UI_PANE_REMOTE].loading = 1;
    state.panes[UI_PANE_REMOTE].rows = NULL;
    state.panes[UI_PANE_REMOTE].row_count = 0;
    state.dock.state = UI_DOCK_IDLE;
    save_state("workspace-reading", &state);

    base(&state);
    state.active_pane = UI_PANE_REMOTE;
    state.panes[UI_PANE_REMOTE].session_lost = 1;
    state.panes[UI_PANE_REMOTE].connected = 0;
    snprintf(state.panes[UI_PANE_REMOTE].error,
             sizeof(state.panes[UI_PANE_REMOTE].error),
             "THE SERVER CLOSED THE CONTROL CHANNEL");
    state.dock.state = UI_DOCK_IDLE;
    save_state("workspace-lost", &state);

    base(&state);
    state.active_pane = UI_PANE_LOCAL;
    state.panes[UI_PANE_REMOTE].connected = 0;
    state.panes[UI_PANE_REMOTE].session_lost = 0;
    state.dock.state = UI_DOCK_IDLE;
    save_state("workspace-no-session", &state);
}

static void site_row(UiSiteList *list, int i, const char *name,
                     const char *host, int port, int anon, int password)
{
    UiSiteRow *row = &list->rows[i];
    memset(row, 0, sizeof(*row));
    snprintf(row->name, sizeof(row->name), "%s", name);
    snprintf(row->host, sizeof(row->host), "%s", host);
    row->port = (uint16_t)port;
    row->anonymous = anon;
    row->password_set = password;
    if (!anon)
        snprintf(row->username, sizeof(row->username), "user");
}

static void index_sites(UiState *state)
{
    state->sites.count = 3;
    state->sites.selected = 1;
    site_row(&state->sites, 0, "FTP.GNU.ORG", "ftp.gnu.org", 21, 1, 0);
    site_row(&state->sites, 1, "HOME NAS", "192.168.1.20", 2121, 0, 1);
    site_row(&state->sites, 2, "ARCHIVE", "archive.example", 21, 0, 1);
}

static void sites_states(void)
{
    UiState state;
    base(&state);
    state.screen = UI_SCREEN_SITES;
    index_sites(&state);
    save_state("sites-populated", &state);

    base(&state);
    state.screen = UI_SCREEN_SITES;
    state.sites.count = 1;
    state.sites.selected = 0;
    site_row(&state.sites, 0, "FTP.GNU.ORG", "ftp.gnu.org", 21, 1, 0);
    save_state("sites-first-run", &state);

    base(&state);
    state.screen = UI_SCREEN_SITES;
    state.sites.count = 0;
    save_state("sites-empty", &state);

    base(&state);
    state.screen = UI_SCREEN_SITES;
    index_sites(&state);
    state.sites.login_failed = 1;
    snprintf(state.sites.reply, sizeof(state.sites.reply),
             "530 LOGIN INCORRECT");
    save_state("sites-login-failed", &state);

    base(&state);
    state.screen = UI_SCREEN_SITES;
    index_sites(&state);
    state.sites.save_unavailable = 1;
    save_state("sites-save-unavailable", &state);

    base(&state);
    state.screen = UI_SCREEN_SITES;
    state.sites.count = 7;
    state.sites.selected = 6;
    state.sites.first = 1;
    for (int i = 0; i < 7; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "SITE%d", i);
        site_row(&state.sites, i, name, "host.test", 21, i == 0, 0);
    }
    save_state("sites-seven", &state);
}

static void editor_states(void)
{
    UiState state;

    base(&state);
    state.screen = UI_SCREEN_SITE_EDITOR;
    snprintf(state.editor.name, sizeof(state.editor.name), "NEW SITE");
    state.editor.port = 21;
    state.editor.anonymous = 1;
    state.editor.field = UI_FIELD_NAME;
    state.editor.new_site = 1;
    state.editor.dirty = 1;
    save_state("editor-new-anonymous", &state);

    base(&state);
    state.screen = UI_SCREEN_SITE_EDITOR;
    snprintf(state.editor.name, sizeof(state.editor.name), "HOME NAS");
    snprintf(state.editor.host, sizeof(state.editor.host), "192.168.1.20");
    state.editor.port = 2121;
    state.editor.username[0] = 'u';
    state.editor.password_set = 1;
    state.editor.field = UI_FIELD_HOST;
    save_state("editor-edit-clean", &state);

    base(&state);
    state.screen = UI_SCREEN_SITE_EDITOR;
    snprintf(state.editor.name, sizeof(state.editor.name), "HOME NAS");
    snprintf(state.editor.host, sizeof(state.editor.host), "192.168.1.20");
    state.editor.port = 2121;
    state.editor.password_set = 1;
    state.editor.field = UI_FIELD_NAME;
    state.editor.dirty = 1;
    snprintf(state.editor.error, sizeof(state.editor.error),
             "NAME IS REQUIRED");
    save_state("editor-invalid", &state);

    base(&state);
    state.screen = UI_SCREEN_SITE_EDITOR;
    snprintf(state.editor.name, sizeof(state.editor.name), "HOME NAS");
    snprintf(state.editor.host, sizeof(state.editor.host), "192.168.1.20");
    state.editor.port = 2121;
    state.editor.password_set = 1;
    state.editor.anonymous = 0;
    state.editor.field = UI_FIELD_PASSWORD;
    state.editor.new_site = 1;
    state.editor.dirty = 1;
    save_state("editor-first-save", &state);
}

static void server_states(void)
{
    UiState state;

    base(&state);
    state.screen = UI_SCREEN_SERVER;
    state.server.run = UI_SERVER_OFF;
    state.server.clients = 0;
    state.server.focus_start = 1;
    snprintf(state.server.address, sizeof(state.server.address),
             "192.168.1.10");
    state.server.address_known = 1;
    save_state("server-off", &state);

    base(&state);
    state.screen = UI_SCREEN_SERVER;
    state.server.run = UI_SERVER_STARTING;
    state.server.clients = 0;
    state.server.focus_start = 1;
    save_state("server-starting", &state);

    base(&state);
    state.screen = UI_SCREEN_SERVER;
    state.server.run = UI_SERVER_ON;
    state.server.clients = 0;
    state.server.focus_start = 1;
    save_state("server-on-idle", &state);

    base(&state);
    state.screen = UI_SCREEN_SERVER;
    state.server.run = UI_SERVER_ON;
    state.server.clients = 1;
    state.server.focus_start = 1;
    state.server.client_active = 1;
    snprintf(state.server.client_addr, sizeof(state.server.client_addr),
             "192.168.1.50");
    snprintf(state.server.client_op, sizeof(state.server.client_op), "UPLOAD");
    snprintf(state.server.client_path, sizeof(state.server.client_path),
             "/UX0:/DATA/FILE.VPK");
    state.server.client_bytes = 1258292;
    state.server.client_total = 30932992;
    state.server.client_total_known = 1;
    save_state("server-on-client", &state);

    base(&state);
    state.screen = UI_SCREEN_SERVER;
    state.server.run = UI_SERVER_OFF;
    state.server.port = 1337;
    state.server.focus_start = 1;
    state.server.port_focus = 1;
    snprintf(state.server.error, sizeof(state.server.error),
             "PORT 1337 BUSY - ANOTHER SERVER MAY USE IT");
    save_state("server-bind-failed", &state);

    base(&state);
    state.screen = UI_SCREEN_SERVER;
    state.server.run = UI_SERVER_OFF;
    state.server.address[0] = '\0';
    state.server.address_known = 0;
    state.server.focus_start = 1;
    snprintf(state.server.error, sizeof(state.server.error),
             "NO NETWORK - CONNECT TO WI-FI");
    save_state("server-no-network", &state);
}

static void actions_states(void)
{
    UiState state;

    base(&state);
    state.screen = UI_SCREEN_ACTIONS;
    state.active_pane = UI_PANE_LOCAL;
    state.panes[UI_PANE_LOCAL].selected = 1;
    ui_actions_prepare(&state);
    save_state("actions-local-single", &state);

    base(&state);
    state.screen = UI_SCREEN_ACTIONS;
    state.active_pane = UI_PANE_LOCAL;
    state.panes[UI_PANE_LOCAL].selected = 1;
    state.panes[UI_PANE_LOCAL].mark_count = 2;
    snprintf(s_local_marks[0].path.text, sizeof(s_local_marks[0].path.text),
             "ux0:data/README");
    s_local_marks[0].size = 2458;
    s_local_marks[0].size_known = 1;
    snprintf(s_local_marks[1].path.text, sizeof(s_local_marks[1].path.text),
             "ux0:data/EMACS-29.4.TAR.XZ");
    s_local_marks[1].size = 124465971;
    s_local_marks[1].size_known = 1;
    state.panes[UI_PANE_LOCAL].marked_bytes = 124468429;
    state.panes[UI_PANE_LOCAL].marked_bytes_known = 1;
    ui_actions_prepare(&state);
    save_state("actions-local-multi", &state);

    base(&state);
    state.screen = UI_SCREEN_ACTIONS;
    state.active_pane = UI_PANE_REMOTE;
    state.panes[UI_PANE_REMOTE].selected = 5;
    ui_actions_prepare(&state);
    save_state("actions-remote-single", &state);

    base(&state);
    state.screen = UI_SCREEN_ACTIONS;
    state.active_pane = UI_PANE_LOCAL;
    snprintf(state.panes[UI_PANE_LOCAL].path,
             sizeof(state.panes[UI_PANE_LOCAL].path), "ux0:");
    state.panes[UI_PANE_LOCAL].selected = 0;
    ui_actions_prepare(&state);
    save_state("actions-protected", &state);
}

static UiConfirmItem s_confirm_items[12];

static void confirm_state(UiState *state, UiConfirmKind kind, const char *title,
                          const char *context, const char *warning)
{
    base(state);
    state->screen = UI_SCREEN_CONFIRM;
    UiConfirm *c = &state->confirm;
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    snprintf(c->title, sizeof(c->title), "%s", title);
    snprintf(c->context, sizeof(c->context), "%s", context);
    snprintf(c->warning, sizeof(c->warning), "%s", warning);
    c->choice_count = 2;
    c->choices[0] = UI_CHOICE_CANCEL;
    c->choices[1] = UI_CHOICE_DELETE;
}

static void confirm_states(void)
{
    UiState state;

    confirm_state(&state, UI_CONFIRM_DELETE, "DELETE 3 ITEMS", "UX0:/DATA/",
                  "THIS CANNOT BE UNDONE.");
    state.confirm.items = s_confirm_items;
    state.confirm.item_count = 3;
    s_confirm_items[0].name = "SCREENSHOTS/";
    s_confirm_items[0].size_known = 0;
    s_confirm_items[1].name = "CONFIG.TXT";
    s_confirm_items[1].size = 1229;
    s_confirm_items[1].size_known = 1;
    s_confirm_items[2].name = "NOTES.TXT";
    s_confirm_items[2].size = 340;
    s_confirm_items[2].size_known = 1;
    save_state("confirm-delete", &state);

    confirm_state(&state, UI_CONFIRM_CONFLICT, "2 DESTINATIONS EXIST",
                  "UPLOADING 5 ITEMS",
                  "THE REPLACED FILES CANNOT BE RESTORED.");
    state.confirm.choice_count = 3;
    state.confirm.choices[0] = UI_CHOICE_CANCEL;
    state.confirm.choices[1] = UI_CHOICE_SKIP;
    state.confirm.choices[2] = UI_CHOICE_OVERWRITE;
    state.confirm.item_count = 5;
    state.confirm.existing_count = 2;
    save_state("confirm-conflict", &state);

    confirm_state(&state, UI_CONFIRM_CANCEL_XFER, "CANCEL THE TRANSFER",
                  "EMACS-29.4.TAR.XZ", "THE PARTIAL FILE IS REMOVED.");
    state.confirm.choices[0] = UI_CHOICE_KEEP;
    state.confirm.choices[1] = UI_CHOICE_CANCEL;
    save_state("confirm-cancel-transfer", &state);

    confirm_state(&state, UI_CONFIRM_STOP_SERVER, "STOP THE SERVER",
                  "1 CLIENT", "THE PARTIAL FILE IS REMOVED.");
    state.confirm.choices[0] = UI_CHOICE_KEEP;
    state.confirm.choices[1] = UI_CHOICE_STOP;
    save_state("confirm-stop-server", &state);

    confirm_state(&state, UI_CONFIRM_DISCARD, "DISCARD CHANGES", "HOME NAS",
                  "");
    state.confirm.choices[0] = UI_CHOICE_KEEP;
    state.confirm.choices[1] = UI_CHOICE_DISCARD;
    save_state("confirm-discard", &state);

    confirm_state(&state, UI_CONFIRM_HIGH_RISK,
                  "WRITE INSIDE HIGH-RISK PATH", "", "");
    snprintf(state.confirm.risk_path, sizeof(state.confirm.risk_path),
             "ur0:tai/x.suprx");
    state.confirm.high_risk = 1;
    state.confirm.choices[0] = UI_CHOICE_CANCEL;
    state.confirm.choices[1] = UI_CHOICE_CONTINUE;
    save_state("confirm-high-risk", &state);

    confirm_state(&state, UI_CONFIRM_DELETE_SITE, "DELETE SITE", "HOME NAS",
                  "THIS CANNOT BE UNDONE.");
    save_state("confirm-delete-site", &state);
}

static void textentry_state(void)
{
    UiState state;

    base(&state);
    state.screen = UI_SCREEN_SITE_EDITOR;
    snprintf(state.editor.name, sizeof(state.editor.name), "HOME NAS");
    snprintf(state.editor.host, sizeof(state.editor.host), "192.168.1.20");
    state.editor.port = 2121;
    state.editor.password_set = 1;
    state.text.unavailable = 1;
    save_state("text-entry-unavailable", &state);
}

int main(int argc, char **argv)
{
    s_out_dir = argc > 1 ? argv[1] : "build/preview";

    if (!layout_is_inside())
        return 1;

    workspace_states();
    sites_states();
    editor_states();
    server_states();
    actions_states();
    confirm_states();
    textentry_state();

    if (s_files == 0) {
        fprintf(stderr, "preview: no states rendered\n");
        return 1;
    }
    printf("preview: %d states\n", s_files);
    return 0;
}
