/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/uistate.h"

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
static UiMark s_local_marks[16];
static UiMark s_remote_marks[16];
static UiMark s_local_right_marks[16];
static UiCommand s_commands[8];
static int s_command_count;

static UiRow s_local_rows[] = {
    {.name = "..", .type = UI_ROW_UP},
    {.name = "folder", .type = UI_ROW_DIR},
    {.name = "A.TXT", .type = UI_ROW_FILE, .size = 10, .size_known = 1},
};

static UiRow s_local_right_rows[] = {
    {.name = "..", .type = UI_ROW_UP},
    {.name = "RIGHTDIR", .type = UI_ROW_DIR},
    {.name = "C.TXT", .type = UI_ROW_FILE, .size = 7, .size_known = 1},
};

static UiRow s_remote_rows[] = {
    {.name = "..", .type = UI_ROW_UP},
    {.name = "REMOTE", .type = UI_ROW_DIR},
    {.name = "B.BIN", .type = UI_ROW_FILE, .size = 5, .size_known = 1},
};

static UiRow s_volume_rows[] = {
    {.name = "ux0:", .type = UI_ROW_VOLUME},
    {.name = "ur0:", .type = UI_ROW_VOLUME},
};

static UiRow s_empty_rows[] = {
    {.name = "..", .type = UI_ROW_UP},
};

static UiRow s_readonly_rows[] = {
    {.name = "..", .type = UI_ROW_UP},
    {.name = "EBOOT.BIN", .type = UI_ROW_FILE, .size = 10, .size_known = 1},
};

static UiRow s_many_rows[15];
static UiRow s_forty_rows[40];

static UiConfirmItem s_confirm_items[12];

static void reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_local_marks, 0, sizeof(s_local_marks));
    memset(s_remote_marks, 0, sizeof(s_remote_marks));
    memset(s_local_right_marks, 0, sizeof(s_local_right_marks));
    memset(s_commands, 0, sizeof(s_commands));
    s_command_count = 0;

    UiMark *marks[UI_PANE_COUNT] = {s_local_marks, s_remote_marks,
                                    s_local_right_marks};
    int caps[UI_PANE_COUNT] = {16, 16, 16};
    ui_state_init(&s_state, marks, caps);
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:data");
    snprintf(s_state.panes[UI_PANE_LOCAL_RIGHT].path,
             sizeof(s_state.panes[UI_PANE_LOCAL_RIGHT].path), "ur0:data");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_local_rows, 3, 0);
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, s_remote_rows, 3, 0);
    ui_state_set_rows(&s_state, UI_PANE_LOCAL_RIGHT, s_local_right_rows, 3,
                      0);
    s_state.panes[UI_PANE_REMOTE].connected = 1;
}

static void set_focus_for_test(UiState *state, UiFocus focus)
{
    state->focus = focus;
    state->dock.focused = focus == UI_FOCUS_DOCK;
}

static int send_key(UiKey key)
{
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_KEY;
    event.key = key;
    s_command_count = ui_state_handle(&s_state, &event, s_commands, 8);
    return s_command_count;
}

static int send_event(UiStateEvent *event)
{
    s_command_count = ui_state_handle(&s_state, event, s_commands, 8);
    return s_command_count;
}

static int send_stick(int step)
{
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_STICK;
    event.stick_y = step;
    return send_event(&event);
}

static UiCommand *command(UiCommandType type)
{
    for (int i = 0; i < s_command_count; ++i) {
        if (s_commands[i].type == type)
            return &s_commands[i];
    }
    return NULL;
}

static int mark_count(UiPane pane)
{
    return ui_state_mark_count(&s_state, pane);
}

static int visible_has(UiAction action)
{
    for (int i = 0; i < s_state.actions.visible_count; ++i) {
        if (s_state.actions.visible[i] == (int)action)
            return 1;
    }
    return 0;
}

static void test_workspace_nav(void)
{
    reset();
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.active_pane == UI_PANE_LOCAL);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 0);

    send_key(UI_KEY_DOWN);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 1);
    send_key(UI_KEY_DOWN);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 2);
    /* No transfer: the last row wraps instead of entering the dock. */
    send_key(UI_KEY_DOWN);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 0);
    CHECK(s_state.dock.focused == 0);

    s_state.dock.active = 1;
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.dock.focused == 1);
    CHECK(s_state.focus == UI_FOCUS_DOCK);
    send_key(UI_KEY_UP);
    CHECK(s_state.dock.focused == 0);
    CHECK(s_state.focus == UI_FOCUS_LIST);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 2);

    s_state.dock.focused = 1;
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.dock.focused == 0);
    CHECK(s_state.focus == UI_FOCUS_LIST);

    s_state.dock.active = 1;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.dock.focused == 1);
    send_key(UI_KEY_L1);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    CHECK(s_state.dock.focused == 0);
    CHECK(s_state.focus == UI_FOCUS_LIST);
    send_key(UI_KEY_R1);
    CHECK(s_state.active_pane == UI_PANE_LOCAL);
}

static void test_stick_selection(void)
{
    reset();
    for (int i = 0; i < 15; ++i)
        snprintf(s_many_rows[i].name, sizeof(s_many_rows[i].name), "F%02d", i);
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_many_rows, 15, 0);

    send_stick(1);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 1);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 0);

    send_stick(5);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 6);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 0);

    send_stick(5);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 11);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 2);

    send_stick(-5);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 6);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 2);

    /* A step above the window moves the view back with the cursor. */
    send_stick(-5);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 1);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 1);
}

static void test_stick_wrap(void)
{
    reset();
    for (int i = 0; i < 40; ++i)
        snprintf(s_forty_rows[i].name, sizeof(s_forty_rows[i].name), "F%02d",
                 i);
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_forty_rows, 40, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 38;
    s_state.panes[UI_PANE_LOCAL].scroll = 29;

    /* A full deflection from row 38 of 40 lands on row 3. */
    send_stick(5);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 3);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 3);

    send_stick(-5);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 38);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 29);

    /* Up on the first row wraps to the last; Down on the last wraps back. */
    s_state.panes[UI_PANE_LOCAL].selected = 0;
    s_state.panes[UI_PANE_LOCAL].scroll = 0;
    send_key(UI_KEY_UP);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 39);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 30);

    send_key(UI_KEY_DOWN);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 0);
}

static void test_dock_entry(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;

    /* With no transfer, Down on the last row wraps and never enters the
     * dock. */
    send_key(UI_KEY_DOWN);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 0);
    CHECK(s_state.focus == UI_FOCUS_LIST);
    CHECK(s_state.dock.focused == 0);

    /* A network transfer reserves the dock entry. */
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    s_state.dock.active = 1;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.focus == UI_FOCUS_DOCK);
    CHECK(s_state.dock.focused == 1);
    send_key(UI_KEY_UP);
    CHECK(s_state.focus == UI_FOCUS_LIST);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 2);

    /* A local job does too, while the network lane is idle. */
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    s_state.local_dock.active = 1;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.focus == UI_FOCUS_DOCK);

    /* A queued item in either lane keeps the dock focusable. */
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    s_state.dock.queued = 1;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.focus == UI_FOCUS_DOCK);

    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    s_state.local_dock.queued = 2;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.focus == UI_FOCUS_DOCK);

    /* The worker's batch position is the queue depth the dock reads: a
     * three-item batch starts with two items still queued. */
    reset();
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_STARTED;
    event.direction = XFER_DIR_DOWNLOAD;
    event.direction_known = 1;
    event.item_index = 1;
    event.item_total = 3;
    snprintf(event.text, sizeof(event.text), "ONE.BIN");
    send_event(&event);
    CHECK(s_state.dock.active == 1);
    CHECK(s_state.dock.queued == 2);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_DONE;
    event.item_index = 3;
    event.item_total = 3;
    send_event(&event);
    CHECK(s_state.dock.active == 0);
    CHECK(s_state.dock.queued == 0);

    /* A finished local item with another behind it is queued, not active:
     * the dock still takes the Down entry. */
    reset();
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_STARTED;
    event.direction = XFER_DIR_LOCAL_COPY;
    event.direction_known = 1;
    event.item_index = 1;
    event.item_total = 2;
    snprintf(event.text, sizeof(event.text), "ONE.TXT");
    send_event(&event);
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_DONE;
    event.direction = XFER_DIR_LOCAL_COPY;
    event.direction_known = 1;
    event.item_index = 1;
    event.item_total = 2;
    send_event(&event);
    CHECK(s_state.local_dock.active == 0);
    CHECK(s_state.local_dock.queued == 1);
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.focus == UI_FOCUS_DOCK);
}

static void test_marks(void)
{
    reset();
    send_key(UI_KEY_DOWN); /* folder */
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);
    CHECK(ui_state_is_marked(&s_state, UI_PANE_LOCAL, "ux0:data/folder"));

    send_key(UI_KEY_DOWN); /* file */
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 2);
    CHECK(s_state.panes[UI_PANE_LOCAL].marked_bytes == 10);
    CHECK(s_state.panes[UI_PANE_LOCAL].marked_bytes_known == 1);

    /* A folder is refused, never queued as one RETR/STOR (D65). */
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 0);
    CHECK(strcmp(s_state.status, "FOLDER TRANSFER NOT SUPPORTED") == 0);

    /* Drop the folder mark and transfer the file alone. */
    send_key(UI_KEY_UP);
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 1);
    CHECK(s_commands[0].type == UI_CMD_TRANSFER);
    CHECK(strcmp(s_commands[0].path, "ux0:data/A.TXT") == 0);
    CHECK(s_commands[0].value == 1);
    CHECK(s_commands[0].total == 10 && s_commands[0].total_known == 1);
    CHECK(strcmp(s_commands[0].path2, "/") == 0);

    send_key(UI_KEY_L1);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    CHECK(mark_count(UI_PANE_REMOTE) == 0);
    send_key(UI_KEY_R1);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "ux0:data/sub");
    send_event(&event);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "ur0:");
    send_event(&event);
    CHECK(mark_count(UI_PANE_LOCAL) == 0);

    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_local_rows, 3, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_BATCH_DONE;
    send_event(&event);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);

    send_key(UI_KEY_L1);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);
    CHECK(ui_state_is_marked(&s_state, UI_PANE_REMOTE, "/B.BIN"));

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_SESSION_LOST;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.text, sizeof(event.text), "CONTROL LOST");
    send_event(&event);
    CHECK(mark_count(UI_PANE_REMOTE) == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 1);

    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RECONNECT) != NULL);
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 0);
}

static void test_open_and_parent(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_CROSS);
    UiCommand *open = command(UI_CMD_OPEN_PATH);
    CHECK(open != NULL);
    CHECK(strcmp(open->path, "ux0:data/folder") == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL].loading == 1);
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL].pending_path,
                 "ux0:data/folder") == 0);

    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_CROSS);
    UiCommand *transfer = command(UI_CMD_TRANSFER);
    CHECK(transfer != NULL);
    CHECK(transfer->value == 0);
    CHECK(transfer->total == 10 && transfer->total_known == 1);
    CHECK(strcmp(transfer->path, "ux0:data/A.TXT") == 0);
    CHECK(strcmp(transfer->path2, "/") == 0);

    reset();
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_GO_PARENT) != NULL);

    reset();
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:");
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_GO_PARENT) != NULL);

    reset();
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "*");
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_GO_PARENT) == NULL);
    CHECK(strcmp(s_state.status, "AT ROOT") == 0);

    reset();
    send_key(UI_KEY_L1);
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_GO_PARENT) == NULL);
    CHECK(strcmp(s_state.status, "AT ROOT") == 0);

    reset();
    s_state.panes[UI_PANE_REMOTE].connected = 0;
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_TRANSFER) == NULL);
    CHECK(strcmp(s_state.status, "NOT CONNECTED") == 0);

    reset();
    s_state.panes[UI_PANE_REMOTE].connected = 0;
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_TRANSFER) == NULL);
    CHECK(strcmp(s_state.status, "NOT CONNECTED") == 0);
}

static void test_sites_editor(void)
{
    reset();
    s_state.sites.count = 3;
    send_key(UI_KEY_SELECT);
    CHECK(s_state.screen == UI_SCREEN_SITES);
    send_key(UI_KEY_DOWN);
    CHECK(s_state.sites.selected == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONNECT_SITE) != NULL);
    CHECK(command(UI_CMD_CONNECT_SITE)->index == 1);

    send_key(UI_KEY_SQUARE);
    CHECK(command(UI_CMD_EDIT_SITE) != NULL);
    CHECK(s_state.screen == UI_SCREEN_SITE_EDITOR);
    snprintf(s_state.editor.name, sizeof(s_state.editor.name), "Old");
    s_state.editor.dirty = 0;
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_SITES);

    send_key(UI_KEY_SQUARE);
    s_state.editor.dirty = 1;
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DISCARD);
    CHECK(s_state.confirm.choice_focus == 0);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_KEEP);
    CHECK(s_state.screen == UI_SCREEN_SITE_EDITOR);

    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    send_key(UI_KEY_RIGHT);
    CHECK(s_state.confirm.choice_focus == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_DISCARD);
    CHECK(s_state.screen == UI_SCREEN_SITES);
    CHECK(s_state.editor.dirty == 0);

    send_key(UI_KEY_TRIANGLE);
    CHECK(command(UI_CMD_NEW_SITE) != NULL);
    CHECK(s_state.screen == UI_SCREEN_SITE_EDITOR);
    CHECK(s_state.editor.port == 21);
    CHECK(s_state.editor.anonymous == 1);
    CHECK(s_state.editor.name[0] == '\0');

    s_state.editor.field = UI_FIELD_ANONYMOUS;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.editor.field != UI_FIELD_USERNAME);
    CHECK(s_state.editor.field != UI_FIELD_PASSWORD);
    s_state.editor.anonymous = 0;
    s_state.editor.field = UI_FIELD_ANONYMOUS;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.editor.field == UI_FIELD_USERNAME);

    s_state.editor.field = UI_FIELD_NAME;
    snprintf(s_state.editor.name, sizeof(s_state.editor.name), "Old");
    s_state.editor.dirty = 0;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_OPEN_IME) != NULL);
    CHECK(strcmp(command(UI_CMD_OPEN_IME)->title, "SITE NAME") == 0);
    CHECK(strcmp(command(UI_CMD_OPEN_IME)->text, "Old") == 0);
    CHECK(s_state.text.open == 1);

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_IME_RESULT;
    event.ok = 0;
    send_event(&event);
    CHECK(s_state.text.open == 0);
    CHECK(strcmp(s_state.editor.name, "Old") == 0);
    CHECK(s_state.editor.field == UI_FIELD_NAME);

    send_key(UI_KEY_CROSS);
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_IME_RESULT;
    event.ok = 1;
    snprintf(event.text, sizeof(event.text), "New");
    send_event(&event);
    CHECK(strcmp(s_state.editor.name, "New") == 0);
    CHECK(s_state.editor.dirty == 1);

    s_state.editor.name[0] = '\0';
    send_key(UI_KEY_SQUARE);
    CHECK(command(UI_CMD_SAVE_SITE) == NULL);
    CHECK(strstr(s_state.editor.error, "NAME") != NULL);

    snprintf(s_state.editor.name, sizeof(s_state.editor.name), "Site");
    snprintf(s_state.editor.host, sizeof(s_state.editor.host), "host.test");
    s_state.editor.port = 21;
    send_key(UI_KEY_SQUARE);
    CHECK(command(UI_CMD_SAVE_SITE) != NULL);

    UiStateEvent saved;
    memset(&saved, 0, sizeof(saved));
    saved.kind = UI_EVT_SITE_SAVED;
    saved.ok = 1;
    send_event(&saved);
    CHECK(s_state.editor.dirty == 0);

    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_SITES);
}

static void test_read_only_actions(void)
{
    reset();
    UiPaneState *pane = &s_state.panes[UI_PANE_LOCAL];
    snprintf(pane->path, sizeof(pane->path), "app0:");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_readonly_rows, 2, 0);
    pane->selected = 1;
    s_state.active_pane = UI_PANE_LOCAL;
    s_state.clip_mode = UI_CLIP_COPY;
    s_state.clip_count = 1;
    snprintf(s_state.clip_paths[0].text, sizeof(s_state.clip_paths[0].text),
             "ux0:data/A.TXT");

    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(s_state.actions.enabled[UI_ACTION_COPY] == 1);
    CHECK(s_state.actions.enabled[UI_ACTION_MOVE] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_MOVE], "READ-ONLY MOUNT") ==
          0);
    CHECK(s_state.actions.enabled[UI_ACTION_RENAME] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_RENAME], "READ-ONLY MOUNT") ==
          0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DELETE], "READ-ONLY MOUNT") ==
          0);
    CHECK(s_state.actions.enabled[UI_ACTION_PASTE] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_PASTE], "READ-ONLY MOUNT") ==
          0);
    CHECK(s_state.actions.enabled[UI_ACTION_NEW_FOLDER] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_NEW_FOLDER],
                 "READ-ONLY MOUNT") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 1);
}

static void test_server_actions(void)
{
    reset();
    send_key(UI_KEY_START);
    CHECK(s_state.screen == UI_SCREEN_SERVER);
    CHECK(s_state.server.focus_start == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_START_SERVER) != NULL);
    s_state.server.run = UI_SERVER_ON;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_STOP_SERVER) != NULL);
    send_key(UI_KEY_DOWN);
    CHECK(s_state.server.focus_start == 0);
    /* Cross opens text entry seeded with the live port; the edit command
     * comes back with what the IME returned, not with the current value. */
    send_key(UI_KEY_CROSS);
    const UiCommand *ime = command(UI_CMD_OPEN_IME);
    CHECK(ime != NULL);
    if (ime != NULL) {
        CHECK(ime->field == UI_FIELD_PORT);
        CHECK(ime->value == 5);
        CHECK(strcmp(ime->title, "SERVER PORT") == 0);
        CHECK(strcmp(ime->text, "1337") == 0);
    }
    CHECK(s_state.text.open == 1);
    UiStateEvent typed;
    memset(&typed, 0, sizeof(typed));
    typed.kind = UI_EVT_IME_RESULT;
    typed.ok = 1;
    snprintf(typed.text, sizeof(typed.text), "1338");
    send_event(&typed);
    CHECK(command(UI_CMD_EDIT_PORT) != NULL);
    CHECK(command(UI_CMD_EDIT_PORT)->value == 1338);
    CHECK(s_state.text.open == 0);
    CHECK(s_state.screen == UI_SCREEN_SERVER);
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(strcmp(s_state.actions.path, "ux0:data") == 0);
    /* The cursor is on ".." in ux0:data, which the protection table treats
     * as protected. Re-enable the action for this direct path check. */
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    s_state.actions.selected = UI_ACTION_DELETE;
    s_state.actions.enabled[UI_ACTION_DELETE] = 1;
    s_state.actions.has_selection = 1;
    snprintf(s_state.actions.path, sizeof(s_state.actions.path), "ux0:");
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RUN_ACTION) == NULL);
    CHECK(strcmp(s_state.status, "VOLUME ROOT") == 0);

    s_state.actions.enabled[UI_ACTION_DELETE] = 1;
    snprintf(s_state.actions.path, sizeof(s_state.actions.path),
             "ux0:data/A.TXT");
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(s_command_count == 0);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_CROSS);
    UiCommand *run = command(UI_CMD_RUN_ACTION);
    CHECK(run != NULL);
    if (run != NULL) {
        CHECK(run->action == UI_ACTION_DELETE);
        CHECK(strcmp(run->path, "ux0:data/A.TXT") == 0);
    }

    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    s_state.panes[UI_PANE_LOCAL].selected = 2; /* A.TXT */
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_COPY;
    CHECK(s_state.actions.enabled[UI_ACTION_COPY] == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RUN_ACTION) == NULL);
    CHECK(s_state.clip_mode == UI_CLIP_COPY);
    CHECK(s_state.clip_count == 1);
    CHECK(strcmp(s_state.clip_paths[0].text, "ux0:data/A.TXT") == 0);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    reset();
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.marked == 2);
    CHECK(strcmp(s_state.actions.path, "ux0:data/folder") == 0);
    s_state.actions.selected = UI_ACTION_COPY;
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 0);
    CHECK(s_state.clip_mode == UI_CLIP_COPY);
    CHECK(s_state.clip_count == 2);
    CHECK(strcmp(s_state.clip_paths[0].text, "ux0:data/folder") == 0);
    CHECK(strcmp(s_state.clip_paths[1].text, "ux0:data/A.TXT") == 0);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    /* Navigate to a different folder and paste. */
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:data/sub");
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.selected == UI_ACTION_PASTE);
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 2);
    CHECK(s_commands[0].type == UI_CMD_RUN_ACTION);
    CHECK(s_commands[1].type == UI_CMD_RUN_ACTION);
    CHECK(strcmp(s_commands[0].path, "ux0:data/folder") == 0);
    CHECK(strcmp(s_commands[1].path, "ux0:data/A.TXT") == 0);
    CHECK(strcmp(s_commands[0].path2, "ux0:data/sub") == 0);
    CHECK(s_state.clip_mode == UI_CLIP_NONE);
}

static void test_confirm(void)
{
    reset();
    for (int i = 0; i < 12; ++i) {
        s_confirm_items[i].name = "ITEM";
        s_confirm_items[i].size = 1;
        s_confirm_items[i].size_known = 1;
    }

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_CONFIRM_REQUEST;
    event.confirm = UI_CONFIRM_DELETE;
    event.items = s_confirm_items;
    event.item_count = 12;
    send_event(&event);

    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(s_state.confirm.item_scroll == 0);
    CHECK(s_state.confirm.choice_focus == 0);

    for (int i = 0; i < 12; ++i)
        send_key(UI_KEY_DOWN);
    CHECK(s_state.confirm.item_scroll == 7);
    for (int i = 0; i < 12; ++i)
        send_key(UI_KEY_UP);
    CHECK(s_state.confirm.item_scroll == 0);

    send_key(UI_KEY_RIGHT);
    CHECK(s_state.confirm.choice_focus == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->confirm == UI_CONFIRM_DELETE);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_DELETE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_CONFIRM_REQUEST;
    event.confirm = UI_CONFIRM_DELETE;
    event.items = s_confirm_items;
    event.item_count = 12;
    send_event(&event);
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CANCEL);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
}

static void test_high_risk_action(void)
{
    reset();
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_DELETE;
    s_state.actions.enabled[UI_ACTION_DELETE] = 1;
    s_state.actions.has_selection = 1;
    snprintf(s_state.actions.path, sizeof(s_state.actions.path),
             "ur0:tai/x.suprx");
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(s_state.confirm.item_count == 1);
    CHECK(strcmp(s_state.confirm.items[0].name, "ur0:tai/x.suprx") == 0);
    CHECK(s_state.confirm_from_worker == 0);
    CHECK(s_state.confirm.choice_focus == 0);
    send_key(UI_KEY_RIGHT);
    CHECK(s_state.confirm.choice_focus == 1);
    send_key(UI_KEY_CROSS);
    UiCommand *run = command(UI_CMD_RUN_ACTION);
    CHECK(run != NULL);
    if (run != NULL) {
        CHECK(run->action == UI_ACTION_DELETE);
        CHECK(run->value == 0);
        CHECK(strcmp(run->path, "ur0:tai/x.suprx") == 0);
    }
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
}

static void test_action_ime_and_risk(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_RENAME;
    s_state.actions.enabled[UI_ACTION_RENAME] = 1;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_OPEN_IME) != NULL);
    CHECK(strcmp(command(UI_CMD_OPEN_IME)->title, "RENAME") == 0);
    CHECK(strcmp(command(UI_CMD_OPEN_IME)->text, "folder") == 0);
    CHECK(s_state.pending_action_valid == 1);

    UiStateEvent ime;
    memset(&ime, 0, sizeof(ime));
    ime.kind = UI_EVT_IME_RESULT;
    ime.ok = 1;
    snprintf(ime.text, sizeof(ime.text), "RENAMED");
    send_event(&ime);
    UiCommand *run = command(UI_CMD_RUN_ACTION);
    CHECK(run != NULL);
    if (run != NULL) {
        CHECK(run->action == UI_ACTION_RENAME);
        CHECK(strcmp(run->path, "ux0:data/folder") == 0);
        CHECK(strcmp(run->path2, "ux0:data") == 0);
        CHECK(strcmp(run->text, "RENAMED") == 0);
    }
    CHECK(s_state.pending_action_valid == 0);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_COPY;
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.clip_mode == UI_CLIP_COPY);
    CHECK(s_state.clip_count == 2);

    /* Navigate into a high-risk destination, then paste. */
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ur0:tai");
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.selected == UI_ACTION_PASTE);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_command_count == 2);
    for (int i = 0; i < s_command_count; ++i) {
        CHECK(s_commands[i].type == UI_CMD_RUN_ACTION);
        CHECK(s_commands[i].action == UI_ACTION_COPY);
        CHECK(s_commands[i].value == 0);
        CHECK(strcmp(s_commands[i].path2, "ur0:tai") == 0);
    }
    CHECK(s_state.clip_mode == UI_CLIP_NONE);
}

static void test_local_actions_inside_volume_root(void)
{
    reset();
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_local_rows, 3, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 1; /* folder, not ".." */
    ui_actions_prepare(&s_state);

    CHECK(strcmp(s_state.actions.path, "ux0:folder") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 1);
    CHECK(s_state.actions.enabled[UI_ACTION_RENAME] == 1);
    CHECK(s_state.actions.enabled[UI_ACTION_NEW_FOLDER] == 1);

    s_state.panes[UI_PANE_LOCAL].selected = 0; /* ".." is no selection */
    ui_actions_prepare(&s_state);
    CHECK(s_state.actions.has_selection == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DELETE], "NO SELECTION") == 0);

    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "*");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_volume_rows, 2, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 0;
    ui_actions_prepare(&s_state);
    CHECK(s_state.actions.has_selection == 1);
    CHECK(strcmp(s_state.actions.path, "ux0:") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_NEW_FOLDER] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_NEW_FOLDER], "ROOT") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DELETE], "VOLUME ROOT") == 0);
}

static void test_actions_no_selection(void)
{
    reset();
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:data/EMPTY");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_empty_rows, 1, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 0;
    ui_actions_prepare(&s_state);

    CHECK(s_state.actions.has_selection == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_NEW_FOLDER] == 1);
    CHECK(s_state.actions.enabled[UI_ACTION_COPY] == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_MOVE] == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_RENAME] == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DELETE], "NO SELECTION") ==
          0);

    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    s_state.actions.selected = UI_ACTION_DELETE;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RUN_ACTION) == NULL);
    CHECK(strcmp(s_state.status, "ACTION UNAVAILABLE") == 0);

    s_state.actions.selected = UI_ACTION_NEW_FOLDER;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_OPEN_IME) != NULL);
}

static void test_marks_pruned_after_delete(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2; /* A.TXT */
    send_key(UI_KEY_SQUARE);
    CHECK(ui_state_mark_count(&s_state, UI_PANE_LOCAL) == 1);

    /* The refreshed listing no longer has A.TXT. */
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_local_rows, 2, 0);
    CHECK(ui_state_mark_count(&s_state, UI_PANE_LOCAL) == 0);

    /* A mark in another folder survives a listing of this folder. */
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_SQUARE);
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:data/sub");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_empty_rows, 1, 0);
    CHECK(ui_state_mark_count(&s_state, UI_PANE_LOCAL) == 1);
}

/* A reload of the folder in place puts the cursor back on the same name, not
 * the same row index, and clamps when that name is gone (R9, KTD7). */
static void test_reload_cursor_by_name(void)
{
    reset();
    UiPaneState *pane = &s_state.panes[UI_PANE_LOCAL];
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "ux0:data");

    /* The cursor sits on A.TXT at index 2; the reload moves it to index 3. */
    pane->selected = 2;
    send_event(&event);
    CHECK(pane->loading == 1);
    UiRow moved[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "folder", .type = UI_ROW_DIR},
        {.name = "MORE", .type = UI_ROW_DIR},
        {.name = "A.TXT", .type = UI_ROW_FILE, .size = 10, .size_known = 1},
    };
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, moved, 4, 0);
    CHECK(pane->selected == 3);
    CHECK(strcmp(pane->rows[pane->selected].name, "A.TXT") == 0);

    /* A name that is gone keeps the row index and clamps to the new list. */
    pane->selected = 3;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "ux0:data");
    send_event(&event);
    UiRow shorter[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "folder", .type = UI_ROW_DIR},
    };
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, shorter, 2, 0);
    CHECK(pane->selected == 1);
    CHECK(strcmp(pane->rows[pane->selected].name, "folder") == 0);
}

/* A reload prunes the marks whose names no longer exist and keeps the rest
 * (R9). */
static void test_reload_prunes_marks_by_name(void)
{
    reset();
    UiPaneState *pane = &s_state.panes[UI_PANE_LOCAL];
    pane->selected = 2; /* A.TXT */
    send_key(UI_KEY_SQUARE);
    pane->selected = 1; /* folder */
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 2);

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "ux0:data");
    send_event(&event);

    UiRow without[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "folder", .type = UI_ROW_DIR},
    };
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, without, 2, 0);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);
    CHECK(ui_state_is_marked(&s_state, UI_PANE_LOCAL, "ux0:data/folder"));
    CHECK(!ui_state_is_marked(&s_state, UI_PANE_LOCAL, "ux0:data/A.TXT"));
    CHECK(pane->selected == 1);
}

/* A listing that failed on a folder that is gone is replaced by the ALL DRIVES
 * root and one reason. The reducer must land there instead of keeping a
 * LISTING FAILED on the dead path (R9, AE14). */
static void test_vanished_folder_resets_to_root(void)
{
    reset();
    UiPaneState *pane = &s_state.panes[UI_PANE_LOCAL];
    UiStateEvent event;

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "uma0:games");
    send_event(&event);
    CHECK(strcmp(pane->path, "uma0:games") == 0);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_END;
    event.pane = UI_PANE_LOCAL;
    event.ok = 0;
    snprintf(event.text, sizeof(event.text), "NOT FOUND");
    send_event(&event);
    CHECK(strcmp(pane->path, "uma0:games") == 0);
    CHECK(strcmp(pane->error, "NOT FOUND") == 0);

    /* The worker's fallback: the root listing, then the reason. */
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "*");
    send_event(&event);
    CHECK(strcmp(pane->path, "*") == 0);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_SET_ROWS;
    event.pane = UI_PANE_LOCAL;
    event.rows = s_volume_rows;
    event.row_count = 2;
    send_event(&event);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_END;
    event.pane = UI_PANE_LOCAL;
    event.ok = 1;
    send_event(&event);
    CHECK(pane->error[0] == '\0');
    CHECK(pane->row_count == 2);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_ERROR;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.text, sizeof(event.text), "FOLDER GONE");
    send_event(&event);
    CHECK(strcmp(pane->path, "*") == 0);
    CHECK(strcmp(pane->error, "FOLDER GONE") == 0);
    CHECK(strcmp(s_state.status, "FOLDER GONE") == 0);
}

static void test_action_returns_to_workspace(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    s_state.actions.selected = UI_ACTION_COPY;
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(command(UI_CMD_RUN_ACTION) == NULL);
    CHECK(s_state.clip_mode == UI_CLIP_COPY);
    CHECK(s_state.clip_count == 1);

    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:data/sub");
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.selected == UI_ACTION_PASTE);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(command(UI_CMD_RUN_ACTION) != NULL);

    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 1);
    s_state.actions.selected = UI_ACTION_DELETE;
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(command(UI_CMD_RUN_ACTION) != NULL);
}

static void test_delete_confirm_items(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_DELETE;
    s_state.actions.enabled[UI_ACTION_DELETE] = 1;
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(s_state.confirm.item_count == 2);
    CHECK(strcmp(s_state.confirm.title, "DELETE 2 ITEMS") == 0);
    CHECK(s_state.confirm.items != NULL);
    CHECK(strcmp(s_state.confirm.items[0].name, "ux0:data/folder") == 0);
    CHECK(strcmp(s_state.confirm.items[1].name, "ux0:data/A.TXT") == 0);
}

static void test_sites_scroll(void)
{
    reset();
    s_state.sites.count = 7;
    for (int i = 0; i < 7; ++i) {
        snprintf(s_state.sites.rows[i].name,
                 sizeof(s_state.sites.rows[i].name), "SITE%d", i);
    }
    send_key(UI_KEY_SELECT);
    for (int i = 0; i < 6; ++i)
        send_key(UI_KEY_DOWN);
    CHECK(s_state.sites.selected == 6);
    CHECK(s_state.sites.first == 1);

    /* The seventh Down wraps and resets the window to the top. */
    send_key(UI_KEY_DOWN);
    CHECK(s_state.sites.selected == 0);
    CHECK(s_state.sites.first == 0);

    /* Up on the first site wraps to the last and shows the last window. */
    send_key(UI_KEY_UP);
    CHECK(s_state.sites.selected == 6);
    CHECK(s_state.sites.first == 1);
}

static void test_sites_stick(void)
{
    reset();
    s_state.sites.count = 8;
    for (int i = 0; i < 8; ++i) {
        snprintf(s_state.sites.rows[i].name,
                 sizeof(s_state.sites.rows[i].name), "SITE%d", i);
    }
    send_key(UI_KEY_SELECT);
    for (int i = 0; i < 7; ++i)
        send_key(UI_KEY_DOWN);
    CHECK(s_state.sites.selected == 7);
    CHECK(s_state.sites.first == 2);

    /* The stick moves by its step, wraps, and resets the window. */
    send_stick(1);
    CHECK(s_state.sites.selected == 0);
    CHECK(s_state.sites.first == 0);

    send_stick(-1);
    CHECK(s_state.sites.selected == 7);
    CHECK(s_state.sites.first == 2);

    send_stick(-5);
    CHECK(s_state.sites.selected == 2);
    CHECK(s_state.sites.first == 2);

    send_stick(5);
    CHECK(s_state.sites.selected == 7);
    CHECK(s_state.sites.first == 2);

    send_stick(5);
    CHECK(s_state.sites.selected == 4);
    CHECK(s_state.sites.first == 0);
}

/* R25: only the workspace lists and the site book wrap. ACTIONS, CONFIRM, and
 * the site editor keep their clamped or field-cycling navigation, and a stick
 * event cannot move them. */
static void test_stick_scope(void)
{
    reset();
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    int first_action = s_state.actions.visible[0];
    int last_action =
        s_state.actions.visible[s_state.actions.visible_count - 1];
    send_stick(5);
    CHECK(s_state.actions.selected == first_action);
    send_key(UI_KEY_UP);
    CHECK(s_state.actions.selected == first_action);
    s_state.actions.selected = last_action;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.actions.selected == last_action);

    reset();
    for (int i = 0; i < 12; ++i) {
        s_confirm_items[i].name = "ITEM";
        s_confirm_items[i].size = 1;
        s_confirm_items[i].size_known = 1;
    }
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_CONFIRM_REQUEST;
    event.confirm = UI_CONFIRM_DELETE;
    event.items = s_confirm_items;
    event.item_count = 12;
    send_event(&event);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    send_stick(-5);
    CHECK(s_state.confirm.choice_focus == 0);
    CHECK(s_state.confirm.item_scroll == 0);
    send_key(UI_KEY_LEFT);
    CHECK(s_state.confirm.choice_focus == 0);
    for (int i = 0; i < 12; ++i)
        send_key(UI_KEY_DOWN);
    CHECK(s_state.confirm.item_scroll == 7);
    send_key(UI_KEY_DOWN);
    CHECK(s_state.confirm.item_scroll == 7);

    reset();
    s_state.sites.count = 1;
    send_key(UI_KEY_SELECT);
    send_key(UI_KEY_SQUARE);
    CHECK(s_state.screen == UI_SCREEN_SITE_EDITOR);
    int field = s_state.editor.field;
    send_stick(5);
    CHECK(s_state.editor.field == field);
    send_key(UI_KEY_DOWN);
    CHECK(s_state.editor.field != field);
}

static void test_confirm_busy_guard(void)
{
    reset();
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_CONFIRM_REQUEST;
    event.confirm = UI_CONFIRM_DELETE;
    event.items = s_confirm_items;
    event.item_count = 3;
    send_event(&event);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_CONFIRM_REQUEST;
    event.confirm = UI_CONFIRM_CONFLICT;
    send_event(&event);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(strcmp(s_state.status, "CONFIRM BUSY") == 0);

    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
}

static void test_path_busy_prompt(void)
{
    reset();
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_CONFIRM_REQUEST;
    event.confirm = UI_CONFIRM_PATH_BUSY;
    event.items = s_confirm_items;
    event.item_count = 1;
    s_confirm_items[0].name = "ux0:data/a.bin";
    send_event(&event);

    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_PATH_BUSY);
    CHECK(s_state.confirm.choices[0] == UI_CHOICE_WAIT);
    CHECK(s_state.confirm.choices[1] == UI_CHOICE_SKIP);
    CHECK(s_state.confirm.choices[2] == UI_CHOICE_CANCEL);
    CHECK(s_state.confirm.choice_focus == 0);

    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->confirm == UI_CONFIRM_PATH_BUSY);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_WAIT);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    reset();
    send_event(&event);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_SKIP);

    reset();
    send_event(&event);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CANCEL);
}

/* R16/R17/KTD6: the same-folder paste is its own prompt. The safe answer is
 * focused first, the list is the items already here, and the destination folder
 * is the context line. */
static void test_duplicate_prompt(void)
{
    reset();
    s_confirm_items[0].name = "ux0:data/A.TXT";
    s_confirm_items[0].size = 3;
    s_confirm_items[0].size_known = 1;

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_CONFIRM_REQUEST;
    event.confirm = UI_CONFIRM_DUPLICATE;
    event.pane = UI_PANE_LOCAL;
    event.items = s_confirm_items;
    event.item_count = 1;
    event.existing_count = 0;
    snprintf(event.text, sizeof(event.text), "ux0:data");
    send_event(&event);

    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DUPLICATE);
    CHECK(strcmp(s_state.confirm.title, "ITEMS ALREADY HERE") == 0);
    CHECK(strcmp(s_state.confirm.context, "ux0:data") == 0);
    CHECK(strcmp(s_state.confirm.warning,
                 "SAVE DUPLICATES KEEPS THE ORIGINAL.") == 0);
    CHECK(s_state.confirm.choices[0] == UI_CHOICE_CANCEL);
    CHECK(s_state.confirm.choices[1] == UI_CHOICE_SAVE_DUPLICATE);
    CHECK(s_state.confirm.choice_count == 2);
    CHECK(s_state.confirm.choice_focus == 0);
    CHECK(s_state.confirm.item_count == 1);

    /* Cross on the focused answer is CANCEL: the paste changes nothing. */
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->confirm == UI_CONFIRM_DUPLICATE);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CANCEL);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    /* Right reaches SAVE DUPLICATES; Circle is still CANCEL. */
    reset();
    send_event(&event);
    CHECK(s_state.confirm.choice_focus == 0);
    send_key(UI_KEY_RIGHT);
    CHECK(s_state.confirm.choice_focus == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->confirm == UI_CONFIRM_DUPLICATE);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_SAVE_DUPLICATE);

    reset();
    send_event(&event);
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CANCEL);
}

/* D73: duplicate destination names are refused before any command, and a move
 * source inside another is skipped. */
/* U27: each lane keeps its own dock; both running shows the network lane full
 * and gives the dock cancel a three-choice prompt. */
static void test_two_lane_dock(void)
{
    reset();
    UiStateEvent net;
    memset(&net, 0, sizeof(net));
    net.kind = UI_EVT_TRANSFER_STARTED;
    net.direction = XFER_DIR_DOWNLOAD;
    net.direction_known = 1;
    net.total = 100;
    net.total_known = 1;
    snprintf(net.text, sizeof(net.text), "FILE.BIN");
    send_event(&net);
    CHECK(s_state.dock.active == 1);
    CHECK(s_state.local_dock.active == 0);
    CHECK(s_state.dock_primary == UI_DOCK_LANE_NETWORK);

    UiStateEvent local;
    memset(&local, 0, sizeof(local));
    local.kind = UI_EVT_TRANSFER_STARTED;
    local.direction = XFER_DIR_LOCAL_COPY;
    local.direction_known = 1;
    local.total = 50;
    local.total_known = 1;
    local.item_index = 1;
    local.item_total = 2;
    snprintf(local.text, sizeof(local.text), "LOCAL.BIN");
    send_event(&local);
    CHECK(s_state.local_dock.active == 1);
    CHECK(s_state.local_shown == 1);
    CHECK(s_state.dock_primary == UI_DOCK_LANE_NETWORK);

    UiStateEvent progress;
    memset(&progress, 0, sizeof(progress));
    progress.kind = UI_EVT_TRANSFER_PROGRESS;
    progress.direction = XFER_DIR_LOCAL_COPY;
    progress.direction_known = 1;
    progress.bytes = 10;
    progress.total = 50;
    progress.total_known = 1;
    send_event(&progress);
    CHECK(s_state.local_dock.done == 10);
    CHECK(s_state.dock.done == 0);

    UiStateEvent done;
    memset(&done, 0, sizeof(done));
    done.kind = UI_EVT_TRANSFER_DONE;
    done.direction = XFER_DIR_DOWNLOAD;
    done.direction_known = 1;
    done.bytes = 100;
    done.total = 100;
    done.total_known = 1;
    send_event(&done);
    CHECK(s_state.dock.active == 0);
    CHECK(s_state.local_dock.active == 1);
    CHECK(s_state.dock_primary == UI_DOCK_LANE_LOCAL);

    /* A successful local rename can carry a debris notice. */
    memset(&done, 0, sizeof(done));
    done.kind = UI_EVT_TRANSFER_DONE;
    done.direction = XFER_DIR_LOCAL_RENAME;
    done.direction_known = 1;
    done.notice = 1;
    snprintf(done.text, sizeof(done.text),
             "ASIDE CLEANUP FAILED - OLD COPY KEPT AS NEW.TXT.VFTP.OLD");
    send_event(&done);
    CHECK(s_state.local_dock.state == UI_DOCK_DONE);
    CHECK(s_state.local_dock.notice == 1);
    CHECK(strstr(s_state.local_dock.message, "ASIDE CLEANUP FAILED") != NULL);

    /* The next failure must not wear the old notice. */
    memset(&done, 0, sizeof(done));
    done.kind = UI_EVT_TRANSFER_FAILED;
    done.direction = XFER_DIR_LOCAL_RENAME;
    done.direction_known = 1;
    snprintf(done.text, sizeof(done.text), "RENAME FAILED");
    send_event(&done);
    CHECK(s_state.local_dock.notice == 0);
    CHECK(s_state.local_dock.state == UI_DOCK_FAILED);

    /* Both active again: the dock prompt offers two lanes. */
    s_state.dock.active = 1;
    s_state.dock.state = UI_DOCK_ACTIVE;
    s_state.local_dock.active = 1;
    s_state.local_dock.state = UI_DOCK_ACTIVE;
    set_focus_for_test(&s_state, UI_FOCUS_DOCK);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_CANCEL_LANES);
    CHECK(strstr(s_state.confirm.warning, "CANCELLED TRANSFERS REMOVE") !=
          NULL);
    CHECK(s_state.confirm.choices[0] == UI_CHOICE_KEEP);
    CHECK(s_state.confirm.choices[1] == UI_CHOICE_CANCEL_NETWORK);
    CHECK(s_state.confirm.choices[2] == UI_CHOICE_CANCEL_LOCAL);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CANCEL_NETWORK) != NULL);

    reset();
    s_state.dock.active = 1;
    s_state.dock.state = UI_DOCK_ACTIVE;
    s_state.local_dock.active = 1;
    s_state.local_dock.state = UI_DOCK_ACTIVE;
    set_focus_for_test(&s_state, UI_FOCUS_DOCK);
    send_key(UI_KEY_CROSS);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CANCEL_LOCAL) != NULL);
}

/* A waiting local lane is a live lane: a solo waiter opens the two-choice
 * cancel instead of being invisible. */
static void test_waiting_lane_cancel(void)
{
    reset();
    s_state.local_shown = 1;
    s_state.local_dock.waiting = 1;
    s_state.dock.active = 0;
    s_state.dock.state = UI_DOCK_IDLE;
    s_state.dock_primary = UI_DOCK_LANE_LOCAL;
    set_focus_for_test(&s_state, UI_FOCUS_DOCK);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_CANCEL_XFER);
    CHECK(strstr(s_state.confirm.warning, "TRANSFERRED FILE IS REMOVED") !=
          NULL);
    CHECK(s_state.confirm.choices[1] == UI_CHOICE_CANCEL);

    /* A waiting lane beside a live network lane offers both lanes. */
    reset();
    s_state.local_shown = 1;
    s_state.local_dock.waiting = 1;
    s_state.dock.active = 1;
    s_state.dock.state = UI_DOCK_ACTIVE;
    set_focus_for_test(&s_state, UI_FOCUS_DOCK);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.confirm.kind == UI_CONFIRM_CANCEL_LANES);
    CHECK(s_state.confirm.choices[2] == UI_CHOICE_CANCEL_LOCAL);

    /* A network PATH BUSY answer marks the network lane waiting, not local. */
    reset();
    static const UiConfirmItem blocker = {"ux0:data/HELD.BIN", 0, 0, 0};
    UiStateEvent request;
    memset(&request, 0, sizeof(request));
    request.kind = UI_EVT_CONFIRM_REQUEST;
    request.confirm = UI_CONFIRM_PATH_BUSY;
    request.pane = UI_PANE_REMOTE;
    request.items = &blocker;
    request.item_count = 1;
    send_event(&request);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_PATH_BUSY);
    send_key(UI_KEY_CROSS); /* WAIT */
    CHECK(s_state.dock.waiting == 1);
    CHECK(s_state.local_dock.waiting == 0);
    CHECK(s_state.dock_primary == UI_DOCK_LANE_NETWORK);

    /* A worker prompt's identity rides the answer back to the worker. */
    reset();
    static const UiConfirmItem one = {"ux0:data/A.TXT", 0, 0, 0};
    UiStateEvent identified;
    memset(&identified, 0, sizeof(identified));
    identified.kind = UI_EVT_CONFIRM_REQUEST;
    identified.confirm = UI_CONFIRM_DELETE;
    identified.prompt_id = 42;
    identified.items = &one;
    identified.item_count = 1;
    send_event(&identified);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    send_key(UI_KEY_CROSS);
    UiCommand *answer = command(UI_CMD_CONFIRM);
    CHECK(answer != NULL);
    CHECK(answer->prompt_id == 42);
}

static void test_paste_guards(void)
{
    reset();
    s_state.screen = UI_SCREEN_ACTIONS;
    s_state.focus = UI_FOCUS_ACTIONS;
    s_state.actions.selected = UI_ACTION_PASTE;
    s_state.actions.enabled[UI_ACTION_PASTE] = 1;
    s_state.actions.visible[0] = UI_ACTION_PASTE;
    s_state.actions.visible_count = 1;
    s_state.clip_mode = UI_CLIP_COPY;
    s_state.clip_count = 2;
    snprintf(s_state.clip_paths[0].text, sizeof(s_state.clip_paths[0].text),
             "ux0:one/x.txt");
    snprintf(s_state.clip_paths[1].text, sizeof(s_state.clip_paths[1].text),
             "ux0:two/x.txt");

    CHECK(send_key(UI_KEY_CROSS) == 0);
    CHECK(strcmp(s_state.status, "SAME DESTINATION") == 0);
    CHECK(s_state.clip_mode == UI_CLIP_COPY);

    reset();
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:dest");
    s_state.screen = UI_SCREEN_ACTIONS;
    s_state.focus = UI_FOCUS_ACTIONS;
    s_state.actions.selected = UI_ACTION_PASTE;
    s_state.actions.enabled[UI_ACTION_PASTE] = 1;
    s_state.actions.visible[0] = UI_ACTION_PASTE;
    s_state.actions.visible_count = 1;
    snprintf(s_state.actions.dest, sizeof(s_state.actions.dest), "ux0:dest");
    s_state.clip_mode = UI_CLIP_MOVE;
    s_state.clip_count = 2;
    snprintf(s_state.clip_paths[0].text, sizeof(s_state.clip_paths[0].text),
             "ux0:data/folder");
    snprintf(s_state.clip_paths[1].text, sizeof(s_state.clip_paths[1].text),
             "ux0:data/folder/A.TXT");

    CHECK(send_key(UI_KEY_CROSS) == 1);
    CHECK(s_commands[0].action == UI_ACTION_MOVE);
    CHECK(strcmp(s_commands[0].path, "ux0:data/folder") == 0);
    CHECK(s_commands[0].batch_total == 1);
    CHECK(s_commands[0].batch_last == 1);
}

static void test_dock_cancel_and_site_delete(void)
{
    reset();
    s_state.dock.active = 1;
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_DOWN);
    CHECK(s_state.dock.focused == 1);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_CANCEL_XFER);
    CHECK(s_command_count == 0);
    send_key(UI_KEY_RIGHT);
    CHECK(s_state.confirm.choice_focus == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CANCEL_TRANSFER) != NULL);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.dock.focused == 0);

    s_state.dock.active = 1;
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_CROSS);
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_KEEP);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.dock.focused == 0);

    reset();
    s_state.sites.count = 2;
    send_key(UI_KEY_SELECT);
    send_key(UI_KEY_SELECT);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE_SITE);
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CANCEL);
    CHECK(s_state.screen == UI_SCREEN_SITES);

    send_key(UI_KEY_CIRCLE);
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    send_key(UI_KEY_DOWN);
    CHECK(s_state.actions.selected == UI_ACTION_MOVE);
    send_key(UI_KEY_UP);
    CHECK(s_state.actions.selected == UI_ACTION_COPY);
}

static void test_listing_and_dock(void)
{
    reset();
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "ux0:data/sub");
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL].loading == 1);
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL].path, "ux0:data/sub") == 0);

    UiRow rows[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "X", .type = UI_ROW_FILE, .size = 2, .size_known = 1},
    };
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_SET_ROWS;
    event.pane = UI_PANE_LOCAL;
    event.rows = rows;
    event.row_count = 2;
    event.truncated = 1;
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL].loading == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL].row_count == 2);
    CHECK(s_state.panes[UI_PANE_LOCAL].truncated == 1);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_END;
    event.pane = UI_PANE_LOCAL;
    event.ok = 1;
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL].loading == 0);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_STARTED;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.text, sizeof(event.text), "X");
    event.total = 100;
    event.total_known = 1;
    send_event(&event);
    CHECK(s_state.dock.active == 1);
    CHECK(s_state.dock.state == UI_DOCK_ACTIVE);
    CHECK(s_state.dock.direction == XFER_DIR_UPLOAD);
    CHECK(s_state.dock.total == 100);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_PROGRESS;
    event.bytes = 50;
    event.total = 100;
    event.total_known = 1;
    send_event(&event);
    CHECK(s_state.dock.done == 50);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_DONE;
    event.bytes = 100;
    event.total = 100;
    event.total_known = 1;
    send_event(&event);
    CHECK(s_state.dock.active == 0);
    CHECK(s_state.dock.state == UI_DOCK_DONE);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_PROGRESS;
    event.bytes = 99;
    event.total = 100;
    event.total_known = 1;
    send_event(&event);
    CHECK(s_state.dock.state == UI_DOCK_DONE);
    CHECK(s_state.dock.done == 100);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_TRANSFER_FAILED;
    snprintf(event.text, sizeof(event.text), "DATA LOST");
    send_event(&event);
    CHECK(s_state.dock.state == UI_DOCK_FAILED);
    CHECK(strstr(s_state.dock.message, "DATA LOST") != NULL);
}

static void test_remote_volume_marks(void)
{
    reset();
    send_key(UI_KEY_L1);
    snprintf(s_state.panes[UI_PANE_REMOTE].path,
             sizeof(s_state.panes[UI_PANE_REMOTE].path), "/UX0:/DATA");
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);
    CHECK(ui_state_is_marked(&s_state, UI_PANE_REMOTE, "/UX0:/DATA/B.BIN"));

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.path, sizeof(event.path), "/UX0:/OTHER");
    send_event(&event);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.path, sizeof(event.path), "/UR0:/DATA");
    send_event(&event);
    CHECK(mark_count(UI_PANE_REMOTE) == 0);

    reset();
    send_key(UI_KEY_L1);
    snprintf(s_state.panes[UI_PANE_REMOTE].path,
             sizeof(s_state.panes[UI_PANE_REMOTE].path), "/");
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.path, sizeof(event.path), "/GNU/");
    send_event(&event);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);
}

static void test_remote_actions_availability(void)
{
    reset();
    send_key(UI_KEY_L1);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(s_state.actions.pane == UI_PANE_REMOTE);
    CHECK(s_state.actions.enabled[UI_ACTION_DISCONNECT] == 1);
    CHECK(s_state.actions.enabled[UI_ACTION_REFRESH] == 1);
    CHECK(s_state.actions.enabled[UI_ACTION_DOWNLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DOWNLOAD], "NO SELECTION") ==
          0);
    CHECK(s_state.actions.enabled[UI_ACTION_NEW_FOLDER] == 1);

    reset();
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].connected = 0;
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.enabled[UI_ACTION_DISCONNECT] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DISCONNECT],
                 "NOT CONNECTED") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_REFRESH] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_REFRESH], "NOT CONNECTED") ==
          0);
    CHECK(s_state.actions.enabled[UI_ACTION_NEW_FOLDER] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_NEW_FOLDER],
                 "NOT CONNECTED") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);

    reset();
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    s_state.panes[UI_PANE_REMOTE].loading = 1;
    CHECK(ui_state_selected_row(&s_state, UI_PANE_REMOTE) == NULL);
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.enabled[UI_ACTION_DOWNLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DOWNLOAD], "READING") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_NEW_FOLDER] == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_REFRESH] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_REFRESH], "READING") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_DISCONNECT] == 1);

    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    s_state.panes[UI_PANE_REMOTE].loading = 1;
    ui_actions_prepare(&s_state);
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_UPLOAD], "REMOTE READING") ==
          0);
}

static void test_action_switch(void)
{
    /* The left pane keeps six rows and never offers a switch. */
    reset();
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(s_state.actions.visible_count == 6);
    CHECK(!visible_has(UI_ACTION_SWITCH_LOCAL));
    CHECK(!visible_has(UI_ACTION_SWITCH_REMOTE));

    /* The right remote view adds SWITCH TO LOCAL last. */
    reset();
    send_key(UI_KEY_L1);
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.pane == UI_PANE_REMOTE);
    CHECK(s_state.actions.visible_count == 7);
    CHECK(s_state.actions.visible[6] == UI_ACTION_SWITCH_LOCAL);
    CHECK(s_state.actions.enabled[UI_ACTION_SWITCH_LOCAL] == 1);

    /* The switch is reducer-only: no command, the new view is active, and
     * ACTIONS closes. */
    s_state.actions.selected = UI_ACTION_SWITCH_LOCAL;
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 0);
    CHECK(s_state.right_view == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.active_pane == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(strcmp(s_state.status, "SWITCHED TO LOCAL") == 0);

    /* The right local view adds SWITCH TO REMOTE last. */
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.pane == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.actions.visible_count == 7);
    CHECK(s_state.actions.visible[6] == UI_ACTION_SWITCH_REMOTE);
    CHECK(s_state.actions.enabled[UI_ACTION_SWITCH_REMOTE] == 1);
    s_state.actions.selected = UI_ACTION_SWITCH_REMOTE;
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 0);
    CHECK(s_state.right_view == UI_PANE_REMOTE);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(strcmp(s_state.status, "SWITCHED TO REMOTE") == 0);

    /* A disconnected remote view still enters ACTIONS with the switch live. */
    reset();
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].connected = 0;
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(s_state.actions.visible_count == 7);
    CHECK(s_state.actions.visible[6] == UI_ACTION_SWITCH_LOCAL);
    CHECK(s_state.actions.enabled[UI_ACTION_SWITCH_LOCAL] == 1);

    /* A lost remote view keeps the prepared switch row reachable. */
    reset();
    send_key(UI_KEY_L1);
    send_key(UI_KEY_TRIANGLE);
    s_state.panes[UI_PANE_REMOTE].connected = 0;
    s_state.panes[UI_PANE_REMOTE].session_lost = 1;
    ui_actions_prepare(&s_state);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(s_state.actions.visible_count == 7);
    CHECK(s_state.actions.visible[6] == UI_ACTION_SWITCH_LOCAL);
    CHECK(s_state.actions.enabled[UI_ACTION_SWITCH_LOCAL] == 1);
    s_state.actions.selected = UI_ACTION_SWITCH_LOCAL;
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 0);
    CHECK(s_state.right_view == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.active_pane == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    /* With both panes local, UPLOAD's NO REMOTE VIEW reason wins over the
     * no-selection reason. */
    reset();
    s_state.right_view = UI_PANE_LOCAL_RIGHT;
    s_state.active_pane = UI_PANE_LOCAL;
    s_state.panes[UI_PANE_LOCAL].selected = 0; /* ".." */
    ui_actions_prepare(&s_state);
    CHECK(s_state.actions.has_selection == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_UPLOAD], "NO REMOTE VIEW") ==
          0);
}

static void test_remote_disconnect_action(void)
{
    reset();
    send_key(UI_KEY_L1);
    snprintf(s_state.panes[UI_PANE_REMOTE].path,
             sizeof(s_state.panes[UI_PANE_REMOTE].path), "/SUB");
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_DISCONNECT;
    send_key(UI_KEY_CROSS);

    CHECK(command(UI_CMD_RUN_ACTION) != NULL);
    CHECK(command(UI_CMD_RUN_ACTION)->action == UI_ACTION_DISCONNECT);
    CHECK(command(UI_CMD_RUN_ACTION)->pane == UI_PANE_REMOTE);
    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].row_count == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].rows == NULL);
    CHECK(s_state.panes[UI_PANE_REMOTE].site[0] == '\0');
    CHECK(s_state.site_known == 0);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(strcmp(s_state.status, "DISCONNECTED") == 0);
    CHECK(mark_count(UI_PANE_REMOTE) == 0);
}

static void test_remote_delete_confirm_and_type(void)
{
    reset();
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 1; /* REMOTE, a directory */
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_DELETE;
    send_key(UI_KEY_CROSS);

    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(s_state.confirm.item_count == 1);
    if (s_state.confirm.item_count == 1)
        CHECK(strcmp(s_state.confirm.local_paths[0], "/REMOTE") == 0);

    s_state.confirm.choice_focus = 1; /* DELETE */
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RUN_ACTION) != NULL);
    CHECK(command(UI_CMD_RUN_ACTION)->action == UI_ACTION_DELETE);
    CHECK(command(UI_CMD_RUN_ACTION)->pane == UI_PANE_REMOTE);
    CHECK(command(UI_CMD_RUN_ACTION)->source_type == UI_ROW_DIR);
    CHECK(strcmp(command(UI_CMD_RUN_ACTION)->path, "/REMOTE") == 0);
}

static void test_remote_error_clears_reading(void)
{
    reset();
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].loading = 1;
    snprintf(s_state.panes[UI_PANE_REMOTE].pending_path,
             sizeof(s_state.panes[UI_PANE_REMOTE].pending_path), "/NEXT");

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_ERROR;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.text, sizeof(event.text), "CWD FAILED");
    send_event(&event);

    CHECK(s_state.panes[UI_PANE_REMOTE].loading == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].pending_path[0] == '\0');
    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 1);
    CHECK(strcmp(s_state.panes[UI_PANE_REMOTE].error, "CWD FAILED") == 0);
    CHECK(strcmp(s_state.status, "CWD FAILED") == 0);
}

static void test_session_lost_clears_identity(void)
{
    reset();
    snprintf(s_state.panes[UI_PANE_REMOTE].site,
             sizeof(s_state.panes[UI_PANE_REMOTE].site), "OLD");
    snprintf(s_state.site_host, sizeof(s_state.site_host), "old.example");
    s_state.site_port = 21;
    s_state.site_known = 1;
    s_state.panes[UI_PANE_REMOTE].loading = 1;
    s_state.screen = UI_SCREEN_ACTIONS;
    s_state.active_pane = UI_PANE_REMOTE;

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_SESSION_LOST;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.text, sizeof(event.text), "CONTROL CONNECTION LOST");
    send_event(&event);

    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 1);
    CHECK(s_state.panes[UI_PANE_REMOTE].loading == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].site[0] == '\0');
    CHECK(s_state.site_known == 0);
    CHECK(s_state.site_host[0] == '\0');
    CHECK(s_state.panes[UI_PANE_REMOTE].row_count == 3); /* R3 keeps rows */
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
}

static UiStateEvent connect_ok_event(void)
{
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_SESSION_OK;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.site, sizeof(event.site), "HOME NAS");
    snprintf(event.host, sizeof(event.host), "192.168.1.20");
    event.port = 2121;
    return event;
}

static UiStateEvent connect_fail_event(UiStateEventKind kind, const char *text)
{
    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.text, sizeof(event.text), "%s", text);
    return event;
}

/* A connect from SITES records where it started, replaces the old session
 * before dialing, and lands on the remote view when it succeeds (R13, R21). */
static void test_connect_from_sites_success(void)
{
    reset();
    s_state.right_view = UI_PANE_LOCAL_RIGHT;
    s_state.sites.count = 2;
    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 1);
    CHECK(s_state.panes[UI_PANE_REMOTE].row_count == 3);

    send_key(UI_KEY_SELECT);
    CHECK(s_state.screen == UI_SCREEN_SITES);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONNECT_SITE) != NULL);
    CHECK(s_state.sites.connecting == 1);
    CHECK(s_state.sites.connect_from_sites == 1);
    /* A new attempt drops the old session before it dials. */
    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].row_count == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].rows == NULL);
    CHECK(s_state.site_known == 0);

    UiStateEvent event = connect_ok_event();
    send_event(&event);
    CHECK(s_state.right_view == UI_PANE_REMOTE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    CHECK(s_state.focus == UI_FOCUS_LIST);
    CHECK(s_state.sites.connecting == 0);
    CHECK(s_state.sites.connect_from_sites == 0);
    CHECK(s_state.sites.login_failed == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 1);
    CHECK(strcmp(s_state.site_name, "HOME NAS") == 0);
}

/* A success after the user left SITES switches the right view but leaves the
 * screen and the pane focus where the user put them. */
static void test_connect_success_after_leaving_sites(void)
{
    reset();
    s_state.sites.count = 2;
    send_key(UI_KEY_SELECT);
    send_key(UI_KEY_CROSS);
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    /* The right pane went local while the worker was still dialing. */
    s_state.right_view = UI_PANE_LOCAL_RIGHT;
    s_state.active_pane = UI_PANE_LOCAL;
    UiStateEvent event = connect_ok_event();
    send_event(&event);
    CHECK(s_state.right_view == UI_PANE_REMOTE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.active_pane == UI_PANE_LOCAL);
    CHECK(s_state.focus == UI_FOCUS_LIST);

    /* A user who moved on to SERVER stays there. */
    reset();
    s_state.sites.count = 2;
    send_key(UI_KEY_SELECT);
    send_key(UI_KEY_CROSS);
    send_key(UI_KEY_CIRCLE);
    send_key(UI_KEY_START);
    CHECK(s_state.screen == UI_SCREEN_SERVER);
    event = connect_ok_event();
    send_event(&event);
    CHECK(s_state.right_view == UI_PANE_REMOTE);
    CHECK(s_state.screen == UI_SCREEN_SERVER);
    CHECK(s_state.focus == UI_FOCUS_SERVER);
}

/* A failure from SITES keeps SITES in front with the reason on the card for
 * every class: login, resolve, refusal, and a rejected host. */
static void test_connect_failure_stays_on_sites(void)
{
    static const struct {
        UiStateEventKind kind;
        const char *text;
    } cases[] = {
        {UI_EVT_SESSION_LOST, "Login incorrect."},
        {UI_EVT_SESSION_LOST, "RESOLVE FAILED"},
        {UI_EVT_SESSION_LOST, "CONNECT FAILED"},
        {UI_EVT_ERROR, "CONNECT REFUSED"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset();
        s_state.sites.count = 2;
        send_key(UI_KEY_SELECT);
        send_key(UI_KEY_CROSS);
        CHECK(s_state.sites.connect_from_sites == 1);

        UiStateEvent event = connect_fail_event(cases[i].kind, cases[i].text);
        send_event(&event);
        CHECK(s_state.screen == UI_SCREEN_SITES);
        CHECK(s_state.focus == UI_FOCUS_SITES);
        CHECK(s_state.sites.connecting == 0);
        CHECK(s_state.sites.connect_from_sites == 0);
        CHECK(s_state.sites.login_failed == 1);
        CHECK(strcmp(s_state.sites.reply, cases[i].text) == 0);
        CHECK(s_state.panes[UI_PANE_REMOTE].connected == 0);
    }

    /* The stored reason is not cleared by an unrelated local error. */
    UiStateEvent local = connect_fail_event(UI_EVT_ERROR, "NO FOLDER");
    local.pane = UI_PANE_LOCAL;
    send_event(&local);
    CHECK(s_state.sites.login_failed == 1);
    CHECK(strcmp(s_state.sites.reply, "CONNECT REFUSED") == 0);

    /* An unrelated error while the attempt is in flight does not consume it. */
    reset();
    s_state.sites.count = 2;
    send_key(UI_KEY_SELECT);
    send_key(UI_KEY_CROSS);
    local = connect_fail_event(UI_EVT_ERROR, "NO FOLDER");
    local.pane = UI_PANE_LOCAL;
    send_event(&local);
    CHECK(s_state.sites.connecting == 1);
    CHECK(s_state.sites.connect_from_sites == 1);
    CHECK(s_state.sites.login_failed == 0);
}

/* A failure that arrives after the user left SITES keeps the current screen
 * and puts the mapped reason on the dock's message line and the site card. */
static void test_connect_failure_after_leaving_sites(void)
{
    reset();
    s_state.sites.count = 2;
    send_key(UI_KEY_SELECT);
    send_key(UI_KEY_CROSS);
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    UiStateEvent event =
        connect_fail_event(UI_EVT_SESSION_LOST, "RESOLVE FAILED");
    send_event(&event);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.focus == UI_FOCUS_LIST);
    CHECK(strcmp(s_state.status, "RESOLVE FAILED") == 0);
    CHECK(s_state.sites.login_failed == 1);
    CHECK(strcmp(s_state.sites.reply, "RESOLVE FAILED") == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 1);

    /* The same holds on another screen, and for a rejected host. */
    reset();
    s_state.sites.count = 2;
    send_key(UI_KEY_SELECT);
    send_key(UI_KEY_CROSS);
    send_key(UI_KEY_CIRCLE);
    send_key(UI_KEY_START);
    CHECK(s_state.screen == UI_SCREEN_SERVER);
    event = connect_fail_event(UI_EVT_ERROR, "CONNECT REFUSED");
    send_event(&event);
    CHECK(s_state.screen == UI_SCREEN_SERVER);
    CHECK(s_state.focus == UI_FOCUS_SERVER);
    CHECK(strcmp(s_state.status, "CONNECT REFUSED") == 0);
    CHECK(strcmp(s_state.sites.reply, "CONNECT REFUSED") == 0);
}

/* A pane reconnect is not a SITES connect; its success does not close a
 * screen the user moved to. */
static void test_pane_reconnect_not_from_sites(void)
{
    reset();
    send_key(UI_KEY_L1);
    UiStateEvent lost = connect_fail_event(UI_EVT_SESSION_LOST, "CONTROL LOST");
    send_event(&lost);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 1);

    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RECONNECT) != NULL);
    CHECK(s_state.sites.connecting == 1);
    CHECK(s_state.sites.connect_from_sites == 0);

    UiStateEvent event = connect_ok_event();
    send_event(&event);
    CHECK(s_state.right_view == UI_PANE_REMOTE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 0);
}

/* ACTIONS, SITES, and SERVER stay reachable on a lost remote view so the
 * switch row can still take the user to the local pane (R7, KTD4). */
static void test_screen_keys_on_lost_view(void)
{
    reset();
    send_key(UI_KEY_L1);
    UiStateEvent lost = connect_fail_event(UI_EVT_SESSION_LOST, "CONTROL LOST");
    send_event(&lost);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 1);

    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(s_state.actions.pane == UI_PANE_REMOTE);
    CHECK(s_state.actions.visible_count == 7);
    CHECK(s_state.actions.visible[6] == UI_ACTION_SWITCH_LOCAL);
    CHECK(s_state.actions.enabled[UI_ACTION_SWITCH_LOCAL] == 1);
    s_state.actions.selected = UI_ACTION_SWITCH_LOCAL;
    send_key(UI_KEY_CROSS);
    CHECK(s_state.right_view == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.active_pane == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    reset();
    send_key(UI_KEY_L1);
    lost = connect_fail_event(UI_EVT_SESSION_LOST, "CONTROL LOST");
    send_event(&lost);
    send_key(UI_KEY_SELECT);
    CHECK(s_state.screen == UI_SCREEN_SITES);
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
    send_key(UI_KEY_START);
    CHECK(s_state.screen == UI_SCREEN_SERVER);
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);

    /* Cross still reconnects and Circle still dismisses. */
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RECONNECT) != NULL);
    send_key(UI_KEY_CIRCLE);
    CHECK(s_state.panes[UI_PANE_REMOTE].session_lost == 0);
}

static void test_remote_marks_pruned(void)
{
    reset();
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 2; /* B.BIN */
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);

    /* The same remote folder is listed without B.BIN. */
    UiRow without[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "REMOTE", .type = UI_ROW_DIR},
    };
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, without, 2, 0);
    CHECK(mark_count(UI_PANE_REMOTE) == 0);

    /* A mark in another remote folder survives a root listing. */
    s_state.panes[UI_PANE_REMOTE].path[0] = '\0';
    snprintf(s_state.panes[UI_PANE_REMOTE].path,
             sizeof(s_state.panes[UI_PANE_REMOTE].path), "/SUB");
    UiRow sub[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "KEEP.BIN", .type = UI_ROW_FILE},
    };
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, sub, 2, 0);
    s_state.panes[UI_PANE_REMOTE].selected = 1;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);
    snprintf(s_state.panes[UI_PANE_REMOTE].path,
             sizeof(s_state.panes[UI_PANE_REMOTE].path), "/");
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, s_remote_rows, 3, 0);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);
}

static void test_remote_multi_download(void)
{
    reset();
    UiRow rows[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "ONE.TXT", .type = UI_ROW_FILE, .size = 3, .size_known = 1},
        {.name = "TWO.TXT", .type = UI_ROW_FILE, .size = 5, .size_known = 1},
    };
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, rows, 3, 0);
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 1;
    send_key(UI_KEY_SQUARE);
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 2);

    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.selected == UI_ACTION_DOWNLOAD);
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 2);
    for (int i = 0; i < s_command_count; ++i) {
        CHECK(s_commands[i].type == UI_CMD_RUN_ACTION);
        CHECK(s_commands[i].action == UI_ACTION_DOWNLOAD);
        CHECK(s_commands[i].pane == UI_PANE_REMOTE);
    }
    CHECK(strcmp(s_commands[0].path, "/ONE.TXT") == 0);
    CHECK(strcmp(s_commands[1].path, "/TWO.TXT") == 0);
    CHECK(strcmp(s_commands[0].path2, "ux0:data") == 0);
    CHECK(s_commands[0].total == 3 && s_commands[0].total_known == 1);
    CHECK(s_commands[1].total == 5 && s_commands[1].total_known == 1);
}

static void test_remote_download_from_all_drives(void)
{
    reset();
    /* The local pane starts at the virtual ALL DRIVES root (D53). A download
     * with no concrete folder must reach the worker's ux0:data fallback. */
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "*");
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 2; /* B.BIN */
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.selected == UI_ACTION_DOWNLOAD);
    send_key(UI_KEY_CROSS);

    CHECK(command(UI_CMD_RUN_ACTION) != NULL);
    CHECK(command(UI_CMD_RUN_ACTION)->action == UI_ACTION_DOWNLOAD);
    CHECK(strcmp(command(UI_CMD_RUN_ACTION)->path2, "*") == 0);
    CHECK(strcmp(s_state.status, "PATH TOO LONG") != 0);
}

static void test_high_risk_delete_warning(void)
{
    reset();
    UiRow rows[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "X.SUPRX", .type = UI_ROW_FILE, .size = 1, .size_known = 1},
    };
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ur0:tai");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, rows, 2, 0);
    s_state.panes[UI_PANE_LOCAL].selected = 1;

    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_DELETE;
    send_key(UI_KEY_CROSS);

    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(strstr(s_state.confirm.warning, "UNDONE") != NULL);
    CHECK(strstr(s_state.confirm.warning, "BOOTING") == NULL);
}

static void test_remote_volume_root_open(void)
{
    reset();
    /* The Vita server lists its volumes as UX0:/UR0:/UMA0:. The colon makes
     * the row unsafe for transfers, but it must still open as a directory. */
    UiRow rows[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "UX0:", .type = UI_ROW_DIR, .unsafe = 1},
    };
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, rows, 2, 0);
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 1;
    send_key(UI_KEY_CROSS);

    UiCommand *open = command(UI_CMD_OPEN_PATH);
    CHECK(open != NULL);
    if (open != NULL) {
        CHECK(open->pane == UI_PANE_REMOTE);
        CHECK(strcmp(open->path, "/UX0:") == 0);
    }
    CHECK(s_state.panes[UI_PANE_REMOTE].loading == 1);
    CHECK(strcmp(s_state.status, "UNSAFE NAME") != 0);
}

static void test_remote_marked_dir_delete_type(void)
{
    reset();
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 1; /* REMOTE, a directory */
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);

    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_DELETE;
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    s_state.confirm.choice_focus = 1;
    send_key(UI_KEY_CROSS);

    CHECK(command(UI_CMD_RUN_ACTION) != NULL);
    CHECK(command(UI_CMD_RUN_ACTION)->action == UI_ACTION_DELETE);
    CHECK(command(UI_CMD_RUN_ACTION)->source_type == UI_ROW_DIR);
    CHECK(strcmp(command(UI_CMD_RUN_ACTION)->path, "/REMOTE") == 0);
}

static void test_remote_refresh_action(void)
{
    reset();
    send_key(UI_KEY_L1);
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.enabled[UI_ACTION_REFRESH] == 1);
    s_state.actions.selected = UI_ACTION_REFRESH;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_REFRESH) != NULL);
    CHECK(command(UI_CMD_REFRESH)->pane == UI_PANE_REMOTE);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
}

static void test_remote_unsafe_and_folder(void)
{
    reset();
    UiRow rows[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "A/B", .type = UI_ROW_FILE, .size = 1, .unsafe = 1},
        {.name = "FOLDER", .type = UI_ROW_DIR},
    };
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, rows, 3, 0);
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 1;
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.enabled[UI_ACTION_DOWNLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DOWNLOAD], "UNSAFE NAME") ==
          0);
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DELETE], "UNSAFE NAME") ==
          0);
    send_key(UI_KEY_CIRCLE);

    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 0);
    CHECK(strstr(s_state.status, "UNSAFE NAME") != NULL);
    CHECK(strstr(s_state.panes[UI_PANE_REMOTE].error, "UNSAFE NAME") != NULL);
    send_key(UI_KEY_UP);
    CHECK(s_state.panes[UI_PANE_REMOTE].error[0] == '\0');

    /* A folder is refused, never sent as one RETR. */
    reset();
    UiRow folder[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "FOLDER", .type = UI_ROW_DIR},
    };
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, folder, 2, 0);
    send_key(UI_KEY_L1);
    s_state.panes[UI_PANE_REMOTE].selected = 1;
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.enabled[UI_ACTION_DOWNLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_DOWNLOAD],
                 "FOLDER NOT SUPPORTED") == 0);

    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1; /* folder */
    send_key(UI_KEY_TRIANGLE);
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_UPLOAD],
                 "FOLDER NOT SUPPORTED") == 0);
}

static void test_cursor_copy_folder(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1; /* folder */
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_COPY;
    CHECK(s_state.actions.enabled[UI_ACTION_COPY] == 1);
    send_key(UI_KEY_CROSS);
    CHECK(s_state.clip_mode == UI_CLIP_COPY);
    CHECK(s_state.clip_count == 1);
    CHECK(strcmp(s_state.clip_paths[0].text, "ux0:data/folder") == 0);
    CHECK(s_state.screen == UI_SCREEN_WORKSPACE);
}

static void test_three_view_model(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_local_marks, 0, sizeof(s_local_marks));
    memset(s_remote_marks, 0, sizeof(s_remote_marks));
    memset(s_local_right_marks, 0, sizeof(s_local_right_marks));

    UiMark *marks[UI_PANE_COUNT] = {s_local_marks, s_remote_marks,
                                    s_local_right_marks};
    int caps[UI_PANE_COUNT] = {16, 16, 16};
    ui_state_init(&s_state, marks, caps);

    CHECK(UI_PANE_LOCAL == 0);
    CHECK(UI_PANE_COUNT == 3);
    CHECK(s_state.right_view == UI_PANE_REMOTE);
    CHECK(ui_pane_right_view(&s_state) == UI_PANE_REMOTE);
    CHECK(ui_pane_remote_view() == UI_PANE_REMOTE);
    CHECK(ui_pane_is_local(UI_PANE_LOCAL) == 1);
    CHECK(ui_pane_is_local(UI_PANE_LOCAL_RIGHT) == 1);
    CHECK(ui_pane_is_local(UI_PANE_REMOTE) == 0);

    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL].path, "*") == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL].connected == 1);
    CHECK(strcmp(s_state.panes[UI_PANE_REMOTE].path, "/") == 0);
    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 0);
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL_RIGHT].path, "*") == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].connected == 1);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].marks == s_local_right_marks);
}

/* L/R moves between the left slot and the right slot's current view, and each
 * view keeps its folder, cursor, scroll, and marks (KTD1). */
static void test_views_keep_state(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    s_state.panes[UI_PANE_LOCAL].scroll = 1;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);

    s_state.active_pane = UI_PANE_LOCAL_RIGHT;
    s_state.panes[UI_PANE_LOCAL_RIGHT].selected = 2;
    s_state.panes[UI_PANE_LOCAL_RIGHT].scroll = 1;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL_RIGHT) == 1);
    CHECK(ui_state_is_marked(&s_state, UI_PANE_LOCAL_RIGHT, "ur0:data/C.TXT"));

    s_state.right_view = UI_PANE_REMOTE;
    s_state.active_pane = UI_PANE_REMOTE;
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);

    s_state.right_view = UI_PANE_LOCAL_RIGHT;
    s_state.active_pane = UI_PANE_LOCAL;
    send_key(UI_KEY_L1);
    CHECK(s_state.active_pane == UI_PANE_LOCAL_RIGHT);
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL].path, "ux0:data") == 0);
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL_RIGHT].path, "ur0:data") == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 2);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 1);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].selected == 2);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].scroll == 1);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);
    CHECK(mark_count(UI_PANE_LOCAL_RIGHT) == 1);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);

    send_key(UI_KEY_R1);
    CHECK(s_state.active_pane == UI_PANE_LOCAL);

    s_state.right_view = UI_PANE_REMOTE;
    s_state.active_pane = UI_PANE_LOCAL;
    send_key(UI_KEY_L1);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].selected == 2);
    CHECK(mark_count(UI_PANE_LOCAL_RIGHT) == 1);
}

/* A listing in the right local view publishes into its own slot and leaves the
 * left view's rows, cursor, and marks untouched. */
static void test_right_local_listing_independent(void)
{
    reset();
    const UiRow *left_rows = s_state.panes[UI_PANE_LOCAL].rows;
    int left_count = s_state.panes[UI_PANE_LOCAL].row_count;
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL_RIGHT;
    snprintf(event.path, sizeof(event.path), "ur0:data/sub");
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL].row_count == left_count);
    CHECK(s_state.panes[UI_PANE_LOCAL].rows == left_rows);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 2);

    UiRow sub_rows[] = {
        {.name = "..", .type = UI_ROW_UP},
        {.name = "SUB.TXT", .type = UI_ROW_FILE, .size = 1, .size_known = 1},
    };
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_SET_ROWS;
    event.pane = UI_PANE_LOCAL_RIGHT;
    event.rows = sub_rows;
    event.row_count = 2;
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].row_count == 2);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].rows == sub_rows);
    CHECK(s_state.panes[UI_PANE_LOCAL].rows == left_rows);
    CHECK(s_state.panes[UI_PANE_LOCAL].row_count == left_count);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);

    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_END;
    event.pane = UI_PANE_LOCAL_RIGHT;
    event.ok = 1;
    send_event(&event);
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL_RIGHT].path,
                 "ur0:data/sub") == 0);
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL].path, "ux0:data") == 0);

    /* An eager local open that fails clears its own READING latch. */
    s_state.panes[UI_PANE_LOCAL_RIGHT].loading = 1;
    snprintf(s_state.panes[UI_PANE_LOCAL_RIGHT].pending_path,
             sizeof(s_state.panes[UI_PANE_LOCAL_RIGHT].pending_path),
             "ur0:bad");
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_ERROR;
    event.pane = UI_PANE_LOCAL_RIGHT;
    snprintf(event.text, sizeof(event.text), "INVALID PATH");
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].loading == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].pending_path[0] == '\0');
    CHECK(strcmp(s_state.panes[UI_PANE_LOCAL_RIGHT].error,
                 "INVALID PATH") == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL].error[0] == '\0');
}

/* A download always targets the left local view; an upload targets the remote
 * view only while the right slot shows it (KTD1, AE10). */
static void test_transfer_targets(void)
{
    reset();
    snprintf(s_state.panes[UI_PANE_LOCAL_RIGHT].path,
             sizeof(s_state.panes[UI_PANE_LOCAL_RIGHT].path), "ur0:other");

    s_state.active_pane = UI_PANE_REMOTE;
    s_state.panes[UI_PANE_REMOTE].selected = 2; /* B.BIN */
    send_key(UI_KEY_CROSS);
    UiCommand *download = command(UI_CMD_TRANSFER);
    CHECK(download != NULL);
    if (download != NULL) {
        CHECK(download->pane == UI_PANE_REMOTE);
        CHECK(strcmp(download->path2, "ux0:data") == 0);
    }

    s_state.active_pane = UI_PANE_LOCAL;
    s_state.panes[UI_PANE_LOCAL].selected = 2; /* A.TXT */
    send_key(UI_KEY_CROSS);
    UiCommand *upload = command(UI_CMD_TRANSFER);
    CHECK(upload != NULL);
    if (upload != NULL) {
        CHECK(upload->pane == UI_PANE_LOCAL);
        CHECK(strcmp(upload->path2, "/") == 0);
    }

    /* The right local view uploads to the shown remote view. */
    s_state.active_pane = UI_PANE_LOCAL_RIGHT;
    s_state.panes[UI_PANE_LOCAL_RIGHT].selected = 2; /* C.TXT */
    send_key(UI_KEY_CROSS);
    upload = command(UI_CMD_TRANSFER);
    CHECK(upload != NULL);
    if (upload != NULL) {
        CHECK(upload->pane == UI_PANE_LOCAL_RIGHT);
        CHECK(strcmp(upload->path2, "/") == 0);
    }

    /* With both panes local there is no upload target. */
    s_state.right_view = UI_PANE_LOCAL_RIGHT;
    s_state.active_pane = UI_PANE_LOCAL_RIGHT;
    s_state.panes[UI_PANE_LOCAL_RIGHT].selected = 2;
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_TRANSFER) == NULL);
    CHECK(strcmp(s_state.status, "NO REMOTE VIEW") == 0);

    /* ACTIONS names the same target and dims UPLOAD in both-local mode. */
    s_state.right_view = UI_PANE_REMOTE;
    s_state.active_pane = UI_PANE_LOCAL;
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    ui_actions_prepare(&s_state);
    CHECK(strcmp(s_state.actions.other, "/") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 1);

    s_state.right_view = UI_PANE_LOCAL_RIGHT;
    ui_actions_prepare(&s_state);
    CHECK(s_state.actions.other[0] == '\0');
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 0);
    CHECK(strcmp(s_state.actions.reason[UI_ACTION_UPLOAD], "NO REMOTE VIEW") ==
          0);

    /* The right local view's ledger follows the same target rule. */
    s_state.active_pane = UI_PANE_LOCAL_RIGHT;
    ui_actions_prepare(&s_state);
    CHECK(s_state.actions.pane == UI_PANE_LOCAL_RIGHT);
    CHECK(s_state.actions.other[0] == '\0');
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 0);

    s_state.right_view = UI_PANE_REMOTE;
    s_state.active_pane = UI_PANE_REMOTE;
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    ui_actions_prepare(&s_state);
    CHECK(strcmp(s_state.actions.other, "ux0:data") == 0);

    s_state.active_pane = UI_PANE_LOCAL_RIGHT;
    s_state.panes[UI_PANE_LOCAL_RIGHT].selected = 2;
    ui_actions_prepare(&s_state);
    CHECK(strcmp(s_state.actions.other, "/") == 0);
    CHECK(s_state.actions.enabled[UI_ACTION_UPLOAD] == 1);
}

/* A lost session clears only the remote view's marks; both local views keep
 * their rows, cursor, and marks. */
static void test_session_loss_only_remote(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 2;
    send_key(UI_KEY_SQUARE);
    s_state.active_pane = UI_PANE_LOCAL_RIGHT;
    s_state.panes[UI_PANE_LOCAL_RIGHT].selected = 2;
    send_key(UI_KEY_SQUARE);
    s_state.right_view = UI_PANE_REMOTE;
    s_state.active_pane = UI_PANE_REMOTE;
    s_state.panes[UI_PANE_REMOTE].selected = 2;
    send_key(UI_KEY_SQUARE);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);
    CHECK(mark_count(UI_PANE_LOCAL_RIGHT) == 1);
    CHECK(mark_count(UI_PANE_REMOTE) == 1);

    const UiRow *right_rows = s_state.panes[UI_PANE_LOCAL_RIGHT].rows;
    int right_count = s_state.panes[UI_PANE_LOCAL_RIGHT].row_count;
    int right_selected = s_state.panes[UI_PANE_LOCAL_RIGHT].selected;

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_SESSION_LOST;
    event.pane = UI_PANE_REMOTE;
    snprintf(event.text, sizeof(event.text), "CONTROL LOST");
    send_event(&event);

    CHECK(mark_count(UI_PANE_REMOTE) == 0);
    CHECK(mark_count(UI_PANE_LOCAL) == 1);
    CHECK(mark_count(UI_PANE_LOCAL_RIGHT) == 1);
    CHECK(s_state.panes[UI_PANE_REMOTE].connected == 0);
    CHECK(s_state.panes[UI_PANE_LOCAL].connected == 1);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].connected == 1);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].rows == right_rows);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].row_count == right_count);
    CHECK(s_state.panes[UI_PANE_LOCAL_RIGHT].selected == right_selected);
}

int main(void)
{
    test_workspace_nav();
    test_three_view_model();
    test_views_keep_state();
    test_right_local_listing_independent();
    test_transfer_targets();
    test_session_loss_only_remote();
    test_stick_selection();
    test_stick_wrap();
    test_dock_entry();
    test_marks();
    test_open_and_parent();
    test_sites_editor();
    test_server_actions();
    test_read_only_actions();
    test_confirm();
    test_high_risk_action();
    test_action_ime_and_risk();
    test_local_actions_inside_volume_root();
    test_actions_no_selection();
    test_marks_pruned_after_delete();
    test_reload_cursor_by_name();
    test_reload_prunes_marks_by_name();
    test_vanished_folder_resets_to_root();
    test_action_returns_to_workspace();
    test_cursor_copy_folder();
    test_delete_confirm_items();
    test_sites_scroll();
    test_sites_stick();
    test_stick_scope();
    test_confirm_busy_guard();
    test_path_busy_prompt();
    test_duplicate_prompt();
    test_two_lane_dock();
    test_waiting_lane_cancel();
    test_paste_guards();
    test_dock_cancel_and_site_delete();
    test_listing_and_dock();
    test_remote_volume_marks();
    test_remote_marks_pruned();
    test_remote_actions_availability();
    test_action_switch();
    test_remote_disconnect_action();
    test_remote_refresh_action();
    test_remote_delete_confirm_and_type();
    test_remote_marked_dir_delete_type();
    test_remote_multi_download();
    test_remote_download_from_all_drives();
    test_high_risk_delete_warning();
    test_remote_volume_root_open();
    test_remote_unsafe_and_folder();
    test_remote_error_clears_reading();
    test_session_lost_clears_identity();
    test_connect_from_sites_success();
    test_connect_success_after_leaving_sites();
    test_connect_failure_stays_on_sites();
    test_connect_failure_after_leaving_sites();
    test_pane_reconnect_not_from_sites();
    test_screen_keys_on_lost_view();

    if (s_failures != 0) {
        fprintf(stderr, "test_uistate: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_uistate: OK\n");
    return 0;
}
