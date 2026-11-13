/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/uistate.h"
#include <stdio.h>
#include <string.h>

#include "core/fmt.h"
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
    fmt_copy(state->status, sizeof(state->status), text != NULL ? text : "");
}

static void begin_remote_connect(UiState *state);

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
        if (strchr(name, ':') != NULL) {
            int n = snprintf(out, cap, "%s", name);
            return n > 0 && (size_t)n < cap;
        }
        Path joined;
        if (path_join(pane_state->path, name, &joined) != PATH_OK)
            return 0;
        int n = snprintf(out, cap, "%s", joined.text);
        return n > 0 && (size_t)n < cap;
    }

    size_t len = strlen(pane_state->path);
    int slash = len > 0 && pane_state->path[len - 1] == '/';
    int n;
    if (strcmp(pane_state->path, "/") == 0)
        n = snprintf(out, cap, "/%s", name);
    else if (slash)
        n = snprintf(out, cap, "%s%s", pane_state->path, name);
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

/* A remote path is not a Vita path, so path_parent cannot parse it. */
static int remote_parent_path(const char *path, char *out, size_t cap)
{
    if (path == NULL || path[0] != '/' || cap == 0)
        return 0;
    const char *slash = NULL;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/')
            slash = p;
    }
    if (slash == NULL || slash == path) {
        if (cap < 2)
            return 0;
        out[0] = '/';
        out[1] = '\0';
        return 1;
    }
    size_t len = (size_t)(slash - path);
    if (len + 1 > cap)
        return 0;
    memcpy(out, path, len);
    out[len] = '\0';
    return 1;
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
    if (old_token[0] == '\0' || new_token[0] == '\0')
        return 0;
    return strcmp(old_token, new_token) != 0;
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
    fmt_copy(mark->path.text, sizeof(mark->path.text), path);
    mark->size = row->size;
    mark->size_known = row->size_known;
    mark->type = row->type;
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
        fmt_copy(state->dock.message, sizeof(state->dock.message), event->text);
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
                 "THE REPLACED FILES CANNOT BE RESTORED.");
        break;
    case UI_CONFIRM_CANCEL_XFER:
        c->choices[0] = UI_CHOICE_KEEP;
        c->choices[1] = UI_CHOICE_CANCEL;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "CANCEL THE TRANSFER");
        snprintf(c->warning, sizeof(c->warning),
                 "THE PARTIAL FILE IS REMOVED.");
        break;
    case UI_CONFIRM_STOP_SERVER:
        c->choices[0] = UI_CHOICE_KEEP;
        c->choices[1] = UI_CHOICE_STOP;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "STOP THE SERVER");
        snprintf(c->warning, sizeof(c->warning),
                 "THE PARTIAL FILE IS REMOVED.");
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

static int action_for_pane(UiPane pane, UiAction action, UiClipMode clip)
{
    if (pane == UI_PANE_LOCAL) {
        if (action == UI_ACTION_DOWNLOAD || action == UI_ACTION_REFRESH ||
            action == UI_ACTION_DISCONNECT)
            return 0;
        if (clip == UI_CLIP_NONE)
            return action != UI_ACTION_PASTE &&
                   action != UI_ACTION_CLEAR_CLIPBOARD;
        return action != UI_ACTION_COPY && action != UI_ACTION_MOVE;
    }
    return action == UI_ACTION_DOWNLOAD || action == UI_ACTION_NEW_FOLDER ||
           action == UI_ACTION_RENAME || action == UI_ACTION_DELETE ||
           action == UI_ACTION_REFRESH || action == UI_ACTION_DISCONNECT;
}

static void prepare_visible(UiActions *actions, UiPane pane, UiClipMode clip)
{
    actions->pane = (int)pane;
    actions->visible_count = 0;
    for (int i = 0; i < UI_ACTION_COUNT; ++i) {
        if (action_for_pane(pane, (UiAction)i, clip))
            actions->visible[actions->visible_count++] = i;
    }
    if (actions->visible_count == 0)
        return;
    actions->selected = actions->visible[0];
}

/* True when every clipboard source is already in `folder`. */
static int clip_all_in_folder(const UiState *state, const char *folder)
{
    if (state->clip_count <= 0)
        return 0;
    for (int i = 0; i < state->clip_count; ++i) {
        Path parent;
        if (path_parent(state->clip_paths[i].text, &parent) != PATH_OK ||
            !ci_equal(parent.text, folder))
            return 0;
    }
    return 1;
}

static void disable(UiActions *actions, UiAction action, const char *reason)
{
    actions->enabled[action] = 0;
    fmt_copy(actions->reason[action], sizeof(actions->reason[action]), reason);
}

void ui_actions_prepare(UiState *state)
{
    if (state == NULL)
        return;

    UiActions *actions = &state->actions;
    UiPane pane_index = state->active_pane;
    UiPaneState *pane = &state->panes[pane_index];
    /* Never read the row arena while a listing is being written. */
    const UiRow *row = pane->loading ? NULL : selected_row(state, pane_index);

    for (int i = 0; i < UI_ACTION_COUNT; ++i) {
        actions->enabled[i] = 1;
        actions->reason[i][0] = '\0';
    }
    prepare_visible(actions, pane_index, state->clip_mode);

    /*
     * A selection is a row that is not "..", or any mark. The cursor sitting
     * on ".." means the folder itself, and no file action may target it from
     * inside it. NEW FOLDER is the one action that still uses the folder.
     */
    int has_selection = pane->mark_count > 0 ||
                        (row != NULL && row->type != UI_ROW_UP);
    actions->has_selection = has_selection;

    char source[PATH_MAX_LEN];
    source[0] = '\0';
    if (pane->mark_count > 0)
        fmt_copy(source, sizeof(source), pane->marks[0].path.text);
    else if (has_selection)
        join_path(pane_index, pane, row->name, source, sizeof(source));
    else
        fmt_copy(source, sizeof(source), pane->path);

    actions->marked = pane->mark_count;
    fmt_copy(actions->path, sizeof(actions->path), source);
    fmt_copy(actions->dest, sizeof(actions->dest), pane->path);
    fmt_copy(actions->other, sizeof(actions->other), state->panes[other_pane(pane_index)].path);

    /*
     * The lock belongs to the selected item, not to the folder the cursor is
     * in. A file inside ux0: is not the volume root; treating it as one made
     * every action unavailable while browsing a volume root.
     */
    int virtual_root = pane_index == UI_PANE_LOCAL &&
                       strcmp(pane->path, "*") == 0;
    int source_is_root =
        has_selection &&
        (pane_index == UI_PANE_LOCAL
             ? (strcmp(source, "*") == 0 || path_is_volume_root(source))
             : strcmp(source, "/") == 0);
    int protected_source = 0;
    PathRisk risk = PATH_RISK_NORMAL;
    if (has_selection && pane_index == UI_PANE_LOCAL && source[0] != '\0') {
        PathResult check = path_check_delete(source, 0, &risk);
        if (check == PATH_PROTECTED || check == PATH_DENIED)
            protected_source = 1;
    }
    int unsafe_source = has_selection && row != NULL && row->unsafe != 0;
    int source_is_dir = 0;
    if (pane->mark_count > 0) {
        for (int i = 0; i < pane->mark_count; ++i) {
            if (pane->marks[i].type == UI_ROW_DIR)
                source_is_dir = 1;
        }
    } else if (row != NULL && row->type == UI_ROW_DIR) {
        source_is_dir = 1;
    }

    if (pane_index == UI_PANE_LOCAL) {
        if (pane->mark_count == 0) {
            disable(actions, UI_ACTION_COPY, "MARK TO COPY");
            disable(actions, UI_ACTION_MOVE, "MARK TO MOVE");
        }
        if (!has_selection) {
            disable(actions, UI_ACTION_COPY, "NO SELECTION");
            disable(actions, UI_ACTION_MOVE, "NO SELECTION");
            disable(actions, UI_ACTION_UPLOAD, "NO SELECTION");
            disable(actions, UI_ACTION_RENAME, "NO SELECTION");
            disable(actions, UI_ACTION_DELETE, "NO SELECTION");
        } else if (source_is_root || protected_source) {
            const char *why = strcmp(source, "*") == 0
                                  ? "ROOT"
                                  : (source_is_root ? "VOLUME ROOT"
                                                    : "PROTECTED PATH");
            disable(actions, UI_ACTION_COPY, why);
            disable(actions, UI_ACTION_MOVE, why);
            disable(actions, UI_ACTION_RENAME, why);
            disable(actions, UI_ACTION_DELETE, why);
        }
        if (has_selection && source_is_dir)
            disable(actions, UI_ACTION_UPLOAD, "FOLDER NOT SUPPORTED");
        if (virtual_root)
            disable(actions, UI_ACTION_NEW_FOLDER, "ROOT");
        if (pane->mark_count > 1)
            disable(actions, UI_ACTION_RENAME, "ONE ITEM ONLY");
        if (state->clip_mode != UI_CLIP_NONE) {
            if (state->clip_count <= 0)
                disable(actions, UI_ACTION_PASTE, "EMPTY CLIPBOARD");
            if (virtual_root)
                disable(actions, UI_ACTION_PASTE, "ROOT");
            if (state->clip_mode == UI_CLIP_MOVE &&
                clip_all_in_folder(state, pane->path))
                disable(actions, UI_ACTION_PASTE, "ALREADY HERE");
        }
    } else {
        if (!pane->connected) {
            disable(actions, UI_ACTION_DOWNLOAD, "NOT CONNECTED");
            disable(actions, UI_ACTION_NEW_FOLDER, "NOT CONNECTED");
            disable(actions, UI_ACTION_RENAME, "NOT CONNECTED");
            disable(actions, UI_ACTION_DELETE, "NOT CONNECTED");
            disable(actions, UI_ACTION_REFRESH, "NOT CONNECTED");
            disable(actions, UI_ACTION_DISCONNECT, "NOT CONNECTED");
        } else if (pane->loading) {
            /* The rows on screen belong to the previous folder until the
             * listing ends. Only DISCONNECT is safe to run while READING. */
            disable(actions, UI_ACTION_DOWNLOAD, "READING");
            disable(actions, UI_ACTION_NEW_FOLDER, "READING");
            disable(actions, UI_ACTION_RENAME, "READING");
            disable(actions, UI_ACTION_DELETE, "READING");
            disable(actions, UI_ACTION_REFRESH, "READING");
        } else if (!has_selection) {
            disable(actions, UI_ACTION_DOWNLOAD, "NO SELECTION");
            disable(actions, UI_ACTION_RENAME, "NO SELECTION");
            disable(actions, UI_ACTION_DELETE, "NO SELECTION");
            /* NEW FOLDER is the one action that uses the folder itself. */
        } else if (unsafe_source) {
            disable(actions, UI_ACTION_DOWNLOAD, "UNSAFE NAME");
            disable(actions, UI_ACTION_RENAME, "UNSAFE NAME");
            disable(actions, UI_ACTION_DELETE, "UNSAFE NAME");
        } else if (source_is_dir) {
            disable(actions, UI_ACTION_DOWNLOAD, "FOLDER NOT SUPPORTED");
        } else if (source_is_root) {
            disable(actions, UI_ACTION_RENAME, "REMOTE ROOT");
            disable(actions, UI_ACTION_DELETE, "REMOTE ROOT");
        }
        if (pane->mark_count > 1)
            disable(actions, UI_ACTION_RENAME, "ONE ITEM ONLY");
        if (state->dock.active &&
            (state->dock.direction == XFER_DIR_DOWNLOAD ||
             state->dock.direction == XFER_DIR_UPLOAD))
            disable(actions, UI_ACTION_DISCONNECT, "TRANSFER IN PROGRESS");
    }

    if (has_selection &&
        ((pane_index == UI_PANE_LOCAL &&
          !state->panes[UI_PANE_REMOTE].connected) ||
         (pane_index == UI_PANE_REMOTE && !pane->connected)))
        disable(actions, UI_ACTION_UPLOAD, "NOT CONNECTED");
    if (pane_index == UI_PANE_LOCAL && has_selection &&
        state->panes[UI_PANE_REMOTE].loading)
        disable(actions, UI_ACTION_UPLOAD, "REMOTE READING");
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
            begin_remote_connect(state);
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

    if (pane->loading &&
        (key == UI_KEY_CROSS || key == UI_KEY_SQUARE)) {
        if (state->active_pane == UI_PANE_REMOTE)
            set_status(state, "READING");
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
        if (row->unsafe) {
            set_status(state, "UNSAFE NAME");
            return;
        }
        if (row->type == UI_ROW_UP) {
            UiCommand *c = emit(out, max, count, UI_CMD_GO_PARENT);
            if (c != NULL)
                c->pane = state->active_pane;
            return;
        }

        char full[PATH_MAX_LEN];
        if (row->type == UI_ROW_VOLUME) {
            fmt_copy(full, sizeof(full), row->name);
        } else if (!join_path(state->active_pane, pane, row->name, full,
                              sizeof(full))) {
            set_status(state, "PATH TOO LONG");
            return;
        }

        if (row->type == UI_ROW_DIR || row->type == UI_ROW_VOLUME) {
            if (state->active_pane == UI_PANE_REMOTE && !pane->connected) {
                set_status(state, "NOT CONNECTED");
                return;
            }
            if (state->active_pane == UI_PANE_REMOTE && pane->loading) {
                set_status(state, "READING");
                return;
            }
            fmt_copy(pane->pending_path, sizeof(pane->pending_path), full);
            pane->loading = 1;
            UiCommand *c = emit(out, max, count, UI_CMD_OPEN_PATH);
            if (c != NULL) {
                c->pane = state->active_pane;
                fmt_copy(c->path, sizeof(c->path), full);
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
                if (pane->marks[i].type == UI_ROW_DIR) {
                    set_status(state, "FOLDER TRANSFER NOT SUPPORTED");
                    return;
                }
            }
            for (int i = 0; i < pane->mark_count; ++i) {
                UiCommand *c = emit(out, max, count, UI_CMD_TRANSFER);
                if (c == NULL)
                    break;
                c->pane = state->active_pane;
                c->source_type = pane->marks[i].type;
                c->total = pane->marks[i].size;
                c->total_known = pane->marks[i].size_known;
                fmt_copy(c->path, sizeof(c->path), pane->marks[i].path.text);
                fmt_copy(c->path2, sizeof(c->path2), state->panes[other].path);
                c->value = pane->mark_count;
            }
            return;
        }

        UiCommand *c = emit(out, max, count, UI_CMD_TRANSFER);
        if (c != NULL) {
            c->pane = state->active_pane;
            c->source_type = row->type;
            c->total = row->size;
            c->total_known = row->size_known;
            fmt_copy(c->path, sizeof(c->path), full);
            fmt_copy(c->path2, sizeof(c->path2), state->panes[other].path);
            c->value = 0;
        }
        return;
    }
    case UI_KEY_CIRCLE:
        if (state->active_pane == UI_PANE_LOCAL &&
            strcmp(pane->path, "*") == 0) {
            set_status(state, "AT ROOT");
            return;
        }
        if (state->active_pane == UI_PANE_LOCAL &&
            path_is_volume_root(pane->path)) {
            UiCommand *c = emit(out, max, count, UI_CMD_GO_PARENT);
            if (c != NULL)
                c->pane = state->active_pane;
            return;
        }
        if (state->active_pane == UI_PANE_REMOTE && !pane->connected) {
            set_status(state, "NOT CONNECTED");
            return;
        }
        if (state->active_pane == UI_PANE_REMOTE && pane->loading) {
            set_status(state, "READING");
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
        if (row == NULL || row->type == UI_ROW_UP ||
            row->type == UI_ROW_VOLUME)
            return;
        if (row->unsafe) {
            set_status(state, "UNSAFE NAME");
            return;
        }
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
        enter_screen(state, UI_SCREEN_ACTIONS);
        ui_actions_prepare(state);
        return;
    case UI_KEY_START:
        enter_screen(state, UI_SCREEN_SERVER);
        return;
    case UI_KEY_SELECT:
        enter_screen(state, UI_SCREEN_SITES);
        return;
    default:
        return;
    }
}

static void clear_remote_session(UiState *state)
{
    UiPaneState *remote = &state->panes[UI_PANE_REMOTE];

    remote->connected = 0;
    remote->session_lost = 0;
    remote->loading = 0;
    remote->empty = 0;
    remote->rows = NULL;
    remote->row_count = 0;
    remote->selected = 0;
    remote->scroll = 0;
    remote->truncated = 0;
    remote->pending_path[0] = '\0';
    remote->error[0] = '\0';
    remote->site[0] = '\0';
    snprintf(remote->path, sizeof(remote->path), "/");
    clear_marks(remote);

    state->site_name[0] = '\0';
    state->site_host[0] = '\0';
    state->site_port = 21;
    state->site_known = 0;
    state->sites.connecting = 0;
    state->sites.login_failed = 0;
    state->sites.reply[0] = '\0';
}

static void begin_remote_connect(UiState *state)
{
    clear_remote_session(state);
    state->panes[UI_PANE_REMOTE].loading = 1;
    state->sites.connecting = 1;
}

static void handle_sites_key(UiState *state, const UiStateEvent *event,
                             UiCommand *out, int max, int *count)
{
    switch (event->key) {
    case UI_KEY_UP:
        if (state->sites.selected > 0)
            state->sites.selected--;
        if (state->sites.selected < state->sites.first)
            state->sites.first = state->sites.selected;
        return;
    case UI_KEY_DOWN:
        if (state->sites.selected + 1 < state->sites.count)
            state->sites.selected++;
        if (state->sites.selected >= state->sites.first + 6)
            state->sites.first = state->sites.selected - 5;
        return;
    case UI_KEY_CROSS: {
        if (state->sites.count <= 0) {
            set_status(state, "NO SITES");
            return;
        }
        if (state->dock.active &&
            (state->dock.direction == XFER_DIR_DOWNLOAD ||
             state->dock.direction == XFER_DIR_UPLOAD)) {
            set_status(state, "TRANSFER IN PROGRESS");
            return;
        }
        begin_remote_connect(state);
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
        state->editor.new_site = 0;
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
        state->editor.new_site = 1;
        state->editor.password_set = 0;
        enter_screen(state, UI_SCREEN_SITE_EDITOR);
        return;
    }
    case UI_KEY_SELECT:
        open_confirm(state, UI_CONFIRM_DELETE_SITE);
        if (state->sites.selected < state->sites.count)
            fmt_copy(state->confirm.context, sizeof(state->confirm.context),
                     state->sites.rows[state->sites.selected].name);
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
                fmt_copy(c->title, sizeof(c->title), field_title((UiField)editor->field));
                fmt_copy(c->text, sizeof(c->text), value);
            }
            state->text.open = 1;
            state->text.field = (UiField)editor->field;
            state->text.masked = editor->field == UI_FIELD_PASSWORD;
            state->text.max = field_max((UiField)editor->field);
            fmt_copy(state->text.title, sizeof(state->text.title), field_title((UiField)editor->field));
            fmt_copy(state->text.value, sizeof(state->text.value), value);
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
            if (c != NULL) {
                c->index = editor->new_site ? -1 : state->sites.selected;
                fmt_copy(c->site.name, sizeof(c->site.name), editor->name);
                fmt_copy(c->site.host, sizeof(c->site.host), editor->host);
                c->site.port = (uint16_t)editor->port;
                c->site.anonymous = editor->anonymous;
                fmt_copy(c->site.username, sizeof(c->site.username), editor->username);
                fmt_copy(c->site.password, sizeof(c->site.password), editor->password);
            }
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
        } else if (state->dock.active &&
                   (state->dock.direction == XFER_DIR_SERVER_UPLOAD ||
                    state->dock.direction == XFER_DIR_SERVER_DOWNLOAD)) {
            open_confirm(state, UI_CONFIRM_STOP_SERVER);
            char context[320];
            snprintf(context, sizeof(context), "%s %s",
                     state->dock.direction == XFER_DIR_SERVER_UPLOAD
                         ? "UPLOAD"
                         : "DOWNLOAD",
                     state->dock.name);
            fmt_copy(state->confirm.context,
                     sizeof(state->confirm.context), context);
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

static const char *last_segment(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == ':')
            name = p + 1;
    }
    return name;
}

static void handle_actions_key(UiState *state, const UiStateEvent *event,
                               UiCommand *out, int max, int *count)
{
    switch (event->key) {
    case UI_KEY_UP: {
        for (int i = 0; i < state->actions.visible_count; ++i) {
            if (state->actions.visible[i] == state->actions.selected) {
                if (i > 0)
                    state->actions.selected = state->actions.visible[i - 1];
                return;
            }
        }
        return;
    }
    case UI_KEY_DOWN: {
        for (int i = 0; i < state->actions.visible_count; ++i) {
            if (state->actions.visible[i] == state->actions.selected) {
                if (i + 1 < state->actions.visible_count)
                    state->actions.selected = state->actions.visible[i + 1];
                return;
            }
        }
        return;
    }
    case UI_KEY_CROSS: {
        UiAction action = (UiAction)state->actions.selected;
        if (!state->actions.enabled[action]) {
            set_status(state, "ACTION UNAVAILABLE");
            return;
        }

        const UiPaneState *pane = &state->panes[state->active_pane];
        const char *destination =
            (action == UI_ACTION_UPLOAD || action == UI_ACTION_DOWNLOAD)
                ? state->actions.other
                : state->actions.dest;

        /* COPY and MOVE arm the clipboard. Nothing is touched until PASTE,
         * after the user has navigated to the destination folder. */
        if (action == UI_ACTION_COPY || action == UI_ACTION_MOVE) {
            if (pane->mark_count <= 0) {
                set_status(state, "MARK ITEMS FIRST");
                return;
            }
            if (pane->mark_count > UI_CLIP_MAX) {
                set_status(state, "TOO MANY ITEMS");
                return;
            }
            state->clip_mode =
                action == UI_ACTION_COPY ? UI_CLIP_COPY : UI_CLIP_MOVE;
            state->clip_count = pane->mark_count;
            for (int i = 0; i < state->clip_count; ++i)
                state->clip_paths[i] = pane->marks[i].path;
            leave_screen(state);
            return;
        }

        if (action == UI_ACTION_CLEAR_CLIPBOARD) {
            state->clip_mode = UI_CLIP_NONE;
            state->clip_count = 0;
            leave_screen(state);
            return;
        }

        if (action == UI_ACTION_PASTE) {
            if (state->clip_mode == UI_CLIP_NONE || state->clip_count <= 0) {
                set_status(state, "EMPTY CLIPBOARD");
                return;
            }

            int high_risk = 0;
            char risk_path[PATH_MAX_LEN];
            risk_path[0] = '\0';
            for (int i = 0; i < state->clip_count; ++i) {
                Path target;
                if (path_join(destination,
                              last_segment(state->clip_paths[i].text),
                              &target) != PATH_OK) {
                    set_status(state, "PATH TOO LONG");
                    return;
                }
                PathRisk risk = PATH_RISK_NORMAL;
                PathResult result = path_check_write(target.text, 0, &risk);
                if (result != PATH_OK) {
                    set_status(state, protection_reason(result, target.text));
                    return;
                }
                if (risk == PATH_RISK_HIGH && !high_risk) {
                    high_risk = 1;
                    fmt_copy(risk_path, sizeof(risk_path), target.text);
                }
            }

            state->pending_action = action;
            state->pending_action_valid = 0;
            state->pending_pane = state->active_pane;
            fmt_copy(state->pending_path.text, sizeof(state->pending_path.text),
                     state->clip_paths[0].text);
            fmt_copy(state->pending_dest.text, sizeof(state->pending_dest.text),
                     destination);
            state->confirm_from_worker = 0;

            if (high_risk) {
                open_confirm(state, UI_CONFIRM_HIGH_RISK);
                state->confirm_return = UI_SCREEN_WORKSPACE;
                fmt_copy(state->confirm.risk_path,
                         sizeof(state->confirm.risk_path), risk_path);
                state->confirm.high_risk = 1;
                return;
            }

            for (int i = 0; i < state->clip_count; ++i) {
                UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
                if (c == NULL) {
                    set_status(state, "COMMAND LIMIT");
                    break;
                }
                c->action = state->clip_mode == UI_CLIP_MOVE ? UI_ACTION_MOVE
                                                             : UI_ACTION_COPY;
                c->pane = state->active_pane;
                fmt_copy(c->path, sizeof(c->path),
                         state->clip_paths[i].text);
                fmt_copy(c->path2, sizeof(c->path2), destination);
            }
            state->clip_mode = UI_CLIP_NONE;
            state->clip_count = 0;
            leave_screen(state);
            return;
        }

        if (action == UI_ACTION_DISCONNECT) {
            clear_remote_session(state);
            UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
            if (c != NULL) {
                c->action = action;
                c->pane = UI_PANE_REMOTE;
            }
            leave_screen(state);
            set_status(state, "DISCONNECTED");
            return;
        }

        if (action == UI_ACTION_REFRESH) {
            UiCommand *c = emit(out, max, count, UI_CMD_REFRESH);
            if (c != NULL)
                c->pane = UI_PANE_REMOTE;
            leave_screen(state);
            return;
        }

        if (!state->actions.has_selection && action != UI_ACTION_NEW_FOLDER) {
            set_status(state, "NO SELECTION");
            return;
        }

        const char *sources[UI_CONFIRM_MAX];
        int source_types[UI_CONFIRM_MAX];
        uint64_t source_sizes[UI_CONFIRM_MAX];
        int source_known[UI_CONFIRM_MAX];
        int source_count = 0;
        int use_marks = pane->mark_count > 0 &&
                        (action == UI_ACTION_DELETE ||
                         action == UI_ACTION_DOWNLOAD ||
                         action == UI_ACTION_UPLOAD);
        if (use_marks) {
            for (int i = 0; i < pane->mark_count && i < UI_CONFIRM_MAX; ++i) {
                sources[source_count] = pane->marks[i].path.text;
                source_types[source_count] = pane->marks[i].type;
                source_sizes[source_count] = pane->marks[i].size;
                source_known[source_count] = pane->marks[i].size_known;
                source_count++;
            }
        } else {
            sources[0] = state->actions.path;
            source_types[0] = UI_ROW_FILE;
            source_sizes[0] = 0;
            source_known[0] = 0;
            const UiRow *selected =
                ui_state_selected_row(state, state->active_pane);
            if (selected != NULL) {
                source_types[0] = selected->type;
                source_sizes[0] = selected->size;
                source_known[0] = selected->size_known;
            }
            source_count = 1;
        }

        int source_type = source_types[0];

        int high_risk = 0;
        char risk_path[PATH_MAX_LEN];
        risk_path[0] = '\0';

        /* A remote path is not a Vita path: the local protection table would
         * reject every remote delete as INVALID PATH. Remote deletes are
         * confirmed here and executed by the FTP client. */
        if (action == UI_ACTION_DELETE &&
            state->active_pane == UI_PANE_LOCAL) {
            for (int i = 0; i < source_count; ++i) {
                PathRisk risk = PATH_RISK_NORMAL;
                PathResult result = path_check_delete(sources[i], 0, &risk);
                if (result != PATH_OK) {
                    set_status(state, protection_reason(result, sources[i]));
                    return;
                }
                if (risk == PATH_RISK_HIGH && !high_risk) {
                    high_risk = 1;
                    fmt_copy(risk_path, sizeof(risk_path), sources[i]);
                }
            }
        } else if (action == UI_ACTION_DOWNLOAD && destination[0] != '\0' &&
                   strcmp(destination, "*") != 0) {
            for (int i = 0; i < source_count; ++i) {
                Path target;
                if (path_join(destination, last_segment(sources[i]),
                              &target) != PATH_OK) {
                    set_status(state, "PATH TOO LONG");
                    return;
                }
                PathRisk risk = PATH_RISK_NORMAL;
                PathResult result = path_check_write(target.text, 0, &risk);
                if (result != PATH_OK) {
                    set_status(state, protection_reason(result, target.text));
                    return;
                }
                if (risk == PATH_RISK_HIGH && !high_risk) {
                    high_risk = 1;
                    fmt_copy(risk_path, sizeof(risk_path), target.text);
                }
            }
        }

        state->pending_action = action;
        state->pending_action_valid = 0;
        state->pending_pane = state->active_pane;
        state->pending_source_type = source_type;
        fmt_copy(state->pending_path.text, sizeof(state->pending_path.text),
                 sources[0]);
        fmt_copy(state->pending_dest.text, sizeof(state->pending_dest.text),
                 destination);
        state->confirm_from_worker = 0;

        if (action == UI_ACTION_NEW_FOLDER || action == UI_ACTION_RENAME) {
            state->pending_action_valid = 1;
            state->text.open = 1;
            UiCommand *c = emit(out, max, count, UI_CMD_OPEN_IME);
            if (c != NULL) {
                c->field = UI_FIELD_NAME;
                c->value = 255;
                c->index = 0;
                fmt_copy(c->title, sizeof(c->title),
                         action == UI_ACTION_RENAME ? "RENAME"
                                                    : "NEW FOLDER NAME");
                if (action == UI_ACTION_RENAME)
                    fmt_copy(c->text, sizeof(c->text),
                             last_segment(state->actions.path));
            }
            return;
        }

        if (action == UI_ACTION_DELETE) {
            open_confirm(state, UI_CONFIRM_DELETE);
            state->confirm_return = UI_SCREEN_WORKSPACE;
            UiConfirm *confirm = &state->confirm;
            confirm->items = confirm->local_items;
            confirm->item_count = source_count;
            confirm->high_risk = high_risk;
            if (high_risk) {
                fmt_copy(confirm->risk_path,
                         sizeof(confirm->risk_path), risk_path);
                /* The plain delete warning is not enough inside a tai folder:
                 * name the boot risk as well (R71, D30). */
                snprintf(confirm->warning, sizeof(confirm->warning),
                         "A BAD EDIT CAN STOP THE CONSOLE BOOTING. THERE IS NO UNDO.");
            }
            snprintf(confirm->title, sizeof(confirm->title), "DELETE %d ITEM%s",
                     source_count, source_count == 1 ? "" : "S");
            fmt_copy(confirm->context, sizeof(confirm->context), destination);
            for (int i = 0; i < source_count; ++i) {
                /* Destructive confirmations name the full path. A mark can
                 * outlive a folder change, so a bare name is ambiguous. */
                fmt_copy(confirm->local_paths[i],
                         sizeof(confirm->local_paths[i]), sources[i]);
                confirm->local_items[i].name = confirm->local_paths[i];
                confirm->local_items[i].size = 0;
                confirm->local_items[i].size_known = 0;
                if (pane->mark_count > 0 && i < pane->mark_count) {
                    confirm->local_items[i].size = pane->marks[i].size;
                    confirm->local_items[i].size_known =
                        pane->marks[i].size_known;
                } else {
                    const UiRow *row = selected_row(state,
                                                    state->active_pane);
                    if (row != NULL) {
                        confirm->local_items[i].size = row->size;
                        confirm->local_items[i].size_known = row->size_known;
                    }
                }
            }
            return;
        }

        if (high_risk) {
            open_confirm(state, UI_CONFIRM_HIGH_RISK);
            state->confirm_return = UI_SCREEN_WORKSPACE;
            fmt_copy(state->confirm.risk_path,
                     sizeof(state->confirm.risk_path), risk_path);
            state->confirm.high_risk = 1;
            return;
        }

        if (action == UI_ACTION_UPLOAD || action == UI_ACTION_DOWNLOAD) {
            for (int i = 0; i < source_count; ++i) {
                UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
                if (c == NULL) {
                    set_status(state, "COMMAND LIMIT");
                    break;
                }
                c->action = action;
                c->pane = state->active_pane;
                c->source_type = source_types[i];
                c->total = source_sizes[i];
                c->total_known = source_known[i];
                fmt_copy(c->path, sizeof(c->path), sources[i]);
                fmt_copy(c->path2, sizeof(c->path2), destination);
            }
            leave_screen(state);
            return;
        }

        UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
        if (c != NULL) {
            c->action = action;
            c->pane = state->active_pane;
            c->source_type = source_type;
            c->total = source_sizes[0];
            c->total_known = source_known[0];
            fmt_copy(c->path, sizeof(c->path), sources[0]);
            fmt_copy(c->path2, sizeof(c->path2), destination);
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
        if (!from_worker && (c->kind == UI_CONFIRM_HIGH_RISK ||
                             c->kind == UI_CONFIRM_DELETE)) {
            int approved = c->kind == UI_CONFIRM_HIGH_RISK
                               ? choice == UI_CHOICE_CONTINUE
                               : choice == UI_CHOICE_DELETE;
            int approved_value =
                (c->kind == UI_CONFIRM_HIGH_RISK || c->high_risk) ? 1 : 0;
            if (approved) {
                UiPaneState *pending =
                    &state->panes[state->pending_pane];
                if (state->pending_action == UI_ACTION_PASTE) {
                    for (int i = 0; i < state->clip_count; ++i) {
                        UiCommand *run = emit(out, max, count,
                                              UI_CMD_RUN_ACTION);
                        if (run == NULL)
                            break;
                        run->action = state->clip_mode == UI_CLIP_MOVE
                                          ? UI_ACTION_MOVE
                                          : UI_ACTION_COPY;
                        run->pane = state->pending_pane;
                        run->value = 1;
                        fmt_copy(run->path, sizeof(run->path),
                                 state->clip_paths[i].text);
                        fmt_copy(run->path2, sizeof(run->path2),
                                 state->pending_dest.text);
                    }
                    state->clip_mode = UI_CLIP_NONE;
                    state->clip_count = 0;
                } else if ((state->pending_action == UI_ACTION_DELETE ||
                            state->pending_action == UI_ACTION_COPY ||
                            state->pending_action == UI_ACTION_MOVE ||
                            state->pending_action == UI_ACTION_UPLOAD ||
                            state->pending_action == UI_ACTION_DOWNLOAD) &&
                           pending->mark_count > 0) {
                    for (int i = 0; i < pending->mark_count; ++i) {
                        UiCommand *run = emit(out, max, count,
                                              UI_CMD_RUN_ACTION);
                        if (run == NULL)
                            break;
                        run->action = state->pending_action;
                        run->pane = state->pending_pane;
                        run->source_type = pending->marks[i].type;
                        run->value = approved_value;
                        fmt_copy(run->path, sizeof(run->path), pending->marks[i].path.text);
                        fmt_copy(run->path2, sizeof(run->path2), state->pending_dest.text);
                    }
                } else {
                    UiCommand *run = emit(out, max, count,
                                          UI_CMD_RUN_ACTION);
                    if (run != NULL) {
                        run->action = state->pending_action;
                        run->pane = state->pending_pane;
                        run->source_type = state->pending_source_type;
                        run->value = approved_value;
                        fmt_copy(run->path, sizeof(run->path), state->pending_path.text);
                        fmt_copy(run->path2, sizeof(run->path2), state->pending_dest.text);
                    }
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
        if ((c->kind == UI_CONFIRM_HIGH_RISK ||
             c->kind == UI_CONFIRM_DELETE) &&
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
    if (state->text.unavailable && event->key == UI_KEY_CIRCLE) {
        state->text.unavailable = 0;
        return;
    }
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
    snprintf(local->path, sizeof(local->path), "*");
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
    state->server.address[0] = '\0';
    state->site_port = 21;
    state->actions.selected = 0;
    state->actions.visible_count = 0;
    state->clip_mode = UI_CLIP_NONE;
    state->clip_count = 0;
    for (int i = 0; i < UI_ACTION_COUNT; ++i)
        state->actions.enabled[i] = 1;
}

/* A mark in the folder being listed must match a row. Marks in other folders
 * survive directory changes, but a deleted file must not stay marked and
 * reappear in the next DELETE confirmation. */
static void prune_marks(UiPaneState *pane, int remote)
{
    int keep = 0;
    for (int i = 0; i < pane->mark_count; ++i) {
        UiMark *mark = &pane->marks[i];
        Path local_parent;
        char remote_parent[PATH_MAX_LEN];
        int in_folder;
        if (remote) {
            in_folder =
                remote_parent_path(mark->path.text, remote_parent,
                                   sizeof(remote_parent)) &&
                strcmp(remote_parent, pane->path) == 0;
        } else {
            in_folder = path_parent(mark->path.text, &local_parent) == PATH_OK &&
                        ci_equal(local_parent.text, pane->path);
        }
        int found = 1;
        if (in_folder) {
            const char *name = last_segment(mark->path.text);
            found = 0;
            for (int r = 0; r < pane->row_count; ++r) {
                if (pane->rows[r].type == UI_ROW_UP)
                    continue;
                int same = remote ? strcmp(pane->rows[r].name, name) == 0
                                  : ci_equal(pane->rows[r].name, name);
                if (same) {
                    found = 1;
                    break;
                }
            }
        }
        if (found) {
            if (keep != i)
                pane->marks[keep] = *mark;
            keep++;
        }
    }
    pane->mark_count = keep;
    recompute_marks(pane);
}

void ui_state_set_rows(UiState *state, UiPane pane, const UiRow *rows,
                       int count, int truncated)
{
    if (state == NULL || pane >= UI_PANE_COUNT)
        return;
    UiPaneState *p = &state->panes[pane];
    p->rows = rows;
    p->row_count = count;
    p->truncated = truncated;
    p->loading = 0;
    p->empty = count == 0;
    prune_marks(p, pane == UI_PANE_REMOTE);
    clamp_pane(p);
}

int ui_state_mark_count(const UiState *state, UiPane pane)
{
    if (state == NULL || pane >= UI_PANE_COUNT)
        return 0;
    return state->panes[pane].mark_count;
}

int ui_state_is_marked(const UiState *state, UiPane pane, const char *path)
{
    if (state == NULL || pane >= UI_PANE_COUNT || path == NULL)
        return 0;
    return mark_index(&state->panes[pane], path) >= 0;
}

const UiRow *ui_state_selected_row(const UiState *state, UiPane pane)
{
    if (state == NULL || pane >= UI_PANE_COUNT)
        return NULL;
    /* While a listing is being written the row arena may be reused by the
     * worker; no caller may read a row until the new generation is applied. */
    if (state->panes[pane].loading)
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
        /* A status is a reaction to the previous action. Clear it here so a
         * stale reason cannot masquerade as the result of this key. */
        state->status[0] = '\0';
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
        int new_session = !pane->connected || pane->session_lost;
        fmt_copy(pane->path, sizeof(pane->path), next);
        pane->pending_path[0] = '\0';
        pane->loading = 1;
        pane->empty = 0;
        pane->session_lost = 0;
        pane->error[0] = '\0';
        if (new_session) {
            /* The first listing of a session: no row or mark from the old
             * server may survive a reconnect to the same-looking root. */
            pane->rows = NULL;
            pane->row_count = 0;
            pane->selected = 0;
            pane->scroll = 0;
            pane->truncated = 0;
            clear_marks(pane);
        }
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
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT)
            ui_state_set_rows(state, (UiPane)event->pane, event->rows,
                              event->row_count, event->truncated);
        break;
    case UI_EVT_LISTING_END:
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            UiPaneState *pane = &state->panes[event->pane];
            pane->loading = 0;
            pane->empty = pane->row_count == 0;
            if (!event->ok) {
                pane->rows = NULL;
                pane->row_count = 0;
                pane->selected = 0;
                pane->scroll = 0;
                pane->empty = 1;
                fmt_copy(pane->error, sizeof(pane->error), event->text);
            } else {
                pane->error[0] = '\0';
            }
        }
        break;
    case UI_EVT_SESSION_LOST:
        state->sites.connecting = 0;
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            UiPaneState *pane = &state->panes[event->pane];
            pane->connected = 0;
            pane->session_lost = 1;
            pane->loading = 0;
            pane->pending_path[0] = '\0';
            pane->empty = pane->row_count == 0;
            /* Do not keep showing the dead site as if it were still current. */
            pane->site[0] = '\0';
            if (event->pane == UI_PANE_REMOTE) {
                state->site_name[0] = '\0';
                state->site_host[0] = '\0';
                state->site_port = 21;
                state->site_known = 0;
            }
            fmt_copy(pane->error, sizeof(pane->error), event->text);
            clear_marks(pane);
        }
        if (state->screen == UI_SCREEN_ACTIONS &&
            state->active_pane == UI_PANE_REMOTE) {
            /* The ledger's enabled flags describe the dead session. Return to
             * the workspace and its SESSION LOST prompt. */
            state->screen = UI_SCREEN_WORKSPACE;
            set_focus(state, UI_FOCUS_LIST);
        }
        break;
    case UI_EVT_SESSION_OK:
        state->sites.connecting = 0;
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            UiPaneState *pane = &state->panes[event->pane];
            pane->connected = 1;
            pane->session_lost = 0;
            pane->error[0] = '\0';
        }
        if (event->site[0] != '\0' || event->host[0] != '\0') {
            fmt_copy(state->site_name, sizeof(state->site_name), event->site);
            fmt_copy(state->site_host, sizeof(state->site_host), event->host);
            state->site_port = event->port > 0 ? event->port : 21;
            state->site_known = 1;
            state->sites.login_failed = 0;
            snprintf(state->panes[UI_PANE_REMOTE].site,
                     sizeof(state->panes[UI_PANE_REMOTE].site), "%s",
                     event->site);
        }
        break;
    case UI_EVT_ERROR:
        if (event->pane == UI_PANE_REMOTE) {
            state->sites.connecting = 0;
            /* A command can fail before any LISTING_END exists (busy client,
             * refused CWD, not connected). Clear the eager READING state here
             * or the pane stays on READING forever. */
            UiPaneState *remote = &state->panes[UI_PANE_REMOTE];
            remote->loading = 0;
            remote->pending_path[0] = '\0';
        }
        set_status(state, event->text);
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT)
            fmt_copy(state->panes[event->pane].error,
                     sizeof(state->panes[event->pane].error), event->text);
        break;
    case UI_EVT_TRANSFER_STARTED:
        state->dock.active = 1;
        if (state->dock.focused)
            set_focus(state, UI_FOCUS_LIST);
        if (event->direction_known)
            state->dock.direction = (XferDirection)event->direction;
        else
            state->dock.direction = event->pane == UI_PANE_LOCAL
                                        ? XFER_DIR_UPLOAD
                                        : XFER_DIR_DOWNLOAD;
        fmt_copy(state->dock.name, sizeof(state->dock.name), event->text);
        set_transfer_dock(state, event, UI_DOCK_ACTIVE);
        state->dock.done = 0;
        state->dock.item_index = event->item_index;
        state->dock.item_total = event->item_total;
        state->dock.elapsed_ms = 0;
        state->dock.elapsed_known = 0;
        state->dock.rate_known = 0;
        break;
    case UI_EVT_TRANSFER_PROGRESS:
        if (!state->dock.active && (state->dock.state == UI_DOCK_DONE ||
                                    state->dock.state == UI_DOCK_FAILED))
            break;
        set_transfer_dock(state, event, UI_DOCK_ACTIVE);
        state->dock.rate = event->rate;
        state->dock.rate_known = event->rate_known;
        state->dock.elapsed_ms = event->elapsed_ms;
        state->dock.elapsed_known = event->elapsed_known;
        if (event->item_index > 0)
            state->dock.item_index = event->item_index;
        if (event->item_total > 0)
            state->dock.item_total = event->item_total;
        break;
    case UI_EVT_TRANSFER_DONE:
        state->dock.active = 0;
        if (state->dock.focused)
            set_focus(state, UI_FOCUS_LIST);
        set_transfer_dock(state, event, UI_DOCK_DONE);
        state->dock.rate = event->rate;
        state->dock.rate_known = event->rate_known;
        state->dock.elapsed_ms = event->elapsed_ms;
        state->dock.elapsed_known = event->elapsed_known;
        break;
    case UI_EVT_TRANSFER_CANCELLED:
        state->dock.active = 0;
        if (state->dock.focused)
            set_focus(state, UI_FOCUS_LIST);
        state->dock.state = UI_DOCK_IDLE;
        state->dock.message[0] = '\0';
        set_status(state, "CANCELLED");
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
        if (event->port > 0)
            state->server.port = event->port;
        if (event->address_known) {
            fmt_copy(state->server.address, sizeof(state->server.address), event->address);
            state->server.address_known = 1;
        } else if (event->address[0] == '\0') {
            state->server.address_known = 0;
            state->server.address[0] = '\0';
        }
        if (state->server.run == UI_SERVER_OFF)
            state->server.client_active = 0;
        break;
    case UI_EVT_SERVER_CLIENT:
        state->server.client_active = event->ok;
        state->server.clients = event->clients;
        snprintf(state->server.client_addr,
                 sizeof(state->server.client_addr), "%s", event->address);
        fmt_copy(state->server.client_op, sizeof(state->server.client_op), event->op);
        snprintf(state->server.client_path,
                 sizeof(state->server.client_path), "%s", event->path);
        state->server.client_bytes = event->bytes;
        state->server.client_total = event->total;
        state->server.client_total_known = event->total_known;
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
        state->confirm.existing_count = event->existing_count;
        if (event->high_risk) {
            state->confirm.high_risk = 1;
            snprintf(state->confirm.risk_path,
                     sizeof(state->confirm.risk_path), "%s",
                     event->risk_path);
        }
        break;
    case UI_EVT_IME_RESULT:
        if (state->pending_action_valid) {
            state->pending_action_valid = 0;
            if (event->ok) {
                UiCommand *c = emit(out, max, &count, UI_CMD_RUN_ACTION);
                if (c != NULL) {
                    c->action = state->pending_action;
                    c->pane = state->pending_pane;
                    c->source_type = state->pending_source_type;
                    fmt_copy(c->path, sizeof(c->path),
                             state->pending_path.text);
                    fmt_copy(c->path2, sizeof(c->path2),
                             state->pending_dest.text);
                    fmt_copy(c->text, sizeof(c->text), event->text);
                }
                state->screen = UI_SCREEN_WORKSPACE;
                set_focus(state, UI_FOCUS_LIST);
            }
            state->text.open = 0;
            break;
        }
        if (!state->text.open)
            break;
        state->text.open = 0;
        if (event->ok) {
            UiEditor *editor = &state->editor;
            const char *value = event->text;
            switch (state->text.field) {
            case UI_FIELD_NAME:
                fmt_copy(editor->name, sizeof(editor->name), value);
                break;
            case UI_FIELD_HOST:
                fmt_copy(editor->host, sizeof(editor->host), value);
                break;
            case UI_FIELD_PORT: {
                int port = 0;
                if (sscanf(value, "%d", &port) == 1)
                    editor->port = port;
                break;
            }
            case UI_FIELD_USERNAME:
                fmt_copy(editor->username, sizeof(editor->username), value);
                break;
            case UI_FIELD_PASSWORD:
                fmt_copy(editor->password, sizeof(editor->password), value);
                editor->password_set = value[0] != '\0';
                break;
            default:
                break;
            }
            editor->dirty = 1;
            editor->field = state->text.field;
        }
        break;
    case UI_EVT_PANE_INFO:
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            UiPaneState *pane = &state->panes[event->pane];
            pane->free_known = event->total_known;
            fmt_size(pane->free_text, sizeof(pane->free_text), event->bytes,
                     event->total_known);
        }
        break;
    case UI_EVT_EDIT_SITE:
        snprintf(state->editor.name, sizeof(state->editor.name), "%s",
                 event->edit_site.name);
        snprintf(state->editor.host, sizeof(state->editor.host), "%s",
                 event->edit_site.host);
        state->editor.port = event->edit_site.port;
        state->editor.anonymous = event->edit_site.anonymous;
        snprintf(state->editor.username, sizeof(state->editor.username), "%s",
                 event->edit_site.username);
        snprintf(state->editor.password, sizeof(state->editor.password), "%s",
                 event->edit_site.password);
        state->editor.password_set = event->edit_site.password[0] != '\0';
        state->editor.new_site = 0;
        state->editor.dirty = 0;
        state->editor.error[0] = '\0';
        state->editor.field = UI_FIELD_NAME;
        break;
    case UI_EVT_IME_UNAVAILABLE:
        state->text.open = 0;
        state->text.unavailable = 1;
        state->pending_action_valid = 0;
        break;
    case UI_EVT_SITE_SAVED:
        if (event->ok) {
            state->editor.dirty = 0;
            state->editor.error[0] = '\0';
        } else {
            fmt_copy(state->editor.error, sizeof(state->editor.error), event->text);
        }
        break;
    case UI_EVT_SITES:
        state->sites.save_unavailable = event->save_unavailable;
        if (event->ok) {
            state->sites.login_failed = 0;
        } else {
            state->sites.connecting = 0;
            state->sites.login_failed = 1;
            fmt_copy(state->sites.reply, sizeof(state->sites.reply), event->text);
        }
        if (event->site_rows != NULL) {
            int count = event->site_count;
            if (count < 0)
                count = 0;
            if (count > UI_SITES_MAX)
                count = UI_SITES_MAX;
            state->sites.count = count;
            for (int i = 0; i < count; ++i)
                state->sites.rows[i] = event->site_rows[i];
        } else if (event->value > 0) {
            state->sites.count = event->value;
        }
        if (state->sites.selected >= state->sites.count)
            state->sites.selected = state->sites.count > 0
                                        ? state->sites.count - 1
                                        : 0;
        if (state->sites.first > state->sites.selected)
            state->sites.first = state->sites.selected;
        if (state->sites.count > 6 && state->sites.selected >=
                                          state->sites.first + 6)
            state->sites.first = state->sites.selected - 5;
        break;
    default:
        break;
    }

    return count;
}
