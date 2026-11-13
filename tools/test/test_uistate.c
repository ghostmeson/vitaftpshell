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
static UiCommand s_commands[8];
static int s_command_count;

static UiRow s_local_rows[] = {
    {.name = "..", .type = UI_ROW_UP},
    {.name = "folder", .type = UI_ROW_DIR},
    {.name = "A.TXT", .type = UI_ROW_FILE, .size = 10, .size_known = 1},
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

static UiRow s_many_rows[15];

static UiConfirmItem s_confirm_items[12];

static void reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_local_marks, 0, sizeof(s_local_marks));
    memset(s_remote_marks, 0, sizeof(s_remote_marks));
    memset(s_commands, 0, sizeof(s_commands));
    s_command_count = 0;

    ui_state_init(&s_state, s_local_marks, 16, s_remote_marks, 16);
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:data");
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_local_rows, 3, 0);
    ui_state_set_rows(&s_state, UI_PANE_REMOTE, s_remote_rows, 3, 0);
    s_state.panes[UI_PANE_REMOTE].connected = 1;
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
    send_key(UI_KEY_DOWN);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 2);
    CHECK(s_state.dock.focused == 0);

    s_state.dock.active = 1;
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

static void test_stick_scroll(void)
{
    reset();
    for (int i = 0; i < 15; ++i)
        snprintf(s_many_rows[i].name, sizeof(s_many_rows[i].name), "F%02d", i);
    ui_state_set_rows(&s_state, UI_PANE_LOCAL, s_many_rows, 15, 0);

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_STICK;
    event.stick_y = 1;
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 1);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 0);
    event.stick_y = 1;
    send_event(&event);
    CHECK(s_state.panes[UI_PANE_LOCAL].scroll == 2);
    CHECK(s_state.panes[UI_PANE_LOCAL].selected == 0);
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
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_EDIT_PORT) != NULL);
    CHECK(command(UI_CMD_EDIT_PORT)->value == 1337);
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
    send_key(UI_KEY_TRIANGLE);
    s_state.actions.selected = UI_ACTION_COPY;
    CHECK(s_state.actions.enabled[UI_ACTION_COPY] == 0);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_RUN_ACTION) == NULL);
    CHECK(strcmp(s_state.status, "ACTION UNAVAILABLE") == 0);
    send_key(UI_KEY_CIRCLE);
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
    event.confirm = UI_CONFIRM_HIGH_RISK;
    event.high_risk = 1;
    snprintf(event.risk_path, sizeof(event.risk_path), "ur0:tai/x.suprx");
    send_event(&event);
    CHECK(s_state.confirm.high_risk == 1);
    CHECK(s_state.confirm.choice_focus == 0);
    CHECK(s_state.confirm.choices[0] == UI_CHOICE_CANCEL);
    CHECK(strcmp(s_state.confirm.risk_path, "ur0:tai/x.suprx") == 0);
    send_key(UI_KEY_RIGHT);
    CHECK(s_state.confirm.choice_focus == 1);
    send_key(UI_KEY_CROSS);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CONTINUE);

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
    CHECK(s_state.confirm.high_risk == 1);
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
        CHECK(run->value == 1);
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
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_HIGH_RISK);
    CHECK(strstr(s_state.confirm.risk_path, "ur0:tai") != NULL);
    send_key(UI_KEY_RIGHT);
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 2);
    for (int i = 0; i < s_command_count; ++i) {
        CHECK(s_commands[i].type == UI_CMD_RUN_ACTION);
        CHECK(s_commands[i].action == UI_ACTION_COPY);
        CHECK(s_commands[i].value == 1);
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

static void test_action_returns_to_workspace(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_SQUARE);
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
    for (int i = 0; i < 7; ++i)
        send_key(UI_KEY_DOWN);
    CHECK(s_state.sites.selected == 6);
    CHECK(s_state.sites.first == 1);
    send_key(UI_KEY_UP);
    CHECK(s_state.sites.selected == 5);
    CHECK(s_state.sites.first == 1);
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
    CHECK(command(UI_CMD_CONFIRM) != NULL);
    CHECK(command(UI_CMD_CONFIRM)->confirm == UI_CONFIRM_CANCEL_XFER);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CANCEL);
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
    CHECK(s_state.confirm.high_risk == 1);
    CHECK(strstr(s_state.confirm.warning, "BOOTING") != NULL);
    CHECK(strcmp(s_state.confirm.risk_path, "ur0:tai/X.SUPRX") == 0);
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
    CHECK(strcmp(s_state.status, "UNSAFE NAME") == 0);

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

int main(void)
{
    test_workspace_nav();
    test_stick_scroll();
    test_marks();
    test_open_and_parent();
    test_sites_editor();
    test_server_actions();
    test_confirm();
    test_high_risk_action();
    test_action_ime_and_risk();
    test_local_actions_inside_volume_root();
    test_actions_no_selection();
    test_marks_pruned_after_delete();
    test_action_returns_to_workspace();
    test_delete_confirm_items();
    test_sites_scroll();
    test_confirm_busy_guard();
    test_dock_cancel_and_site_delete();
    test_listing_and_dock();
    test_remote_volume_marks();
    test_remote_marks_pruned();
    test_remote_actions_availability();
    test_remote_disconnect_action();
    test_remote_refresh_action();
    test_remote_delete_confirm_and_type();
    test_remote_marked_dir_delete_type();
    test_remote_multi_download();
    test_remote_download_from_all_drives();
    test_high_risk_delete_warning();
    test_remote_unsafe_and_folder();
    test_remote_error_clears_reading();
    test_session_lost_clears_identity();

    if (s_failures != 0) {
        fprintf(stderr, "test_uistate: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_uistate: OK\n");
    return 0;
}
