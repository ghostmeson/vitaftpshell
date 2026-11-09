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

    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 2);
    CHECK(s_commands[0].type == UI_CMD_TRANSFER);
    CHECK(s_commands[1].type == UI_CMD_TRANSFER);
    CHECK(strcmp(s_commands[0].path, "ux0:data/folder") == 0);
    CHECK(strcmp(s_commands[1].path, "ux0:data/A.TXT") == 0);
    CHECK(s_commands[0].value == 2);
    CHECK(strcmp(s_commands[0].path2, "/") == 0);

    send_key(UI_KEY_L1);
    CHECK(s_state.active_pane == UI_PANE_REMOTE);
    CHECK(mark_count(UI_PANE_REMOTE) == 0);
    send_key(UI_KEY_R1);
    CHECK(mark_count(UI_PANE_LOCAL) == 2);

    UiStateEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = UI_EVT_LISTING_BEGIN;
    event.pane = UI_PANE_LOCAL;
    snprintf(event.path, sizeof(event.path), "ux0:data/sub");
    send_event(&event);
    CHECK(mark_count(UI_PANE_LOCAL) == 2);

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
    CHECK(strcmp(transfer->path, "ux0:data/A.TXT") == 0);
    CHECK(strcmp(transfer->path2, "/") == 0);

    reset();
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_GO_PARENT) != NULL);

    reset();
    snprintf(s_state.panes[UI_PANE_LOCAL].path,
             sizeof(s_state.panes[UI_PANE_LOCAL].path), "ux0:");
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_GO_PARENT) == NULL);
    CHECK(strcmp(s_state.status, "AT VOLUME ROOT") == 0);

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
    send_key(UI_KEY_TRIANGLE);
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

    send_key(UI_KEY_SELECT);
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
    CHECK(strcmp(s_state.actions.path, "ux0:data") == 0);
    /* The cursor is on ".." in ux0:data, which the protection table treats
     * as protected. Re-enable the action for this direct path check. */
    CHECK(s_state.actions.enabled[UI_ACTION_DELETE] == 0);
    s_state.actions.selected = UI_ACTION_DELETE;
    s_state.actions.enabled[UI_ACTION_DELETE] = 1;
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

    s_state.actions.selected = UI_ACTION_COPY;
    s_state.actions.enabled[UI_ACTION_COPY] = 0;
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
    send_key(UI_KEY_SELECT);
    CHECK(s_state.actions.marked == 2);
    CHECK(strcmp(s_state.actions.path, "ux0:data/folder") == 0);
    s_state.actions.selected = UI_ACTION_COPY;
    send_key(UI_KEY_CROSS);
    CHECK(s_command_count == 2);
    CHECK(s_commands[0].type == UI_CMD_RUN_ACTION);
    CHECK(s_commands[1].type == UI_CMD_RUN_ACTION);
    CHECK(strcmp(s_commands[0].path, "ux0:data/folder") == 0);
    CHECK(strcmp(s_commands[1].path, "ux0:data/A.TXT") == 0);
    CHECK(strcmp(s_commands[0].path2, "ux0:data") == 0);
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
    send_key(UI_KEY_SELECT);
    s_state.actions.selected = UI_ACTION_DELETE;
    s_state.actions.enabled[UI_ACTION_DELETE] = 1;
    snprintf(s_state.actions.path, sizeof(s_state.actions.path),
             "ur0:tai/x.suprx");
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_HIGH_RISK);
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
    CHECK(s_state.screen == UI_SCREEN_ACTIONS);
}

static void test_action_ime_and_risk(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_SELECT);
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

    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_SELECT);
    s_state.actions.selected = UI_ACTION_COPY;
    snprintf(s_state.actions.dest, sizeof(s_state.actions.dest), "ur0:tai");
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
}

static void test_delete_confirm_items(void)
{
    reset();
    s_state.panes[UI_PANE_LOCAL].selected = 1;
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_DOWN);
    send_key(UI_KEY_SQUARE);
    send_key(UI_KEY_SELECT);
    s_state.actions.selected = UI_ACTION_DELETE;
    s_state.actions.enabled[UI_ACTION_DELETE] = 1;
    send_key(UI_KEY_CROSS);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE);
    CHECK(s_state.confirm.item_count == 2);
    CHECK(strcmp(s_state.confirm.title, "DELETE 2 ITEMS") == 0);
    CHECK(s_state.confirm.items != NULL);
    CHECK(strcmp(s_state.confirm.items[0].name, "folder") == 0);
    CHECK(strcmp(s_state.confirm.items[1].name, "A.TXT") == 0);
}

static void test_sites_scroll(void)
{
    reset();
    s_state.sites.count = 7;
    for (int i = 0; i < 7; ++i) {
        snprintf(s_state.sites.rows[i].name,
                 sizeof(s_state.sites.rows[i].name), "SITE%d", i);
    }
    send_key(UI_KEY_TRIANGLE);
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
    send_key(UI_KEY_TRIANGLE);
    send_key(UI_KEY_SELECT);
    CHECK(s_state.screen == UI_SCREEN_CONFIRM);
    CHECK(s_state.confirm.kind == UI_CONFIRM_DELETE_SITE);
    send_key(UI_KEY_CIRCLE);
    CHECK(command(UI_CMD_CONFIRM)->choice == UI_CHOICE_CANCEL);
    CHECK(s_state.screen == UI_SCREEN_SITES);

    send_key(UI_KEY_CIRCLE);
    send_key(UI_KEY_SELECT);
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
    test_delete_confirm_items();
    test_sites_scroll();
    test_confirm_busy_guard();
    test_dock_cancel_and_site_delete();
    test_listing_and_dock();
    test_remote_volume_marks();

    if (s_failures != 0) {
        fprintf(stderr, "test_uistate: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_uistate: OK\n");
    return 0;
}
