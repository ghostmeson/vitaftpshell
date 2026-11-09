/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/uistate.h"

#include <stdio.h>
#include <string.h>

#include "core/path.h"

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int ci_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (lower_ascii(*a) != lower_ascii(*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static UiCommand *emit(UiCommand *out, int max, int *count,
                       UiCommandType type)
{
    if (out == NULL || count == NULL || *count >= max)
        return NULL;
    UiCommand *command = &out[*count];
    memset(command, 0, sizeof(*command));
    command->type = type;
    (*count)++;
    return command;
}

static UiPane other_pane(UiPane pane)
{
    return pane == UI_PANE_LOCAL ? UI_PANE_REMOTE : UI_PANE_LOCAL;
}

static UiFocus focus_for_screen(UiScreen screen)
{
    switch (screen) {
    case UI_SCREEN_SITES:
        return UI_FOCUS_SITES;
    case UI_SCREEN_SITE_EDITOR:
        return UI_FOCUS_EDITOR;
    case UI_SCREEN_SERVER:
        return UI_FOCUS_SERVER;
    case UI_SCREEN_ACTIONS:
        return UI_FOCUS_ACTIONS;
    case UI_SCREEN_CONFIRM:
        return UI_FOCUS_CONFIRM;
    case UI_SCREEN_WORKSPACE:
    default:
        return UI_FOCUS_LIST;
    }
}

static void set_focus(UiState *state, UiFocus focus)
{
    state->focus = focus;
    state->dock.focused = focus == UI_FOCUS_DOCK;
}

static void set_status(UiState *state, const char *text)
{
    snprintf(state->status, sizeof(state->status), "%s",
             text != NULL ? text : "");
}

static void enter_screen(UiState *state, UiScreen screen)
{
    state->return_screen = state->screen;
    state->screen = screen;
    set_focus(state, focus_for_screen(screen));
}

static void leave_screen(UiState *state)
{
    state->screen = state->return_screen;
    state->return_screen = UI_SCREEN_WORKSPACE;
    set_focus(state, focus_for_screen(state->screen));
}

static void leave_editor(UiState *state)
{
    state->screen = state->editor_return;
    state->editor_return = UI_SCREEN_WORKSPACE;
    set_focus(state, focus_for_screen(state->screen));
    state->editor.dirty = 0;
}

static int join_path(UiPane pane, const UiPaneState *pane_state,
                     const char *name, char *out, size_t cap)
{
    if (pane == UI_PANE_LOCAL) {
        Path joined;
        if (path_join(pane_state->path, name, &joined) != PATH_OK)
            return 0;
        int n = snprintf(out, cap, "%s", joined.text);
        return n > 0 && (size_t)n < cap;
    }

    int n;
    if (strcmp(pane_state->path, "/") == 0)
        n = snprintf(out, cap, "/%s", name);
    else
        n = snprintf(out, cap, "%s/%s", pane_state->path, name);
    return n > 0 && (size_t)n < cap;
}

static void remote_volume(const char *path, char *out, size_t cap)
{
    out[0] = '\0';
    if (path == NULL || path[0] != '/')
        return;
    const char *p = path + 1;
    size_t n = 0;
    while (p[n] != '\0' && p[n] != '/' && n + 1 < cap) {
        out[n] = p[n];
        n++;
    }
    out[n] = '\0';
}

static int volume_changed(UiPane pane, const char *old_path,
                          const char *new_path)
{
    if (old_path == NULL || new_path == NULL || old_path[0] == '\0' ||
        new_path[0] == '\0')
        return 0;

    if (pane == UI_PANE_LOCAL) {
        Volume old_volume;
        Volume new_volume;
        if (!path_volume(old_path, &old_volume) ||
            !path_volume(new_path, &new_volume))
            return 0;
        return old_volume != new_volume;
    }

    char old_token[16];
    char new_token[16];
    remote_volume(old_path, old_token, sizeof(old_token));
    remote_volume(new_path, new_token, sizeof(new_token));
    return !ci_equal(old_token, new_token);
}

static void recompute_marks(UiPaneState *pane)
{
    pane->marked_bytes = 0;
    pane->marked_bytes_known = 0;
    for (int i = 0; i < pane->mark_count; ++i) {
        if (pane->marks[i].size_known) {
            pane->marked_bytes += pane->marks[i].size;
            pane->marked_bytes_known = 1;
        }
    }
}

static int mark_index(const UiPaneState *pane, const char *path)
{
    for (int i = 0; i < pane->mark_count; ++i) {
        if (strcmp(pane->marks[i].path.text, path) == 0)
            return i;
    }
    return -1;
}

static void clear_marks(UiPaneState *pane)
{
    pane->mark_count = 0;
    pane->marked_bytes = 0;
    pane->marked_bytes_known = 0;
}

static int toggle_mark(UiPaneState *pane, const UiRow *row, const char *path)
{
    int index = mark_index(pane, path);
    if (index >= 0) {
        for (int i = index; i + 1 < pane->mark_count; ++i)
            pane->marks[i] = pane->marks[i + 1];
        pane->mark_count--;
        recompute_marks(pane);
        return 1;
    }

    if (pane->mark_count >= pane->mark_cap)
        return 0;

    UiMark *mark = &pane->marks[pane->mark_count++];
    memset(mark, 0, sizeof(*mark));
    snprintf(mark->path.text, sizeof(mark->path.text), "%s", path);
    mark->size = row->size;
    mark->size_known = row->size_known;
    recompute_marks(pane);
    return 1;
}

static void clamp_pane(UiPaneState *pane)
{
    if (pane->row_count <= 0) {
        pane->row_count = 0;
        pane->selected = 0;
        pane->scroll = 0;
        return;
    }
    if (pane->selected >= pane->row_count)
        pane->selected = pane->row_count - 1;
    if (pane->selected < 0)
        pane->selected = 0;

    int max_scroll = pane->row_count - UI_ROWS_VISIBLE;
    if (max_scroll < 0)
        max_scroll = 0;
    if (pane->scroll > max_scroll)
        pane->scroll = max_scroll;
    if (pane->scroll < 0)
        pane->scroll = 0;
}

static void move_selection(UiPaneState *pane, int delta)
{
    if (pane->row_count <= 0)
        return;
    int next = pane->selected + delta;
    if (next < 0)
        next = 0;
    if (next >= pane->row_count)
        next = pane->row_count - 1;
    pane->selected = next;

    if (pane->selected < pane->scroll)
        pane->scroll = pane->selected;
    if (pane->selected >= pane->scroll + UI_ROWS_VISIBLE)
        pane->scroll = pane->selected - UI_ROWS_VISIBLE + 1;
    clamp_pane(pane);
}

static void scroll_rows(UiPaneState *pane, int delta)
{
    pane->scroll += delta;
    clamp_pane(pane);
}

static const UiRow *selected_row(const UiState *state, UiPane pane)
{
    const UiPaneState *p = &state->panes[pane];
    if (p->rows == NULL || p->selected < 0 || p->selected >= p->row_count)
        return NULL;
    return &p->rows[p->selected];
}

static void set_transfer_dock(UiState *state, const UiStateEvent *event,
                              UiDockState dock_state)
{
    state->dock.state = dock_state;
    if (event->text[0] != '\0')
        snprintf(state->dock.message, sizeof(state->dock.message), "%s",
                 event->text);
    state->dock.done = event->bytes;
    state->dock.total = event->total;
    state->dock.total_known = event->total_known;
}

static void open_confirm(UiState *state, UiConfirmKind kind)
{
    state->confirm_return = state->screen;
    state->screen = UI_SCREEN_CONFIRM;
    set_focus(state, UI_FOCUS_CONFIRM);

    UiConfirm *c = &state->confirm;
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    c->choice_focus = 0;

    switch (kind) {
    case UI_CONFIRM_DELETE:
        c->choices[0] = UI_CHOICE_CANCEL;
        c->choices[1] = UI_CHOICE_DELETE;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "DELETE");
        snprintf(c->warning, sizeof(c->warning), "THIS CANNOT BE UNDONE.");
        break;
    case UI_CONFIRM_CONFLICT:
        c->choices[0] = UI_CHOICE_CANCEL;
        c->choices[1] = UI_CHOICE_SKIP;
        c->choices[2] = UI_CHOICE_OVERWRITE;
        c->choice_count = 3;
        snprintf(c->title, sizeof(c->title), "DESTINATIONS EXIST");
        snprintf(c->warning, sizeof(c->warning),
                 "EXISTING FILES WILL BE REPLACED.");
        break;
    case UI_CONFIRM_CANCEL_XFER:
        c->choices[0] = UI_CHOICE_KEEP;
        c->choices[1] = UI_CHOICE_CANCEL;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "CANCEL TRANSFER");
        snprintf(c->warning, sizeof(c->warning),
                 "THE PARTIAL FILE IS REMOVED.");
        break;
    case UI_CONFIRM_STOP_SERVER:
        c->choices[0] = UI_CHOICE_KEEP;
        c->choices[1] = UI_CHOICE_STOP;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "STOP SERVER");
        snprintf(c->warning, sizeof(c->warning),
                 "THE IN-FLIGHT WRITE IS CLOSED.");
        break;
    case UI_CONFIRM_DISCARD:
        c->choices[0] = UI_CHOICE_KEEP;
        c->choices[1] = UI_CHOICE_DISCARD;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "DISCARD CHANGES");
        snprintf(c->warning, sizeof(c->warning), "THE EDITS ARE LOST.");
        break;
    case UI_CONFIRM_HIGH_RISK:
        c->choices[0] = UI_CHOICE_CANCEL;
        c->choices[1] = UI_CHOICE_CONTINUE;
        c->choice_count = 2;
        c->high_risk = 1;
        snprintf(c->title, sizeof(c->title), "WRITE INSIDE HIGH-RISK PATH");
        snprintf(c->warning, sizeof(c->warning),
                 "A BAD EDIT CAN STOP THE CONSOLE BOOTING. THERE IS NO UNDO.");
        break;
    case UI_CONFIRM_DELETE_SITE:
        c->choices[0] = UI_CHOICE_CANCEL;
        c->choices[1] = UI_CHOICE_DELETE;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "DELETE SITE");
        snprintf(c->warning, sizeof(c->warning), "THIS CANNOT BE UNDONE.");
        break;
    case UI_CONFIRM_NONE:
    default:
        break;
    }
}

static void close_confirm(UiState *state, int return_to_editor)
{
    UiConfirmKind kind = state->confirm.kind;
    state->confirm.kind = UI_CONFIRM_NONE;

    if (kind == UI_CONFIRM_DISCARD && return_to_editor) {
        state->screen = UI_SCREEN_SITE_EDITOR;
        set_focus(state, UI_FOCUS_EDITOR);
        return;
    }
    if (kind == UI_CONFIRM_DISCARD) {
        leave_editor(state);
        return;
    }
    state->screen = state->confirm_return;
    set_focus(state, focus_for_screen(state->screen));
    if (state->screen == UI_SCREEN_WORKSPACE)
        set_focus(state, UI_FOCUS_LIST);
}

static void handle_workspace_key(UiState *state, const UiStateEvent *event,
                                 UiCommand *out, int max, int *count)
{
    UiPaneState *pane = &state->panes[state->active_pane];
    UiPane other = other_pane(state->active_pane);
    UiKey key = event->key;

    if (key == UI_KEY_L1 || key == UI_KEY_R1) {
        state->active_pane = other;
        set_focus(state, UI_FOCUS_LIST);
        return;
    }

    if (pane->session_lost) {
        if (key == UI_KEY_CROSS) {
            UiCommand *c = emit(out, max, count, UI_CMD_RECONNECT);
            if (c != NULL)
                c->pane = state->active_pane;
        } else if (key == UI_KEY_CIRCLE) {
            pane->session_lost = 0;
            set_status(state, "");
        }
        return;
    }

    if (state->dock.focused) {
        if (key == UI_KEY_UP || key == UI_KEY_CIRCLE) {
            set_focus(state, UI_FOCUS_LIST);
            if (pane->row_count > 0)
                pane->selected = pane->row_count - 1;
            clamp_pane(pane);
        } else if (key == UI_KEY_CROSS) {
            open_confirm(state, UI_CONFIRM_CANCEL_XFER);
        }
        return;
    }

    switch (key) {
    case UI_KEY_UP:
        move_selection(pane, -1);
        return;
    case UI_KEY_DOWN:
        if (pane->row_count == 0 ||
            pane->selected >= pane->row_count - 1) {
            if (state->dock.active || state->dock.queued > 0)
                set_focus(state, UI_FOCUS_DOCK);
            return;
        }
        move_selection(pane, 1);
        return;
    case UI_KEY_CROSS: {
        const UiRow *row = selected_row(state, state->active_pane);
        if (row == NULL)
            return;
        if (row->type == UI_ROW_UP) {
            UiCommand *c = emit(out, max, count, UI_CMD_GO_PARENT);
            if (c != NULL)
                c->pane = state->active_pane;
            return;
        }

        char full[PATH_MAX_LEN];
        if (!join_path(state->active_pane, pane, row->name, full,
                       sizeof(full))) {
            set_status(state, "PATH TOO LONG");
            return;
        }

        if (row->type == UI_ROW_DIR) {
            snprintf(pane->pending_path, sizeof(pane->pending_path), "%s",
                     full);
            pane->loading = 1;
            UiCommand *c = emit(out, max, count, UI_CMD_OPEN_PATH);
            if (c != NULL) {
                c->pane = state->active_pane;
                snprintf(c->path, sizeof(c->path), "%s", full);
            }
            return;
        }

        if ((state->active_pane == UI_PANE_REMOTE && !pane->connected) ||
            (state->active_pane == UI_PANE_LOCAL &&
             !state->panes[other].connected)) {
            set_status(state, "NOT CONNECTED");
            return;
        }

        if (pane->mark_count > 0) {
            for (int i = 0; i < pane->mark_count; ++i) {
                UiCommand *c = emit(out, max, count, UI_CMD_TRANSFER);
                if (c == NULL)
                    break;
                c->pane = state->active_pane;
                snprintf(c->path, sizeof(c->path), "%s",
                         pane->marks[i].path.text);
                snprintf(c->path2, sizeof(c->path2), "%s",
                         state->panes[other].path);
                c->value = pane->mark_count;
            }
            return;
        }

        UiCommand *c = emit(out, max, count, UI_CMD_TRANSFER);
        if (c != NULL) {
            c->pane = state->active_pane;
            snprintf(c->path, sizeof(c->path), "%s", full);
            snprintf(c->path2, sizeof(c->path2), "%s",
                     state->panes[other].path);
            c->value = 0;
        }
        return;
    }
    case UI_KEY_CIRCLE:
        if (state->active_pane == UI_PANE_LOCAL &&
            path_is_volume_root(pane->path)) {
            set_status(state, "AT VOLUME ROOT");
            return;
        }
        if (state->active_pane == UI_PANE_REMOTE &&
            strcmp(pane->path, "/") == 0) {
            set_status(state, "AT ROOT");
            return;
        }
        {
            UiCommand *c = emit(out, max, count, UI_CMD_GO_PARENT);
            if (c != NULL)
                c->pane = state->active_pane;
        }
        return;
    case UI_KEY_SQUARE: {
        const UiRow *row = selected_row(state, state->active_pane);
        if (row == NULL || row->type == UI_ROW_UP)
            return;
        char full[PATH_MAX_LEN];
        if (!join_path(state->active_pane, pane, row->name, full,
                       sizeof(full))) {
            set_status(state, "PATH TOO LONG");
            return;
        }
        if (!toggle_mark(pane, row, full))
            set_status(state, "MARK LIMIT");
        return;
    }
    case UI_KEY_TRIANGLE:
        enter_screen(state, UI_SCREEN_SITES);
        return;
    case UI_KEY_START:
        enter_screen(state, UI_SCREEN_SERVER);
        return;
    case UI_KEY_SELECT: {
        enter_screen(state, UI_SCREEN_ACTIONS);
        state->actions.selected = 0;
        state->actions.marked = pane->mark_count;

        const UiRow *row = selected_row(state, state->active_pane);
        char source[PATH_MAX_LEN];
        source[0] = '\0';
        if (row != NULL && row->type != UI_ROW_UP)
            join_path(state->active_pane, pane, row->name, source,
                      sizeof(source));
        if (source[0] == '\0')
            snprintf(source, sizeof(source), "%s", pane->path);
        snprintf(state->actions.path, sizeof(state->actions.path), "%s",
                 source);
        snprintf(state->actions.dest, sizeof(state->actions.dest), "%s",
                 pane->path);
        snprintf(state->actions.other, sizeof(state->actions.other), "%s",
                 state->panes[other].path);
        return;
    }
    default:
        return;
    }
}

static void handle_sites_key(UiState *state, const UiStateEvent *event,
                             UiCommand *out, int max, int *count)
{
    switch (event->key) {
    case UI_KEY_UP:
        if (state->sites.selected > 0)
            state->sites.selected--;
        return;
    case UI_KEY_DOWN:
        if (state->sites.selected + 1 < state->sites.count)
            state->sites.selected++;
        return;
    case UI_KEY_CROSS: {
        UiCommand *c = emit(out, max, count, UI_CMD_CONNECT_SITE);
        if (c != NULL)
            c->index = state->sites.selected;
        return;
    }
    case UI_KEY_SQUARE: {
        UiCommand *c = emit(out, max, count, UI_CMD_EDIT_SITE);
        if (c != NULL)
            c->index = state->sites.selected;
        state->editor_return = UI_SCREEN_SITES;
        state->editor.dirty = 0;
        state->editor.error[0] = '\0';
        enter_screen(state, UI_SCREEN_SITE_EDITOR);
        return;
    }
    case UI_KEY_TRIANGLE: {
        UiCommand *c = emit(out, max, count, UI_CMD_NEW_SITE);
        (void)c;
        state->editor_return = UI_SCREEN_SITES;
        memset(&state->editor, 0, sizeof(state->editor));
        state->editor.port = 21;
        state->editor.anonymous = 1;
        state->editor.field = UI_FIELD_NAME;
        state->editor.dirty = 0;
        enter_screen(state, UI_SCREEN_SITE_EDITOR);
        return;
    }
    case UI_KEY_SELECT:
        open_confirm(state, UI_CONFIRM_DELETE_SITE);
        snprintf(state->confirm.context, sizeof(state->confirm.context),
                 "SITE %d", state->sites.selected + 1);
        return;
    case UI_KEY_CIRCLE:
        leave_screen(state);
        return;
    default:
        return;
    }
}

static UiField next_field(UiField field, int direction, int anonymous)
{
    for (int step = 1; step <= UI_FIELD_COUNT; ++step) {
        int index = ((int)field + direction * step) % UI_FIELD_COUNT;
        if (index < 0)
            index += UI_FIELD_COUNT;
        if (anonymous &&
            (index == UI_FIELD_USERNAME || index == UI_FIELD_PASSWORD))
            continue;
        return (UiField)index;
    }
    return field;
}

static void editor_value(const UiEditor *editor, UiField field, char *out,
                         size_t cap)
{
    out[0] = '\0';
    switch (field) {
    case UI_FIELD_NAME:
        snprintf(out, cap, "%s", editor->name);
        break;
    case UI_FIELD_HOST:
        snprintf(out, cap, "%s", editor->host);
        break;
    case UI_FIELD_PORT:
        snprintf(out, cap, "%d", editor->port);
        break;
    case UI_FIELD_USERNAME:
        snprintf(out, cap, "%s", editor->username);
        break;
    case UI_FIELD_PASSWORD:
        snprintf(out, cap, "%s", editor->password);
        break;
    case UI_FIELD_ANONYMOUS:
    case UI_FIELD_COUNT:
    default:
        break;
    }
}

static const char *field_title(UiField field)
{
    switch (field) {
    case UI_FIELD_NAME:
        return "SITE NAME";
    case UI_FIELD_HOST:
        return "HOST";
    case UI_FIELD_PORT:
        return "PORT";
    case UI_FIELD_USERNAME:
        return "USERNAME";
    case UI_FIELD_PASSWORD:
        return "PASSWORD";
    default:
        return "";
    }
}

static int field_max(UiField field)
{
    switch (field) {
    case UI_FIELD_NAME:
    case UI_FIELD_USERNAME:
        return 64;
    case UI_FIELD_HOST:
        return 255;
    case UI_FIELD_PORT:
        return 5;
    case UI_FIELD_PASSWORD:
        return 128;
    default:
        return 0;
    }
}

static void handle_editor_key(UiState *state, const UiStateEvent *event,
                              UiCommand *out, int max, int *count)
{
    UiEditor *editor = &state->editor;

    switch (event->key) {
    case UI_KEY_UP:
        editor->field = next_field((UiField)editor->field, -1,
                                   editor->anonymous);
        return;
    case UI_KEY_DOWN:
        editor->field = next_field((UiField)editor->field, 1,
                                   editor->anonymous);
        return;
    case UI_KEY_CROSS:
        if (editor->field == UI_FIELD_ANONYMOUS) {
            editor->anonymous = !editor->anonymous;
            editor->dirty = 1;
            return;
        }
        {
            char value[256];
            editor_value(editor, (UiField)editor->field, value, sizeof(value));
            UiCommand *c = emit(out, max, count, UI_CMD_OPEN_IME);
            if (c != NULL) {
                c->field = (UiField)editor->field;
                c->index = editor->field == UI_FIELD_PASSWORD;
                c->value = field_max((UiField)editor->field);
                snprintf(c->title, sizeof(c->title), "%s",
                         field_title((UiField)editor->field));
                snprintf(c->text, sizeof(c->text), "%s", value);
            }
            state->text.open = 1;
            state->text.field = (UiField)editor->field;
            state->text.masked = editor->field == UI_FIELD_PASSWORD;
            state->text.max = field_max((UiField)editor->field);
            snprintf(state->text.title, sizeof(state->text.title), "%s",
                     field_title((UiField)editor->field));
            snprintf(state->text.value, sizeof(state->text.value), "%s",
                     value);
        }
        return;
    case UI_KEY_SQUARE:
        if (editor->name[0] == '\0') {
            snprintf(editor->error, sizeof(editor->error), "NAME IS REQUIRED");
            editor->field = UI_FIELD_NAME;
            return;
        }
        if (editor->host[0] == '\0') {
            snprintf(editor->error, sizeof(editor->error), "HOST IS REQUIRED");
            editor->field = UI_FIELD_HOST;
            return;
        }
        if (editor->port < 1 || editor->port > 65535) {
            snprintf(editor->error, sizeof(editor->error),
                     "PORT MUST BE 1-65535");
            editor->field = UI_FIELD_PORT;
            return;
        }
        editor->error[0] = '\0';
        {
            UiCommand *c = emit(out, max, count, UI_CMD_SAVE_SITE);
            (void)c;
        }
        return;
    case UI_KEY_CIRCLE:
        if (editor->dirty)
            open_confirm(state, UI_CONFIRM_DISCARD);
        else
            leave_editor(state);
        return;
    default:
        return;
    }
}

static void handle_server_key(UiState *state, const UiStateEvent *event,
                              UiCommand *out, int max, int *count)
{
    switch (event->key) {
    case UI_KEY_UP:
    case UI_KEY_DOWN:
        state->server.focus_start = !state->server.focus_start;
        return;
    case UI_KEY_CROSS:
        if (!state->server.focus_start) {
            UiCommand *c = emit(out, max, count, UI_CMD_EDIT_PORT);
            if (c != NULL)
                c->value = state->server.port;
            return;
        }
        if (state->server.run == UI_SERVER_OFF) {
            emit(out, max, count, UI_CMD_START_SERVER);
        } else {
            emit(out, max, count, UI_CMD_STOP_SERVER);
        }
        return;
    case UI_KEY_CIRCLE:
        leave_screen(state);
        return;
    default:
        return;
    }
}

static const char *protection_reason(PathResult result, const char *path)
{
    if (result == PATH_PROTECTED)
        return path_is_volume_root(path) ? "VOLUME ROOT" : "PROTECTED PATH";
    if (result == PATH_DENIED)
        return "BOOT CONFIG DENIED";
    return "INVALID PATH";
}

static void handle_actions_key(UiState *state, const UiStateEvent *event,
                               UiCommand *out, int max, int *count)
{
    switch (event->key) {
    case UI_KEY_UP:
        if (state->actions.selected > 0)
            state->actions.selected--;
        return;
    case UI_KEY_DOWN:
        if (state->actions.selected + 1 < UI_ACTION_COUNT)
            state->actions.selected++;
        return;
    case UI_KEY_CROSS: {
        UiAction action = (UiAction)state->actions.selected;
        if (!state->actions.enabled[action]) {
            set_status(state, "ACTION UNAVAILABLE");
            return;
        }
        const UiPaneState *pane = &state->panes[state->active_pane];
        const char *first_source = state->actions.path;
        if ((action == UI_ACTION_COPY || action == UI_ACTION_MOVE ||
             action == UI_ACTION_DELETE) &&
            pane->mark_count > 0)
            first_source = pane->marks[0].path.text;

        if (action == UI_ACTION_DELETE) {
            PathRisk risk = PATH_RISK_NORMAL;
            PathResult result = path_check_delete(first_source, 0, &risk);
            if (result != PATH_OK) {
                set_status(state, protection_reason(result, first_source));
                return;
            }
            if (risk == PATH_RISK_HIGH) {
                state->confirm_from_worker = 0;
                state->pending_action = action;
                state->pending_pane = state->active_pane;
                snprintf(state->pending_path.text,
                         sizeof(state->pending_path.text), "%s",
                         first_source);
                snprintf(state->pending_dest.text,
                         sizeof(state->pending_dest.text), "%s",
                         state->actions.dest);
                open_confirm(state, UI_CONFIRM_HIGH_RISK);
                snprintf(state->confirm.risk_path,
                         sizeof(state->confirm.risk_path), "%s",
                         first_source);
                state->confirm.high_risk = 1;
                return;
            }
        }

        if ((action == UI_ACTION_COPY || action == UI_ACTION_MOVE ||
             action == UI_ACTION_DELETE) &&
            pane->mark_count > 0) {
            for (int i = 0; i < pane->mark_count; ++i) {
                UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
                if (c == NULL)
                    break;
                c->action = action;
                c->pane = state->active_pane;
                snprintf(c->path, sizeof(c->path), "%s",
                         pane->marks[i].path.text);
                snprintf(c->path2, sizeof(c->path2), "%s",
                         state->actions.dest);
            }
            return;
        }

        UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
        if (c != NULL) {
            c->action = action;
            c->pane = state->active_pane;
            snprintf(c->path, sizeof(c->path), "%s", state->actions.path);
            snprintf(c->path2, sizeof(c->path2), "%s",
                     state->actions.dest);
        }
        return;
    }
    case UI_KEY_CIRCLE:
        leave_screen(state);
        return;
    default:
        return;
    }
}

static void handle_confirm_key(UiState *state, const UiStateEvent *event,
                               UiCommand *out, int max, int *count)
{
    UiConfirm *c = &state->confirm;

    switch (event->key) {
    case UI_KEY_UP:
        if (c->item_scroll > 0)
            c->item_scroll--;
        return;
    case UI_KEY_DOWN: {
        int max_scroll = c->item_count - UI_CONFIRM_VIEW;
        if (max_scroll < 0)
            max_scroll = 0;
        if (c->item_scroll < max_scroll)
            c->item_scroll++;
        return;
    }
    case UI_KEY_LEFT:
        if (c->choice_focus > 0)
            c->choice_focus--;
        return;
    case UI_KEY_RIGHT:
        if (c->choice_focus + 1 < c->choice_count)
            c->choice_focus++;
        return;
    case UI_KEY_CROSS: {
        UiChoice choice = c->choices[c->choice_focus];
        int from_worker = state->confirm_from_worker;
        if (c->kind == UI_CONFIRM_HIGH_RISK && !from_worker) {
            if (choice == UI_CHOICE_CONTINUE) {
                UiCommand *run = emit(out, max, count, UI_CMD_RUN_ACTION);
                if (run != NULL) {
                    run->action = state->pending_action;
                    run->pane = state->pending_pane;
                    run->value = 1; /* risk approved */
                    snprintf(run->path, sizeof(run->path), "%s",
                             state->pending_path.text);
                    snprintf(run->path2, sizeof(run->path2), "%s",
                             state->pending_dest.text);
                }
            }
            close_confirm(state, 0);
            return;
        }
        UiCommand *command = emit(out, max, count, UI_CMD_CONFIRM);
        if (command != NULL) {
            command->confirm = c->kind;
            command->choice = choice;
            command->index = state->sites.selected;
        }
        if (c->kind == UI_CONFIRM_DISCARD) {
            if (choice == UI_CHOICE_DISCARD)
                close_confirm(state, 0);
            else
                close_confirm(state, 1);
            return;
        }
        close_confirm(state, 0);
        return;
    }
    case UI_KEY_CIRCLE: {
        if (c->kind == UI_CONFIRM_HIGH_RISK &&
            !state->confirm_from_worker) {
            close_confirm(state, 0);
            return;
        }
        UiChoice choice = UI_CHOICE_CANCEL;
        if (c->kind == UI_CONFIRM_DISCARD ||
            c->kind == UI_CONFIRM_CANCEL_XFER ||
            c->kind == UI_CONFIRM_STOP_SERVER)
            choice = UI_CHOICE_KEEP;
        UiCommand *command = emit(out, max, count, UI_CMD_CONFIRM);
        if (command != NULL) {
            command->confirm = c->kind;
            command->choice = choice;
            command->index = state->sites.selected;
        }
        if (c->kind == UI_CONFIRM_DISCARD)
            close_confirm(state, 1);
        else
            close_confirm(state, 0);
        return;
    }
    default:
        return;
    }
}

static void handle_key(UiState *state, const UiStateEvent *event,
                       UiCommand *out, int max, int *count)
{
    switch (state->screen) {
    case UI_SCREEN_WORKSPACE:
        handle_workspace_key(state, event, out, max, count);
        return;
    case UI_SCREEN_SITES:
        handle_sites_key(state, event, out, max, count);
        return;
    case UI_SCREEN_SITE_EDITOR:
        handle_editor_key(state, event, out, max, count);
        return;
    case UI_SCREEN_SERVER:
        handle_server_key(state, event, out, max, count);
        return;
    case UI_SCREEN_ACTIONS:
        handle_actions_key(state, event, out, max, count);
        return;
    case UI_SCREEN_CONFIRM:
        handle_confirm_key(state, event, out, max, count);
        return;
    default:
        return;
    }
}

void ui_state_init(UiState *state, UiMark *local_marks, int local_cap,
                   UiMark *remote_marks, int remote_cap)
{
    if (state == NULL)
        return;
    memset(state, 0, sizeof(*state));

    state->screen = UI_SCREEN_WORKSPACE;
    state->return_screen = UI_SCREEN_WORKSPACE;
    state->confirm_return = UI_SCREEN_WORKSPACE;
    state->editor_return = UI_SCREEN_WORKSPACE;
    state->active_pane = UI_PANE_LOCAL;
    state->focus = UI_FOCUS_LIST;

    UiPaneState *local = &state->panes[UI_PANE_LOCAL];
    snprintf(local->path, sizeof(local->path), "ux0:data");
    local->connected = 1;
    local->marks = local_marks;
    local->mark_cap = local_cap;

    UiPaneState *remote = &state->panes[UI_PANE_REMOTE];
    snprintf(remote->path, sizeof(remote->path), "/");
    remote->connected = 0;
    remote->marks = remote_marks;
    remote->mark_cap = remote_cap;

    state->dock.state = UI_DOCK_IDLE;
    state->sites.count = 0;
    state->sites.selected = 0;
    state->editor.port = 21;
    state->editor.anonymous = 1;
    state->editor.field = UI_FIELD_NAME;
    state->server.run = UI_SERVER_OFF;
    state->server.port = 1337;
    state->server.focus_start = 1;
    state->actions.selected = 0;
    for (int i = 0; i < UI_ACTION_COUNT; ++i)
        state->actions.enabled[i] = 1;
}

void ui_state_set_rows(UiState *state, UiPane pane, const UiRow *rows,
                       int count, int truncated)
{
    if (state == NULL || pane < 0 || pane >= UI_PANE_COUNT)
        return;
    UiPaneState *p = &state->panes[pane];
    p->rows = rows;
    p->row_count = count;
    p->truncated = truncated;
    p->loading = 0;
    p->empty = count == 0;
    clamp_pane(p);
}

int ui_state_mark_count(const UiState *state, UiPane pane)
{
    if (state == NULL || pane < 0 || pane >= UI_PANE_COUNT)
        return 0;
    return state->panes[pane].mark_count;
}

int ui_state_is_marked(const UiState *state, UiPane pane, const char *path)
{
    if (state == NULL || pane < 0 || pane >= UI_PANE_COUNT || path == NULL)
        return 0;
    return mark_index(&state->panes[pane], path) >= 0;
}

const UiRow *ui_state_selected_row(const UiState *state, UiPane pane)
{
    if (state == NULL || pane < 0 || pane >= UI_PANE_COUNT)
        return NULL;
    return selected_row(state, pane);
}

int ui_state_handle(UiState *state, const UiStateEvent *event, UiCommand *out,
                    int max)
{
    if (state == NULL || event == NULL)
        return 0;

    int count = 0;
    switch (event->kind) {
    case UI_EVT_KEY:
        handle_key(state, event, out, max, &count);
        break;
    case UI_EVT_STICK:
        if (state->screen == UI_SCREEN_WORKSPACE &&
            state->focus == UI_FOCUS_LIST) {
            scroll_rows(&state->panes[state->active_pane], event->stick_y);
        }
        break;
    case UI_EVT_LISTING_BEGIN: {
        if (event->pane < 0 || event->pane >= UI_PANE_COUNT)
            break;
        UiPaneState *pane = &state->panes[event->pane];
        const char *next = event->path[0] != '\0'
                               ? event->path
                               : (pane->pending_path[0] != '\0'
                                      ? pane->pending_path
                                      : pane->path);
        if (volume_changed(event->pane, pane->path, next))
            clear_marks(pane);
        int path_changed = strcmp(pane->path, next) != 0;
        snprintf(pane->path, sizeof(pane->path), "%s", next);
        pane->pending_path[0] = '\0';
        pane->loading = 1;
        pane->empty = 0;
        pane->session_lost = 0;
        if (path_changed) {
            pane->rows = NULL;
            pane->row_count = 0;
            pane->selected = 0;
            pane->scroll = 0;
            pane->truncated = 0;
        }
        break;
    }
    case UI_EVT_SET_ROWS:
        ui_state_set_rows(state, event->pane, event->rows, event->row_count,
                          event->truncated);
        break;
    case UI_EVT_LISTING_END:
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            UiPaneState *pane = &state->panes[event->pane];
            pane->loading = 0;
            pane->empty = pane->row_count == 0;
            if (!event->ok)
                snprintf(pane->error, sizeof(pane->error), "%s",
                         event->text);
        }
        break;
    case UI_EVT_SESSION_LOST:
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            UiPaneState *pane = &state->panes[event->pane];
            pane->connected = 0;
            pane->session_lost = 1;
            pane->loading = 0;
            snprintf(pane->error, sizeof(pane->error), "%s", event->text);
            clear_marks(pane);
        }
        break;
    case UI_EVT_SESSION_OK:
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            UiPaneState *pane = &state->panes[event->pane];
            pane->connected = 1;
            pane->session_lost = 0;
            pane->error[0] = '\0';
        }
        break;
    case UI_EVT_ERROR:
        set_status(state, event->text);
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT)
            snprintf(state->panes[event->pane].error,
                     sizeof(state->panes[event->pane].error), "%s",
                     event->text);
        break;
    case UI_EVT_TRANSFER_STARTED:
        state->dock.active = 1;
        if (state->dock.focused)
            set_focus(state, UI_FOCUS_LIST);
        state->dock.direction = event->pane == UI_PANE_LOCAL
                                    ? XFER_DIR_UPLOAD
                                    : XFER_DIR_DOWNLOAD;
        snprintf(state->dock.name, sizeof(state->dock.name), "%s",
                 event->text);
        set_transfer_dock(state, event, UI_DOCK_ACTIVE);
        state->dock.done = 0;
        break;
    case UI_EVT_TRANSFER_PROGRESS:
        if (!state->dock.active && (state->dock.state == UI_DOCK_DONE ||
                                    state->dock.state == UI_DOCK_FAILED))
            break;
        set_transfer_dock(state, event, UI_DOCK_ACTIVE);
        break;
    case UI_EVT_TRANSFER_DONE:
        state->dock.active = 0;
        if (state->dock.focused)
            set_focus(state, UI_FOCUS_LIST);
        set_transfer_dock(state, event, UI_DOCK_DONE);
        break;
    case UI_EVT_TRANSFER_FAILED:
        state->dock.active = 0;
        if (state->dock.focused)
            set_focus(state, UI_FOCUS_LIST);
        set_transfer_dock(state, event, UI_DOCK_FAILED);
        break;
    case UI_EVT_BATCH_DONE:
        state->dock.active = 0;
        if (state->dock.focused)
            set_focus(state, UI_FOCUS_LIST);
        state->dock.state = UI_DOCK_DONE;
        /* D18: a completed batch does not clear marks. */
        break;
    case UI_EVT_SERVER_STATE:
        state->server.run = (UiServerRun)event->value;
        state->server.clients = event->clients;
        break;
    case UI_EVT_CONFIRM_REQUEST:
        if (state->screen == UI_SCREEN_CONFIRM) {
            set_status(state, "CONFIRM BUSY");
            break;
        }
        state->confirm_from_worker = 1;
        open_confirm(state, event->confirm);
        state->confirm.items = event->items;
        state->confirm.item_count = event->item_count;
        if (event->high_risk) {
            state->confirm.high_risk = 1;
            snprintf(state->confirm.risk_path,
                     sizeof(state->confirm.risk_path), "%s",
                     event->risk_path);
        }
        break;
    case UI_EVT_IME_RESULT:
        if (!state->text.open)
            break;
        state->text.open = 0;
        if (event->ok) {
            UiEditor *editor = &state->editor;
            const char *value = event->text;
            switch (state->text.field) {
            case UI_FIELD_NAME:
                snprintf(editor->name, sizeof(editor->name), "%s", value);
                break;
            case UI_FIELD_HOST:
                snprintf(editor->host, sizeof(editor->host), "%s", value);
                break;
            case UI_FIELD_PORT: {
                int port = 0;
                if (sscanf(value, "%d", &port) == 1)
                    editor->port = port;
                break;
            }
            case UI_FIELD_USERNAME:
                snprintf(editor->username, sizeof(editor->username), "%s",
                         value);
                break;
            case UI_FIELD_PASSWORD:
                snprintf(editor->password, sizeof(editor->password), "%s",
                         value);
                editor->password_set = value[0] != '\0';
                break;
            default:
                break;
            }
            editor->dirty = 1;
            editor->field = state->text.field;
        }
        break;
    case UI_EVT_SITE_SAVED:
        if (event->ok) {
            state->editor.dirty = 0;
            state->editor.error[0] = '\0';
        } else {
            snprintf(state->editor.error, sizeof(state->editor.error), "%s",
                     event->text);
        }
        break;
    case UI_EVT_SITES:
        state->sites.count = event->value;
        if (state->sites.selected >= state->sites.count)
            state->sites.selected = state->sites.count > 0
                                        ? state->sites.count - 1
                                        : 0;
        break;
    default:
        break;
    }

    return count;
}
