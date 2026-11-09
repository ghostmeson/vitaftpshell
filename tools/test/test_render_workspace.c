/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/uistate.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "render_trace.h"
#include "ui/dock.h"
#include "ui/render.h"
#include "ui/workspace.h"

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

static void row_init(int i, const char *name, UiRowType type, uint64_t size,
                     int size_known, int risk)
{
    memset(&s_rows[i], 0, sizeof(s_rows[i]));
    snprintf(s_rows[i].name, sizeof(s_rows[i].name), "%s", name);
    s_rows[i].type = (uint8_t)type;
    s_rows[i].size = size;
    s_rows[i].size_known = size_known;
    s_rows[i].risk = (uint8_t)risk;
}

static void setup(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_local_marks, 0, sizeof(s_local_marks));
    memset(s_remote_marks, 0, sizeof(s_remote_marks));

    ui_state_init(&s_state, s_local_marks, 16, s_remote_marks, 16);

    row_init(0, "..", UI_ROW_UP, 0, 0, 0);
    row_init(1, "VITAFTPSHELL", UI_ROW_DIR, 0, 0, 0);
    row_init(2, "A.TXT", UI_ROW_FILE, 10, 1, 0);
    for (int i = 3; i < 12; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "F%02d", i - 3);
        row_init(i, name, UI_ROW_FILE, (uint64_t)i, 1, 0);
    }

    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_rows, 12, 0);
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, s_rows, 3, 0);

    UiPaneState *local = &s_state.panes[UI_PANE_LOCAL];
    snprintf(local->path, sizeof(local->path), "ux0:data");
    snprintf(local->free_text, sizeof(local->free_text), "23.4 GB");
    local->free_known = 1;

    UiPaneState *remote = &s_state.panes[UI_PANE_REMOTE];
    remote->connected = 1;
    snprintf(remote->path, sizeof(remote->path), "/GNU/");
    snprintf(remote->site, sizeof(remote->site), "FTP.GNU.ORG");

    snprintf(s_state.site_name, sizeof(s_state.site_name), "FTP.GNU.ORG");
    snprintf(s_state.site_host, sizeof(s_state.site_host), "ftp.gnu.org");
    s_state.site_port = 21;
    s_state.site_known = 1;

    s_state.server.run = UI_SERVER_OFF;
    s_state.server.port = 1337;

    trace_reset();
}

static void render(void)
{
    trace_reset();
    ui_render(&s_state);
}

static void test_ten_rows_and_scroll(void)
{
    setup();
    for (int i = 0; i < 12; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "F%02d", i);
        row_init(i, name, UI_ROW_FILE, (uint64_t)i, 1, 0);
    }
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_rows, 12, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 0;
    s_state.panes[UI_PANE_LOCAL].scroll = 0;
    render();

    /* Ten 30 px rows from y+48; the name baseline is y+8 inside the row. */
    CHECK(trace_text_at("F00", 58.0f, 132.0f));
    CHECK(trace_text_at("F07", 58.0f, 132.0f + 7.0f * 30.0f));
    CHECK(trace_text_at("F08", 58.0f, 132.0f + 8.0f * 30.0f));
    CHECK(!trace_has_text("F10"));

    s_state.panes[UI_PANE_LOCAL].scroll = 2;
    render();
    CHECK(trace_text_at("F02", 58.0f, 132.0f));
    CHECK(trace_text_at("F09", 58.0f, 132.0f + 7.0f * 30.0f));
    CHECK(!trace_text_at("F00", 58.0f, 132.0f));
}

static void test_truncation_and_value(void)
{
    setup();
    row_init(0, "THIS IS A VERY LONG FILE NAME THAT CANNOT FIT IN THE COLUMN",
             UI_ROW_FILE, 13002343, 1, 0);
    row_init(1, "SHORT.BIN", UI_ROW_FILE, 0, 0, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 0;
    s_state.panes[UI_PANE_LOCAL].scroll = 0;
    render();

    CHECK(trace_has_text_suffix(".."));
    CHECK(trace_text_at("SHORT.BIN", 58.0f, 132.0f + 30.0f));
    CHECK(!trace_has_text("THIS IS A VERY LONG FILE NAME THAT CANNOT FIT IN THE COLUMN"));
    CHECK(trace_text_right("12.4 MB", 460.0f, 135.5f, PX_MICRO, C_WHITE));
    CHECK(trace_text_right("--", 460.0f, 165.5f, PX_MICRO, C_DIM));
}

static void test_active_pane(void)
{
    setup();
    s_state.active_pane = UI_PANE_LOCAL;
    render();
    CHECK(trace_has_rect(18.0f, 76.0f, 2.0f, 352.0f, C_YELLOW));
    CHECK(!trace_has_rect(486.0f, 76.0f, 2.0f, 352.0f, C_YELLOW));
    CHECK(trace_text_at_color("LOCAL", 32.0f, 84.0f, C_YELLOW));
    CHECK(trace_text_at_color("REMOTE", 500.0f, 84.0f, C_DIM));

    s_state.active_pane = UI_PANE_REMOTE;
    render();
    CHECK(trace_has_rect(486.0f, 76.0f, 2.0f, 352.0f, C_YELLOW));
    CHECK(!trace_has_rect(18.0f, 76.0f, 2.0f, 352.0f, C_YELLOW));
    CHECK(trace_text_at_color("REMOTE", 500.0f, 84.0f, C_YELLOW));
    CHECK(trace_text_at_color("LOCAL", 32.0f, 84.0f, C_DIM));
}

static void test_first_2000(void)
{
    setup();
    s_state.panes[UI_PANE_LOCAL].truncated = 1;
    s_state.active_pane = UI_PANE_LOCAL;
    render();

    CHECK(trace_text_at_color("FIRST 2000",
                              460.0f - font_width("FIRST 2000", PX_MICRO),
                              87.5f, C_YELLOW));

    s_state.panes[UI_PANE_LOCAL].selected = 2;
    s_state.panes[UI_PANE_LOCAL].marks = s_local_marks;
    s_state.panes[UI_PANE_LOCAL].mark_count = 1;
    snprintf(s_local_marks[0].path.text, sizeof(s_local_marks[0].path.text),
             "ux0:data/A.TXT");
    s_local_marks[0].size = 10;
    s_local_marks[0].size_known = 1;
    render();
    CHECK(trace_has_text("1 MARKED"));
    CHECK(trace_has_text_color("FIRST 2000", C_YELLOW));
}

static void test_risk_mark(void)
{
    setup();
    row_init(1, "BOOT_CONFIG.TXT", UI_ROW_FILE, 4, 1, 1);
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    render();

    /* Row 1 starts at 154; the diamond starts at x+27 and y+12. */
    CHECK(trace_has_rect_within(45.0f, 166.0f, 6.0f, 6.0f, C_YELLOW));
    CHECK(trace_text_at("BOOT_CONFIG.TXT", 58.0f, 162.0f));
    CHECK(!trace_has_rect_within(45.0f, 136.0f, 6.0f, 6.0f, C_YELLOW));
}

static void test_dock_states(void)
{
    setup();
    s_state.active_pane = UI_PANE_LOCAL;
    s_state.dock.state = UI_DOCK_IDLE;
    render();
    CHECK(trace_text_at_color("IDLE", 32.0f, 450.0f, C_DIM));
    CHECK(trace_has_text("NO MARKS"));

    memset(&s_state.dock, 0, sizeof(s_state.dock));
    s_state.dock.state = UI_DOCK_ACTIVE;
    s_state.dock.active = 1;
    s_state.dock.direction = XFER_DIR_DOWNLOAD;
    snprintf(s_state.dock.name, sizeof(s_state.dock.name), "FILE.BIN");
    s_state.dock.done = 50;
    s_state.dock.total = 100;
    s_state.dock.total_known = 1;
    s_state.dock.item_index = 1;
    s_state.dock.item_total = 2;
    s_state.dock.rate = 1024;
    s_state.dock.rate_known = 1;
    render();
    CHECK(trace_text_at_color("DOWNLOAD", 32.0f, 450.0f, C_YELLOW));
    CHECK(trace_has_text("FILE.BIN"));
    CHECK(trace_has_text("1 OF 2"));
    CHECK(trace_has_text("50 B OF 100 B"));
    CHECK(trace_has_text("50%"));
    CHECK(trace_has_text("1.0 KB/S"));

    s_state.dock.total_known = 0;
    render();
    CHECK(trace_has_text("50 B OF --"));
    CHECK(trace_has_text_color("--", C_WHITE));

    memset(&s_state.dock, 0, sizeof(s_state.dock));
    s_state.dock.state = UI_DOCK_DONE;
    s_state.dock.direction = XFER_DIR_DOWNLOAD;
    snprintf(s_state.dock.name, sizeof(s_state.dock.name), "FILE.BIN");
    s_state.dock.total = 1024;
    s_state.dock.total_known = 1;
    s_state.dock.item_index = 2;
    s_state.dock.item_total = 2;
    s_state.dock.elapsed_ms = 64000;
    s_state.dock.elapsed_known = 1;
    s_state.dock.rate = 1024;
    s_state.dock.rate_known = 1;
    render();
    CHECK(trace_text_at_color("DONE", 32.0f, 450.0f, C_GREEN));
    CHECK(trace_has_text("1.0 KB IN 00:01:04"));
    CHECK(trace_has_text("1.0 KB/S AVG"));

    memset(&s_state.dock, 0, sizeof(s_state.dock));
    s_state.dock.state = UI_DOCK_FAILED;
    s_state.dock.direction = XFER_DIR_DOWNLOAD;
    snprintf(s_state.dock.name, sizeof(s_state.dock.name), "FILE.BIN");
    snprintf(s_state.dock.message, sizeof(s_state.dock.message),
             "DATA CONNECTION TIMED OUT");
    s_state.dock.done = 50;
    s_state.dock.total = 100;
    s_state.dock.total_known = 1;
    render();
    CHECK(trace_text_at_color("FAILED", 32.0f, 450.0f, C_YELLOW));
    CHECK(trace_has_text("DATA CONNECTION TIMED OUT"));
    CHECK(trace_text_at_color("NO PARTIAL KEPT", 928.0f -
                              font_width("NO PARTIAL KEPT", PX_MICRO),
                              483.0f, C_DIM));

    setup();
    s_state.panes[UI_PANE_REMOTE].connected = 0;
    s_state.dock.state = UI_DOCK_IDLE;
    render();
    CHECK(trace_text_at_color("IDLE", 32.0f, 450.0f, C_DIM));
    CHECK(trace_has_text("REMOTE"));
    CHECK(trace_has_text("NO MARKS"));
    CHECK(trace_has_text("NO SELECTION"));
}

static void test_header(void)
{
    setup();
    s_state.server.run = UI_SERVER_ON;
    s_state.server.clients = 1;
    s_state.panes[UI_PANE_REMOTE].connected = 1;
    render();

    CHECK(trace_text_at("WORKSPACE", 18.0f, 42.0f));
    CHECK(trace_has_text("CONNECTED"));
    CHECK(trace_has_text("ftp.gnu.org"));
    CHECK(trace_has_text(":21"));
    CHECK(trace_has_text("SERVER"));
    CHECK(trace_has_text_color("ON", C_GREEN));
    CHECK(trace_has_text("1 CLIENT"));

    s_state.server.run = UI_SERVER_OFF;
    s_state.server.clients = 0;
    s_state.panes[UI_PANE_REMOTE].connected = 0;
    s_state.site_known = 0;
    render();
    CHECK(trace_has_text_color("OFF", C_DIM));
    CHECK(trace_has_text("NOT CONNECTED"));
}

static void test_regions(void)
{
    setup();
    render();

    CHECK(trace_has_rect(18.0f, 76.0f, 456.0f, 352.0f, C_PANEL));
    CHECK(trace_has_rect(486.0f, 76.0f, 456.0f, 352.0f, C_PANEL));
    CHECK(trace_has_rect(18.0f, 440.0f, 924.0f, 64.0f, C_PANEL));
    CHECK(trace_has_rect(32.0f, 119.0f, 428.0f, 1.0f, C_HAIR));
    CHECK(trace_has_rect(500.0f, 119.0f, 428.0f, 1.0f, C_HAIR));
    CHECK(trace_has_rect(18.0f, 76.0f, 456.0f, 352.0f, C_PANEL));
}

int main(void)
{
    test_ten_rows_and_scroll();
    test_truncation_and_value();
    test_active_pane();
    test_first_2000();
    test_risk_mark();
    test_dock_states();
    test_header();
    test_regions();

    if (s_failures != 0) {
        fprintf(stderr, "test_render_workspace: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_render_workspace: OK\n");
    return 0;
}
