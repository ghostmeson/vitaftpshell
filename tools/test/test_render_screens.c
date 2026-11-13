/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/uistate.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "render_trace.h"
#include "ui/render.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static UiState s_state;
static UiRow s_rows[16];
static UiMark s_local_marks[16];
static UiMark s_remote_marks[16];
static UiConfirmItem s_items[12];

static void row_init(int i, const char *name, UiRowType type, uint64_t size,
                     int size_known)
{
    memset(&s_rows[i], 0, sizeof(s_rows[i]));
    snprintf(s_rows[i].name, sizeof(s_rows[i].name), "%s", name);
    s_rows[i].type = (uint8_t)type;
    s_rows[i].size = size;
    s_rows[i].size_known = size_known;
}

static void setup(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_local_marks, 0, sizeof(s_local_marks));
    memset(s_remote_marks, 0, sizeof(s_remote_marks));

    ui_state_init(&s_state, s_local_marks, 16, s_remote_marks, 16);
    row_init(0, "..", UI_ROW_UP, 0, 0);
    row_init(1, "FILE.BIN", UI_ROW_FILE, 1024, 1);
    row_init(2, "FOLDER", UI_ROW_DIR, 0, 0);
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_rows, 3, 0);
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, s_rows, 3, 0);

    UiPaneState *local = &s_state.panes[UI_PANE_LOCAL];
    snprintf(local->path, sizeof(local->path), "ux0:data");

    UiPaneState *remote = &s_state.panes[UI_PANE_REMOTE];
    remote->connected = 1;
    snprintf(remote->path, sizeof(remote->path), "/GNU/");
    snprintf(remote->site, sizeof(remote->site), "FTP.GNU.ORG");

    snprintf(s_state.site_name, sizeof(s_state.site_name), "FTP.GNU.ORG");
    snprintf(s_state.site_host, sizeof(s_state.site_host), "ftp.gnu.org");
    s_state.site_port = 21;
    s_state.site_known = 1;
    s_state.server.port = 1337;
    trace_reset();
}

static void render(void)
{
    trace_reset();
    ui_render(&s_state);
}

static void site_row(int i, const char *name, const char *host, int anon,
                     int password_set)
{
    UiSiteRow *row = &s_state.sites.rows[i];
    memset(row, 0, sizeof(*row));
    snprintf(row->name, sizeof(row->name), "%s", name);
    snprintf(row->host, sizeof(row->host), "%s", host);
    row->port = (uint16_t)(anon ? 21 : 2121);
    row->anonymous = anon;
    row->password_set = password_set;
    if (!anon)
        snprintf(row->username, sizeof(row->username), "user");
}

static void test_sites(void)
{
    setup();
    s_state.screen = UI_SCREEN_SITES;
    s_state.sites.count = 3;
    s_state.sites.selected = 1;
    site_row(0, "FTP.GNU.ORG", "ftp.gnu.org", 1, 0);
    site_row(1, "HOME NAS", "192.168.1.20", 0, 1);
    site_row(2, "ARCHIVE", "archive.example", 0, 1);
    render();

    CHECK(trace_has_rect(18.0f, 76.0f, 924.0f, 110.0f, C_PANEL));
    CHECK(trace_has_rect(18.0f, 198.0f, 924.0f, 230.0f, C_PANEL));
    CHECK(trace_text_at("SITES", 18.0f, 42.0f));
    CHECK(trace_text_at("SELECTED SITE", 32.0f, 86.0f));
    CHECK(trace_text_at("HOME NAS", 32.0f, 100.0f));
    CHECK(trace_text_at("192.168.1.20:2121", 32.0f, 128.0f));
    CHECK(trace_text_at("LOGIN", 600.0f, 100.0f));
    CHECK(trace_text_at("PASSWORD", 600.0f, 130.0f));
    CHECK(trace_text_right("user", 928.0f, 100.0f, PX_LABEL, C_WHITE));
    CHECK(trace_text_right("STORED", 928.0f, 130.0f, PX_LABEL, C_WHITE));
    CHECK(trace_text_at("PASSWORDS ARE STORED UNENCRYPTED", 32.0f, 160.0f));
    CHECK(trace_text_right("SQUARE TO EDIT", 928.0f, 160.0f, PX_MICRO, C_DIM));
    CHECK(trace_text_at("ALL SITES", 32.0f, 206.0f));
    CHECK(trace_text_right("3 SAVED", 928.0f, 209.5f, PX_MICRO, C_DIM));
    CHECK(trace_has_rect(32.0f, 241.0f, 896.0f, 1.0f, C_HAIR));
    CHECK(trace_text_at("FTP.GNU.ORG", 50.0f, 254.0f));
    CHECK(trace_text_at("ftp.gnu.org:21", 318.0f, 257.5f));
    CHECK(trace_text_at("192.168.1.20:2121", 318.0f, 287.5f));
    CHECK(trace_text_right("LOGIN", 928.0f, 287.5f, PX_MICRO, C_CYAN));
    CHECK(!trace_has_rect_at(936.0f, 246.0f, C_HAIR));

    s_state.sites.rows[1].anonymous = 1;
    s_state.sites.rows[1].password_set = 0;
    render();
    CHECK(trace_text_at("NO PASSWORD STORED", 32.0f, 160.0f));
    CHECK(trace_text_right("NOT NEEDED", 928.0f, 130.0f, PX_LABEL, C_WHITE));

    s_state.sites.login_failed = 1;
    snprintf(s_state.sites.reply, sizeof(s_state.sites.reply),
             "530 LOGIN INCORRECT");
    render();
    CHECK(trace_has_text_prefix("LOGIN FAILED  -  "));

    setup();
    s_state.screen = UI_SCREEN_SITES;
    s_state.sites.count = 0;
    render();
    CHECK(trace_has_rect(18.0f, 76.0f, 924.0f, 352.0f, C_PANEL));
    CHECK(trace_text_at("ALL SITES", 32.0f, 84.0f));
    CHECK(trace_text_right("0 SAVED", 928.0f, 87.5f, PX_MICRO, C_DIM));
    CHECK(trace_has_text("NO SITES"));
    CHECK(trace_has_text("TRIANGLE TO ADD A SITE"));

    setup();
    s_state.screen = UI_SCREEN_SITES;
    s_state.sites.count = 7;
    s_state.sites.selected = 0;
    for (int i = 0; i < 7; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "SITE%d", i);
        site_row(i, name, "host.test", i == 0, 0);
    }
    s_state.sites.save_unavailable = 1;
    render();
    CHECK(trace_text_at_color("SAVE UNAVAILABLE", 178.0f, 206.0f, C_YELLOW));
    CHECK(trace_text_right("7 IN MEMORY", 928.0f, 209.5f, PX_MICRO, C_DIM));
    CHECK(trace_has_rect(936.0f, 246.0f, 2.0f, 180.0f, C_HAIR));
    CHECK(trace_text_at("SITE0", 50.0f, 254.0f));
    CHECK(!trace_has_text("SITE6"));

    s_state.sites.selected = 6;
    s_state.sites.first = 1;
    render();
    CHECK(!trace_text_at("SITE0", 50.0f, 254.0f));
    CHECK(trace_text_at("SITE6", 50.0f, 254.0f + 5.0f * 30.0f));
}

static void test_editor(void)
{
    setup();
    s_state.screen = UI_SCREEN_SITE_EDITOR;
    UiEditor *editor = &s_state.editor;
    snprintf(editor->name, sizeof(editor->name), "HOME NAS");
    snprintf(editor->host, sizeof(editor->host), "192.168.1.20");
    editor->port = 2121;
    editor->anonymous = 0;
    editor->password_set = 1;
    editor->username[0] = 'u';
    editor->field = UI_FIELD_PORT;
    editor->dirty = 1;
    editor->new_site = 1;
    render();

    CHECK(trace_has_rect(18.0f, 76.0f, 924.0f, 110.0f, C_PANEL));
    CHECK(trace_has_rect(18.0f, 198.0f, 924.0f, 230.0f, C_PANEL));
    CHECK(trace_text_at("SITE PREVIEW", 32.0f, 86.0f));
    CHECK(trace_text_at("HOME NAS", 32.0f, 100.0f));
    CHECK(trace_text_at("192.168.1.20:2121", 32.0f, 128.0f));
    CHECK(trace_text_right("SET", 928.0f, 130.0f, PX_LABEL, C_WHITE));
    CHECK(trace_text_at("PASSWORDS ARE STORED UNENCRYPTED", 32.0f, 160.0f));
    CHECK(trace_text_right("UNSAVED", 928.0f, 160.0f, PX_MICRO, C_WHITE));
    CHECK(trace_text_at("SITE FIELDS", 32.0f, 206.0f));
    CHECK(trace_has_rect(32.0f, 241.0f, 896.0f, 1.0f, C_HAIR));
    CHECK(trace_text_at("NAME", 50.0f, 254.0f));
    CHECK(trace_text_at("HOST", 50.0f, 284.0f));
    CHECK(trace_text_at("PORT", 50.0f, 314.0f));
    CHECK(trace_text_right("2121", 928.0f, 314.0f, PX_LABEL, C_WHITE));
    CHECK(trace_has_rect(26.0f, 306.0f, 2.0f, 30.0f, C_WHITE));
    CHECK(trace_text_at_color("PORT", 50.0f, 314.0f, C_WHITE));
    CHECK(trace_text_right("..........", 928.0f, 404.0f, PX_LABEL, C_DIM));

    editor->anonymous = 1;
    editor->password_set = 0;
    editor->dirty = 0;
    render();
    CHECK(trace_count_text_color("NOT USED", C_DIM) == 2);
    CHECK(trace_text_at("NO PASSWORD STORED", 32.0f, 160.0f));
    CHECK(trace_text_right("NO CHANGES", 928.0f, 160.0f, PX_MICRO, C_DIM));

    editor->anonymous = 0;
    editor->field = UI_FIELD_NAME;
    editor->error[0] = '\0';
    snprintf(editor->error, sizeof(editor->error), "NAME IS REQUIRED");
    render();
    CHECK(trace_text_at_color("NAME IS REQUIRED", 32.0f, 160.0f, C_YELLOW));
    CHECK(trace_text_at_color("NAME", 50.0f, 254.0f, C_YELLOW));

    /* The reducer skips the credential rows while anonymous is on. */
    editor->anonymous = 1;
    editor->field = UI_FIELD_ANONYMOUS;
    UiCommand commands[4];
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_KEY;
    event.key = UI_KEY_DOWN;
    CHECK(ui_state_handle(&s_state, &event, commands, 4) == 0);
    CHECK(s_state.editor.field == UI_FIELD_NAME);
}

static void test_actions(void)
{
    setup();
    s_state.screen = UI_SCREEN_ACTIONS;
    s_state.active_pane = UI_PANE_LOCAL;
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    ui_actions_prepare(&s_state);
    render();

    CHECK(trace_has_rect(18.0f, 76.0f, 552.0f, 352.0f, C_PANEL));
    CHECK(trace_has_rect(582.0f, 76.0f, 360.0f, 352.0f, C_PANEL));
    CHECK(trace_text_at("LOCAL ACTIONS", 32.0f, 84.0f));
    CHECK(trace_text_at("ux0:data/FILE.BIN", 32.0f, 102.0f));
    CHECK(trace_text_right("1 ITEM", 556.0f, 86.0f, PX_MICRO, C_DIM));
    CHECK(trace_text_at("COPY", 50.0f, 130.0f));
    CHECK(trace_text_at("MARK TO COPY", 50.0f, 147.0f));
    CHECK(trace_text_at("MOVE", 50.0f, 174.0f));
    CHECK(trace_text_at("MARK TO MOVE", 50.0f, 191.0f));
    CHECK(trace_text_at("DELETE", 50.0f, 350.0f));
    CHECK(trace_text_at_color("CANNOT BE UNDONE", 50.0f, 367.0f, C_YELLOW));
    CHECK(trace_text_at("TARGET", 596.0f, 84.0f));
    CHECK(trace_text_at("LOCAL", 596.0f, 108.0f));
    CHECK(trace_text_at("ux0:data", 596.0f, 126.0f));
    CHECK(trace_text_at("FILE.BIN", 596.0f, 148.0f));
    CHECK(trace_text_at("DESTINATION", 596.0f, 224.0f));

    s_state.panes[UI_PANE_LOCAL].mark_count = 2;
    snprintf(s_local_marks[0].path.text, sizeof(s_local_marks[0].path.text),
             "ux0:data/FILE.BIN");
    s_local_marks[0].size = 1024;
    s_local_marks[0].size_known = 1;
    snprintf(s_local_marks[1].path.text, sizeof(s_local_marks[1].path.text),
             "ux0:data/FOLDER");
    s_local_marks[1].size_known = 0;
    s_state.panes[UI_PANE_LOCAL].marked_bytes = 1024;
    s_state.panes[UI_PANE_LOCAL].marked_bytes_known = 1;
    ui_actions_prepare(&s_state);
    render();
    CHECK(trace_text_right("2 MARKED", 556.0f, 86.0f, PX_MICRO, C_YELLOW));
    CHECK(trace_text_at("2", 596.0f, 148.0f));
    CHECK(trace_text_at("MARKED", 596.0f, 169.0f));
    CHECK(trace_text_right("1.0 KB", 928.0f, 169.0f, PX_MICRO, C_WHITE));

    setup();
    s_state.screen = UI_SCREEN_ACTIONS;
    s_state.active_pane = UI_PANE_LOCAL;
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "*");
    static const UiRow volume_rows[] = {
        {.name = "ux0:", .type = UI_ROW_VOLUME},
    };
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, volume_rows, 1, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 0;
    ui_actions_prepare(&s_state);
    render();
    CHECK(trace_has_text("VOLUME ROOT"));
    CHECK(trace_text_at_color("VOLUME ROOT", 596.0f, 178.0f, C_YELLOW));

    setup();
    s_state.screen = UI_SCREEN_ACTIONS;
    s_state.active_pane = UI_PANE_REMOTE;
    s_state.panes[UI_PANE_REMOTE].selected = 1;
    ui_actions_prepare(&s_state);
    render();
    CHECK(trace_text_at("REMOTE ACTIONS", 32.0f, 84.0f));
    CHECK(trace_text_at("DOWNLOAD", 50.0f, 130.0f));
    CHECK(trace_text_at("TO LOCAL PANE", 50.0f, 147.0f));
    CHECK(trace_text_at("NEW FOLDER", 50.0f, 174.0f));
    CHECK(trace_text_at("RENAME", 50.0f, 218.0f));
    CHECK(trace_text_at("DELETE", 50.0f, 262.0f));
    CHECK(trace_text_at("REFRESH", 50.0f, 306.0f));
    CHECK(trace_text_at("DISCONNECT", 50.0f, 350.0f));
}

static void test_server(void)
{
    setup();
    s_state.screen = UI_SCREEN_SERVER;
    s_state.server.run = UI_SERVER_OFF;
    s_state.server.port = 1337;
    s_state.server.focus_start = 1;
    snprintf(s_state.server.address, sizeof(s_state.server.address),
             "192.168.1.10");
    s_state.server.address_known = 1;
    render();

    CHECK(trace_has_rect(18.0f, 76.0f, 552.0f, 352.0f, C_PANEL));
    CHECK(trace_has_rect(582.0f, 76.0f, 360.0f, 200.0f, C_PANEL));
    CHECK(trace_has_rect(582.0f, 288.0f, 360.0f, 140.0f, C_PANEL));
    CHECK(trace_text_at("STATUS", 32.0f, 84.0f));
    CHECK(trace_text_at_color("OFF", 32.0f, 108.0f, C_DIM));
    CHECK(trace_text_at("FTP://", 32.0f, 166.5f));
    float ip_x = 32.0f + font_width("FTP://", PX_MICRO) + 8.0f;
    CHECK(trace_text_at("192.168.1.10", ip_x, 156.0f));
    CHECK(trace_text_at(":1337",
                        ip_x + font_width("192.168.1.10", PX_VALUE) + 6.0f,
                        166.5f));
    CHECK(trace_text_at("PORT", 32.0f, 218.0f));
    CHECK(trace_text_right("1337", 538.0f, 214.0f, PX_VALUE, C_WHITE));
    CHECK(trace_text_at("CLIENTS", 32.0f, 248.0f));
    CHECK(trace_text_right("0", 538.0f, 244.0f, PX_VALUE, C_WHITE));
    CHECK(trace_text_at("ACCESS", 32.0f, 278.0f));
    CHECK(trace_text_right("READ-WRITE", 538.0f, 274.0f, PX_VALUE, C_WHITE));
    CHECK(trace_has_text("ANONYMOUS CLIENTS CAN READ"));
    CHECK(trace_has_text("AND WRITE UX0:, UR0:, AND UMA0."));
    CHECK(trace_has_text("UX0:/DATA/VITAFTPSHELL IS EXCLUDED."));
    CHECK(trace_has_text("UR0:TAI AND UX0:TAI ARE READ-ONLY."));
    CHECK(trace_has_text("UR0:TAI/BOOT_CONFIG.TXT IS DENIED."));
    CHECK(trace_has_text("THE SERVER IS OFF AT LAUNCH."));
    CHECK(trace_has_text("NO CLIENT"));
    CHECK(trace_text_at("NO CLIENT", 596.0f, 320.0f));
    CHECK(trace_text_at("ONE CLIENT AT A TIME", 596.0f, 342.0f));
    CHECK(trace_text_at_color("START SERVER", 32.0f + (488.0f -
                              font_width("START SERVER", PX_LABEL)) * 0.5f,
                              340.0f + (40.0f - 14.0f) * 0.5f, C_FIELD));
    CHECK(trace_text_at_color("IDLE", 32.0f, 450.0f, C_DIM));
    CHECK(trace_has_text("SERVER OFF"));
    CHECK(trace_has_text("PORT 1337"));
    CHECK(trace_has_text("X TO START"));
    CHECK(trace_has_text_color("OFF", C_DIM));

    s_state.server.run = UI_SERVER_STARTING;
    s_state.server.focus_start = 1;
    render();
    CHECK(trace_text_at_color("STARTING", 32.0f, 108.0f, C_YELLOW));
    CHECK(trace_has_text("CANCEL"));
    CHECK(trace_has_text("X TO CANCEL"));
    CHECK(trace_has_text_color("STARTING", C_YELLOW));

    s_state.server.run = UI_SERVER_ON;
    s_state.server.clients = 1;
    s_state.server.client_active = 1;
    s_state.server.focus_start = 1;
    snprintf(s_state.server.client_addr,
             sizeof(s_state.server.client_addr), "192.168.1.50");
    snprintf(s_state.server.client_op, sizeof(s_state.server.client_op),
             "UPLOAD");
    snprintf(s_state.server.client_path, sizeof(s_state.server.client_path),
             "/UX0:/DATA/FILE.VPK");
    s_state.server.client_bytes = 1258292;
    s_state.server.client_total = 30932992;
    s_state.server.client_total_known = 1;
    render();
    CHECK(trace_text_at_color("ON", 32.0f, 108.0f, C_GREEN));
    CHECK(trace_has_text("RUNNING"));
    CHECK(trace_has_text("1 CLIENT"));
    CHECK(trace_has_text("X TO STOP"));
    CHECK(trace_text_right("CONNECTED", 928.0f, 299.5f, PX_MICRO, C_GREEN));
    CHECK(trace_text_at("192.168.1.50", 596.0f, 320.0f));
    CHECK(trace_text_at("UPLOAD", 596.0f, 342.0f));
    CHECK(trace_has_text("1.2 MB OF 29.5 MB"));
    CHECK(trace_text_at("ONE CLIENT AT A TIME", 596.0f, 382.0f));
    CHECK(trace_has_text_color("1 CLIENT", C_WHITE));

    s_state.server.run = UI_SERVER_OFF;
    s_state.server.clients = 0;
    s_state.server.client_active = 0;
    s_state.server.port_focus = 1;
    snprintf(s_state.server.error, sizeof(s_state.server.error),
             "PORT 1337 BUSY - ANOTHER SERVER MAY USE IT");
    render();
    CHECK(trace_text_at_color("PORT 1337 BUSY - ANOTHER SERVER MAY USE IT",
                              32.0f, 190.0f, C_YELLOW));
    CHECK(trace_has_rect(26.0f, 210.0f, 2.0f, 30.0f, C_WHITE));

    setup();
    s_state.screen = UI_SCREEN_SERVER;
    s_state.server.run = UI_SERVER_OFF;
    s_state.server.port = 1337;
    s_state.server.focus_start = 1;
    s_state.server.address_known = 0;
    snprintf(s_state.server.error, sizeof(s_state.server.error),
             "NO NETWORK - CONNECT TO WI-FI");
    render();
    CHECK(trace_text_at_color("NO NETWORK - CONNECT TO WI-FI", 32.0f, 190.0f,
                              C_YELLOW));
    CHECK(trace_text_at("--", 32.0f + font_width("FTP://", PX_MICRO) + 8.0f, 156.0f));
}

static void test_confirm(void)
{
    setup();
    s_state.screen = UI_SCREEN_CONFIRM;
    UiConfirm *confirm = &s_state.confirm;
    memset(confirm, 0, sizeof(*confirm));
    confirm->kind = UI_CONFIRM_DELETE;
    snprintf(confirm->title, sizeof(confirm->title), "DELETE 3 ITEMS");
    snprintf(confirm->context, sizeof(confirm->context), "UX0:/DATA/");
    snprintf(confirm->warning, sizeof(confirm->warning),
             "THIS CANNOT BE UNDONE.");
    confirm->items = s_items;
    confirm->item_count = 3;
    s_items[0].name = "SCREENSHOTS/";
    s_items[0].size_known = 0;
    s_items[1].name = "CONFIG.TXT";
    s_items[1].size = 1229;
    s_items[1].size_known = 1;
    s_items[2].name = "NOTES.TXT";
    s_items[2].size = 340;
    s_items[2].size_known = 1;
    confirm->choices[0] = UI_CHOICE_CANCEL;
    confirm->choices[1] = UI_CHOICE_DELETE;
    confirm->choice_count = 2;
    confirm->choice_focus = 0;
    render();

    CHECK(trace_has_rect(180.0f, 122.0f, 600.0f, 300.0f, C_PANEL));
    CHECK(trace_text_at("DELETE 3 ITEMS", 210.0f, 140.0f));
    CHECK(trace_text_at("UX0:/DATA/", 210.0f, 168.0f));
    CHECK(trace_text_at("SCREENSHOTS/", 210.0f, 190.0f));
    CHECK(trace_text_at("CONFIG.TXT", 210.0f, 214.0f));
    CHECK(trace_text_at("NOTES.TXT", 210.0f, 238.0f));
    CHECK(trace_text_right("DIR", 750.0f, 190.0f, PX_MICRO, C_DIM));
    CHECK(trace_text_at("3 ITEMS - 1.5 KB KNOWN", 210.0f, 314.0f));
    CHECK(trace_text_at_color("THIS CANNOT BE UNDONE.", 210.0f, 336.0f,
                              C_YELLOW));
    CHECK(trace_has_text("CANCEL"));
    CHECK(trace_has_text("DELETE"));
    CHECK(trace_has_line(256.0f, 362.0f, 464.0f, 362.0f, C_WHITE));

    /* A delete inside a tai folder keeps the console-boot warning (R71). */
    snprintf(confirm->warning, sizeof(confirm->warning),
             "A BAD EDIT CAN STOP THE CONSOLE BOOTING. THERE IS NO UNDO.");
    snprintf(confirm->risk_path, sizeof(confirm->risk_path),
             "ur0:tai/X.SUPRX");
    confirm->high_risk = 1;
    render();
    CHECK(trace_text_at_color(
        "A BAD EDIT CAN STOP THE CONSOLE BOOTING. THERE IS NO UNDO.", 210.0f,
        336.0f, C_YELLOW));

    memset(confirm, 0, sizeof(*confirm));
    confirm->kind = UI_CONFIRM_CONFLICT;
    snprintf(confirm->title, sizeof(confirm->title),
             "2 DESTINATIONS EXIST");
    snprintf(confirm->context, sizeof(confirm->context),
             "UPLOADING 5 ITEMS");
    snprintf(confirm->warning, sizeof(confirm->warning),
             "THE REPLACED FILES CANNOT BE RESTORED.");
    confirm->item_count = 5;
    confirm->existing_count = 2;
    confirm->choices[0] = UI_CHOICE_CANCEL;
    confirm->choices[1] = UI_CHOICE_SKIP;
    confirm->choices[2] = UI_CHOICE_OVERWRITE;
    confirm->choice_count = 3;
    render();
    CHECK(trace_text_at("2 OF 5 DESTINATIONS ALREADY EXIST.", 210.0f, 196.0f));
    CHECK(trace_text_at("SKIP KEEPS THE EXISTING FILES.", 210.0f, 220.0f));
    CHECK(trace_text_at("OVERWRITE REPLACES THEM.", 210.0f, 244.0f));
    CHECK(trace_text_at_color("THE REPLACED FILES CANNOT BE RESTORED.",
                              210.0f, 336.0f, C_YELLOW));
    CHECK(trace_has_text("SKIP EXISTING"));
    CHECK(trace_has_text("OVERWRITE ALL"));
    CHECK(trace_has_text("CANCEL"));

    memset(confirm, 0, sizeof(*confirm));
    confirm->kind = UI_CONFIRM_HIGH_RISK;
    snprintf(confirm->title, sizeof(confirm->title),
             "WRITE INSIDE HIGH-RISK PATH");
    snprintf(confirm->risk_path, sizeof(confirm->risk_path),
             "ur0:tai/x.suprx");
    confirm->high_risk = 1;
    confirm->choices[0] = UI_CHOICE_CANCEL;
    confirm->choices[1] = UI_CHOICE_CONTINUE;
    confirm->choice_count = 2;
    confirm->choice_focus = 0;
    render();
    CHECK(trace_text_at("ur0:tai/x.suprx", 210.0f, 168.0f));
    CHECK(trace_text_at("A BAD EDIT CAN STOP THE CONSOLE BOOTING.", 210.0f,
                        196.0f));
    CHECK(trace_text_at_color("THERE IS NO UNDO.", 210.0f, 220.0f, C_YELLOW));
    CHECK(trace_has_text("CANCEL"));
    CHECK(trace_has_text("CONTINUE"));

    memset(confirm, 0, sizeof(*confirm));
    confirm->kind = UI_CONFIRM_CANCEL_XFER;
    snprintf(confirm->title, sizeof(confirm->title), "CANCEL THE TRANSFER");
    snprintf(confirm->context, sizeof(confirm->context), "FILE.BIN");
    confirm->choices[0] = UI_CHOICE_KEEP;
    confirm->choices[1] = UI_CHOICE_CANCEL;
    confirm->choice_count = 2;
    render();
    CHECK(trace_text_at("CANCEL THE TRANSFER", 210.0f, 140.0f));
    CHECK(trace_text_at("FILE.BIN", 210.0f, 196.0f));
    CHECK(trace_has_text("KEEP DOWNLOADING"));
    CHECK(trace_has_text("THE PARTIAL FILE IS REMOVED."));

    memset(confirm, 0, sizeof(*confirm));
    confirm->kind = UI_CONFIRM_STOP_SERVER;
    snprintf(confirm->title, sizeof(confirm->title), "STOP THE SERVER");
    snprintf(confirm->context, sizeof(confirm->context), "1 CLIENT");
    confirm->choices[0] = UI_CHOICE_KEEP;
    confirm->choices[1] = UI_CHOICE_STOP;
    confirm->choice_count = 2;
    render();
    CHECK(trace_text_at("STOP THE SERVER", 210.0f, 140.0f));
    CHECK(trace_has_text("KEEP RUNNING"));
    CHECK(trace_has_text("STOP SERVER"));
    CHECK(trace_text_at("THE WRITE IS INTERRUPTED.", 210.0f, 220.0f));

    memset(confirm, 0, sizeof(*confirm));
    confirm->kind = UI_CONFIRM_DISCARD;
    snprintf(confirm->title, sizeof(confirm->title), "DISCARD CHANGES");
    snprintf(confirm->context, sizeof(confirm->context), "HOME NAS");
    confirm->choices[0] = UI_CHOICE_KEEP;
    confirm->choices[1] = UI_CHOICE_DISCARD;
    confirm->choice_count = 2;
    render();
    CHECK(trace_text_at_color("UNSAVED CHANGES ARE LOST.", 210.0f, 196.0f,
                              C_YELLOW));
    CHECK(trace_has_text("KEEP EDITING"));
    CHECK(trace_has_text("DISCARD"));

    memset(confirm, 0, sizeof(*confirm));
    confirm->kind = UI_CONFIRM_DELETE_SITE;
    snprintf(confirm->title, sizeof(confirm->title), "DELETE SITE");
    snprintf(confirm->warning, sizeof(confirm->warning),
             "THIS CANNOT BE UNDONE.");
    confirm->choices[0] = UI_CHOICE_CANCEL;
    confirm->choices[1] = UI_CHOICE_DELETE;
    confirm->choice_count = 2;
    render();
    CHECK(trace_text_at("DELETE SITE", 210.0f, 140.0f));
    CHECK(trace_text_at("THE SITE IS REMOVED FROM THE BOOK.", 210.0f, 196.0f));
}

static void test_textentry(void)
{
    setup();
    s_state.screen = UI_SCREEN_SITE_EDITOR;
    s_state.text.unavailable = 1;
    render();
    CHECK(trace_text_at("TEXT ENTRY UNAVAILABLE", 210.0f, 200.0f));
    CHECK(trace_text_at("FIELD UNCHANGED", 210.0f, 240.0f));
    CHECK(trace_text_at("CIRCLE TO RETURN", 210.0f, 330.0f));
}

int main(void)
{
    test_sites();
    test_editor();
    test_actions();
    test_server();
    test_confirm();
    test_textentry();

    if (s_failures != 0) {
        fprintf(stderr, "test_render_screens: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_render_screens: OK\n");
    return 0;
}
