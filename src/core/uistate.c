/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/uistate.h"
#include <stdio.h>
#include <string.h>

#include "core/fmt.h"
#include "core/path.h"

static void open_confirm(UiState *state, UiConfirmKind kind);
static int is_server_direction(int direction);

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

/* True when `child` is inside `parent` (case-insensitive, separator-aware). */
static int path_within(const char *parent, const char *child)
{
    size_t n = strlen(parent);
    while (n > 0 && parent[n - 1] == '/')
        n--;
    if (n == 0 || parent == NULL || child == NULL)
        return 0;
    for (size_t i = 0; i < n; ++i) {
        if (child[i] == '\0' || lower_ascii(child[i]) != lower_ascii(parent[i]))
            return 0;
    }
    if (child[n] == '\0')
        return 0;
    return parent[n - 1] == ':' || child[n] == '/';
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

UiPane ui_pane_right_view(const UiState *state)
{
    return state != NULL ? state->right_view : UI_PANE_REMOTE;
}

int ui_pane_is_local(UiPane pane)
{
    return pane == UI_PANE_LOCAL || pane == UI_PANE_LOCAL_RIGHT;
}

UiPane ui_pane_remote_view(void)
{
    return UI_PANE_REMOTE;
}

/* Every command in one reducer emission carries the same batch id, so the
 * worker can report one terminal state for the whole set. */
static void stamp_batch(UiState *state, UiCommand *command, int index,
                        int total)
{
    if (state == NULL || command == NULL)
        return;
    command->batch_id = state->batch_seq;
    command->batch_index = index;
    command->batch_total = total;
    command->batch_last = index + 1 >= total;
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

/* A refused action keeps its rows, so the failure belongs in the pane's inline
 * error line, not only in the small dock status. */
static void set_action_error(UiState *state, UiPaneState *pane,
                             const char *text)
{
    if (text == NULL)
        return;
    set_status(state, text);
    if (pane != NULL)
        fmt_copy(pane->error, sizeof(pane->error), text);
}

/* Clear an action failure on the next key. A failed listing (no rows) and a
 * lost session keep their detail because their centered state reads it. */
static void open_dock_cancel(UiState *state)
{
    int network = state->dock.active;
    int local = state->local_dock.active || state->local_dock.waiting;
    int server = is_server_direction((int)state->dock.direction);

    if (network && local) {
        open_confirm(state, UI_CONFIRM_CANCEL_LANES);
        if (server) {
            /* A server transfer ends through the server-stop confirm. */
            state->confirm.choices[1] = UI_CHOICE_CANCEL_LOCAL;
            state->confirm.choice_count = 2;
        }
        return;
    }
    if (local) {
        open_confirm(state, UI_CONFIRM_CANCEL_XFER);
        return;
    }
    if (network) {
        if (server) {
            set_status(state, "STOP THE SERVER TO CANCEL");
            return;
        }
        open_confirm(state, UI_CONFIRM_CANCEL_XFER);
    }
}

static void clear_action_error(UiPaneState *pane)
{
    if (pane != NULL && pane->row_count > 0 && !pane->session_lost)
        pane->error[0] = '\0';
}

static void begin_remote_connect(UiState *state, int from_sites);

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
    if (ui_pane_is_local(pane)) {
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

    if (ui_pane_is_local(pane)) {
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

static void move_selection(UiPaneState *pane, int delta, int wrap)
{
    if (pane->row_count <= 0)
        return;

    int next;
    if (wrap) {
        next = ((pane->selected + delta) % pane->row_count +
                pane->row_count) % pane->row_count;
    } else {
        next = pane->selected + delta;
        if (next < 0)
            next = 0;
        if (next >= pane->row_count)
            next = pane->row_count - 1;
    }
    pane->selected = next;

    if (pane->selected < pane->scroll)
        pane->scroll = pane->selected;
    if (pane->selected >= pane->scroll + UI_ROWS_VISIBLE)
        pane->scroll = pane->selected - UI_ROWS_VISIBLE + 1;
    clamp_pane(pane);
}

/* The dock entry is available while either lane can still be cancelled: an
 * item running now or one queued behind it (R24, KTD5). */
static int dock_focusable(const UiState *state)
{
    return state->dock.active || state->dock.queued > 0 ||
           state->local_dock.active || state->local_dock.queued > 0;
}

static const UiRow *selected_row(const UiState *state, UiPane pane)
{
    const UiPaneState *p = &state->panes[pane];
    if (p->rows == NULL || p->selected < 0 || p->selected >= p->row_count)
        return NULL;
    return &p->rows[p->selected];
}

static int is_local_direction(int direction)
{
    return direction >= XFER_DIR_LOCAL_COPY &&
           direction <= XFER_DIR_LOCAL_MKDIR;
}

static int is_server_direction(int direction)
{
    return direction == XFER_DIR_SERVER_DOWNLOAD ||
           direction == XFER_DIR_SERVER_UPLOAD;
}

static UiDock *lane_for(UiState *state, const UiStateEvent *event)
{
    if (event->direction_known && is_local_direction(event->direction)) {
        state->local_shown = 1;
        return &state->local_dock;
    }
    return &state->dock;
}

static void lane_started(UiState *state, UiDock *lane)
{
    if (lane == &state->local_dock) {
        state->local_shown = 1;
        lane->waiting = 0;
        if (!state->dock.active)
            state->dock_primary = UI_DOCK_LANE_LOCAL;
    } else {
        lane->waiting = 0;
        state->dock_primary = UI_DOCK_LANE_NETWORK;
    }
}

static void lane_terminal(UiState *state, UiDock *lane)
{
    if (lane == &state->local_dock) {
        state->local_shown = 1;
        lane->waiting = 0;
        if (!state->dock.active)
            state->dock_primary = UI_DOCK_LANE_LOCAL;
    } else {
        lane->waiting = 0;
        state->dock_primary = (state->local_shown && state->local_dock.active)
                                  ? UI_DOCK_LANE_LOCAL
                                  : UI_DOCK_LANE_NETWORK;
    }
}

static void set_transfer_dock(UiDock *dock, const UiStateEvent *event,
                              UiDockState dock_state)
{
    dock->state = dock_state;
    if (event->text[0] != '\0')
        fmt_copy(dock->message, sizeof(dock->message), event->text);
    dock->done = event->bytes;
    dock->total = event->total;
    dock->total_known = event->total_known;
}

/* The worker reports the batch position (item_index of item_total). The items
 * after the active one are the queue the dock can still cancel, so the dock
 * reads the depth from the event instead of keeping a second counter (KTD5). */
static void set_lane_queue(UiDock *lane, const UiStateEvent *event)
{
    int queued = event->item_total - event->item_index;
    lane->queued = queued > 0 ? queued : 0;
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
    case UI_CONFIRM_PATH_BUSY:
        c->choices[0] = UI_CHOICE_WAIT;
        c->choices[1] = UI_CHOICE_SKIP;
        c->choices[2] = UI_CHOICE_CANCEL;
        c->choice_count = 3;
        snprintf(c->title, sizeof(c->title), "PATH IN USE");
        snprintf(c->warning, sizeof(c->warning),
                 "WAIT RESUMES WHEN THE OTHER OPERATION RELEASES IT.");
        break;
    case UI_CONFIRM_CANCEL_LANES:
        c->choices[0] = UI_CHOICE_KEEP;
        c->choices[1] = UI_CHOICE_CANCEL_NETWORK;
        c->choices[2] = UI_CHOICE_CANCEL_LOCAL;
        c->choice_count = 3;
        snprintf(c->title, sizeof(c->title), "CANCEL WHICH TRANSFER");
        snprintf(c->warning, sizeof(c->warning),
                 "CANCELLED TRANSFERS REMOVE THEIR FILES.");
        break;
    case UI_CONFIRM_CANCEL_XFER:
        c->choices[0] = UI_CHOICE_KEEP;
        c->choices[1] = UI_CHOICE_CANCEL;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "CANCEL THE TRANSFER");
        /* An explicit cancel discards even a download whose bytes all
         * arrived (D89); "transferred" is true for both directions. */
        snprintf(c->warning, sizeof(c->warning),
                 "THE TRANSFERRED FILE IS REMOVED.");
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
    case UI_CONFIRM_DELETE_SITE:
        c->choices[0] = UI_CHOICE_CANCEL;
        c->choices[1] = UI_CHOICE_DELETE;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "DELETE SITE");
        snprintf(c->warning, sizeof(c->warning), "THIS CANNOT BE UNDONE.");
        break;
    case UI_CONFIRM_DUPLICATE:
        /* CANCEL is focused first, so a same-folder paste cannot duplicate by
         * accident; SAVE DUPLICATES is the yellow commit (R16). */
        c->choices[0] = UI_CHOICE_CANCEL;
        c->choices[1] = UI_CHOICE_SAVE_DUPLICATE;
        c->choice_count = 2;
        snprintf(c->title, sizeof(c->title), "ITEMS ALREADY HERE");
        snprintf(c->warning, sizeof(c->warning),
                 "SAVE DUPLICATES KEEPS THE ORIGINAL.");
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
    /* The switch belongs to the right slot only: the left pane never shows
     * one, the remote view offers SWITCH TO LOCAL, the local right view offers
     * SWITCH TO REMOTE (KTD3). */
    if (action == UI_ACTION_SWITCH_LOCAL)
        return pane == UI_PANE_REMOTE;
    if (action == UI_ACTION_SWITCH_REMOTE)
        return pane == UI_PANE_LOCAL_RIGHT;

    if (ui_pane_is_local(pane)) {
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
    int local_pane = ui_pane_is_local(pane_index);
    UiPane remote_view = ui_pane_remote_view();
    UiPaneState *pane = &state->panes[pane_index];
    /* Never read the row arena while a listing is being written. */
    const UiRow *row = pane->loading ? NULL : selected_row(state, pane_index);

    for (int i = 0; i < UI_ACTION_COUNT; ++i) {
        actions->enabled[i] = 1;
        actions->reason[i][0] = '\0';
    }
    actions->protected_root = 0;
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
    /* A local upload targets the remote view only while it is shown; a
     * download always lands in the left local view (KTD1). */
    if (local_pane) {
        if (state->right_view == remote_view)
            fmt_copy(actions->other, sizeof(actions->other),
                     state->panes[remote_view].path);
        else
            actions->other[0] = '\0';
    } else {
        fmt_copy(actions->other, sizeof(actions->other),
                 state->panes[UI_PANE_LOCAL].path);
    }

    /*
     * The lock belongs to the selected item, not to the folder the cursor is
     * in. A file inside ux0: is not the volume root; treating it as one made
     * every action unavailable while browsing a volume root.
     */
    int virtual_root = local_pane &&
                       strcmp(pane->path, "*") == 0;
    int source_is_root =
        has_selection &&
        (local_pane
             ? (strcmp(source, "*") == 0 || path_is_volume_root(source))
             : strcmp(source, "/") == 0);
    int protected_source = 0;
    int read_only_source = 0;
    PathRisk risk = PATH_RISK_NORMAL;
    if (has_selection && local_pane && source[0] != '\0') {
        PathResult check = path_check_delete(source, 0, &risk);
        if (check == PATH_PROTECTED || check == PATH_DENIED)
            protected_source = 1;
        if (check == PATH_READ_ONLY)
            read_only_source = 1;
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

    if (local_pane) {
        if (!has_selection) {
            disable(actions, UI_ACTION_COPY, "NO SELECTION");
            disable(actions, UI_ACTION_MOVE, "NO SELECTION");
            disable(actions, UI_ACTION_UPLOAD, "NO SELECTION");
            disable(actions, UI_ACTION_RENAME, "NO SELECTION");
            disable(actions, UI_ACTION_DELETE, "NO SELECTION");
        } else if (source_is_root || protected_source) {
            actions->protected_root = 1;
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
        if (has_selection && read_only_source) {
            disable(actions, UI_ACTION_MOVE, "READ-ONLY MOUNT");
            disable(actions, UI_ACTION_RENAME, "READ-ONLY MOUNT");
            disable(actions, UI_ACTION_DELETE, "READ-ONLY MOUNT");
        }
        Volume dest_volume;
        int dest_read_only =
            path_volume(pane->path, &dest_volume) &&
            path_volume_is_read_only(dest_volume);
        if (dest_read_only) {
            disable(actions, UI_ACTION_PASTE, "READ-ONLY MOUNT");
            disable(actions, UI_ACTION_NEW_FOLDER, "READ-ONLY MOUNT");
        }
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
        Volume download_volume;
        if (path_volume(actions->other, &download_volume) &&
            path_volume_is_read_only(download_volume))
            disable(actions, UI_ACTION_DOWNLOAD, "READ-ONLY MOUNT");
        if (state->dock.active &&
            (state->dock.direction == XFER_DIR_DOWNLOAD ||
             state->dock.direction == XFER_DIR_UPLOAD))
            disable(actions, UI_ACTION_DISCONNECT, "TRANSFER IN PROGRESS");
    }

    if (local_pane) {
        /* UPLOAD exists as a row in both-local mode but has no target, so it
         * is dim even with no selection (AE8). The reason is applied last so
         * NO SELECTION cannot mask it. */
        if (state->right_view != remote_view)
            disable(actions, UI_ACTION_UPLOAD, "NO REMOTE VIEW");
        else if (has_selection && !state->panes[remote_view].connected)
            disable(actions, UI_ACTION_UPLOAD, "NOT CONNECTED");
        else if (has_selection && state->panes[remote_view].loading)
            disable(actions, UI_ACTION_UPLOAD, "REMOTE READING");
    }
}

static void handle_workspace_key(UiState *state, const UiStateEvent *event,
                                 UiCommand *out, int max, int *count)
{
    UiPaneState *pane = &state->panes[state->active_pane];
    int local_pane = ui_pane_is_local(state->active_pane);
    UiPane remote_view = ui_pane_remote_view();
    UiKey key = event->key;

    if (key == UI_KEY_L1 || key == UI_KEY_R1) {
        /* L/R moves between the left slot and whichever view the right slot
         * shows; it is a slot move, never a session action (KTD1). */
        state->active_pane = state->active_pane == UI_PANE_LOCAL
                                 ? state->right_view
                                 : UI_PANE_LOCAL;
        set_focus(state, UI_FOCUS_LIST);
        return;
    }

    /* The screen keys stay live on a lost view: ACTIONS (and its switch row)
     * is the way to the local pane, and SERVER and SITES remain reachable
     * (KTD4, R7). Intercepting the lost state first swallowed all three. */
    switch (key) {
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
        break;
    }

    if (pane->session_lost) {
        if (key == UI_KEY_CROSS) {
            begin_remote_connect(state, 0);
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
            open_dock_cancel(state);
        }
        return;
    }

    clear_action_error(pane);

    if (pane->loading &&
        (key == UI_KEY_CROSS || key == UI_KEY_SQUARE)) {
        if (!local_pane)
            set_status(state, "READING");
        return;
    }

    switch (key) {
    case UI_KEY_UP:
        move_selection(pane, -1, 1);
        return;
    case UI_KEY_DOWN:
        if (pane->row_count == 0) {
            if (dock_focusable(state))
                set_focus(state, UI_FOCUS_DOCK);
            return;
        }
        if (pane->selected >= pane->row_count - 1 && dock_focusable(state)) {
            set_focus(state, UI_FOCUS_DOCK);
            return;
        }
        move_selection(pane, 1, 1);
        return;
    case UI_KEY_CROSS: {
        const UiRow *row = selected_row(state, state->active_pane);
        if (row == NULL)
            return;
        int is_dir = row->type == UI_ROW_DIR || row->type == UI_ROW_VOLUME;
        /* The unsafe-name rule protects the local path a transfer builds. It
         * must not block navigation: the Vita server's own volume roots are
         * named `UX0:`, `UR0:`, and `UMA0:` (D65). */
        if (row->unsafe && !is_dir) {
            set_action_error(state, pane,
                             "UNSAFE NAME - CANNOT MAKE A LOCAL FILE");
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

        if (is_dir) {
            if (!local_pane && !pane->connected) {
                set_status(state, "NOT CONNECTED");
                return;
            }
            if (!local_pane && pane->loading) {
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

        if (local_pane) {
            /* A local transfer is an upload. Without the remote view on
             * screen there is no target to name (AE10). */
            if (state->right_view != remote_view) {
                set_status(state, "NO REMOTE VIEW");
                return;
            }
            if (!state->panes[remote_view].connected) {
                set_status(state, "NOT CONNECTED");
                return;
            }
        } else if (!pane->connected) {
            set_status(state, "NOT CONNECTED");
            return;
        }

        UiPane target_view = local_pane ? remote_view : UI_PANE_LOCAL;

        if (pane->mark_count > 0) {
            for (int i = 0; i < pane->mark_count; ++i) {
                if (pane->marks[i].type == UI_ROW_DIR) {
                    set_action_error(state, pane,
                                     "FOLDER TRANSFER NOT SUPPORTED");
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
                fmt_copy(c->path2, sizeof(c->path2),
                         state->panes[target_view].path);
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
            fmt_copy(c->path2, sizeof(c->path2),
                     state->panes[target_view].path);
            c->value = 0;
        }
        return;
    }
    case UI_KEY_CIRCLE:
        if (local_pane && strcmp(pane->path, "*") == 0) {
            set_status(state, "AT ROOT");
            return;
        }
        if (local_pane && path_is_volume_root(pane->path)) {
            UiCommand *c = emit(out, max, count, UI_CMD_GO_PARENT);
            if (c != NULL)
                c->pane = state->active_pane;
            return;
        }
        if (!local_pane && !pane->connected) {
            set_status(state, "NOT CONNECTED");
            return;
        }
        if (!local_pane && pane->loading) {
            set_status(state, "READING");
            return;
        }
        if (!local_pane && strcmp(pane->path, "/") == 0) {
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
    default:
        return;
    }
}

static void clear_remote_session(UiState *state)
{
    UiPaneState *remote = &state->panes[ui_pane_remote_view()];

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
    state->sites.connect_from_sites = 0;
    state->sites.login_failed = 0;
    state->sites.reply[0] = '\0';
}

static void begin_remote_connect(UiState *state, int from_sites)
{
    /* A connect attempt replaces the current session before it dials: the
     * pane drops its rows and identity now, so a failure cannot leave the old
     * session looking live (KTD4, R13). */
    clear_remote_session(state);
    state->panes[ui_pane_remote_view()].loading = 1;
    state->sites.connecting = 1;
    state->sites.connect_from_sites = from_sites;
}

/* One connect failure, recorded where the site card reads it. The card keeps
 * the reason whether or not SITES is still in front (R22). */
static void set_site_failure(UiState *state, const char *text)
{
    state->sites.connecting = 0;
    state->sites.connect_from_sites = 0;
    state->sites.login_failed = 1;
    fmt_copy(state->sites.reply, sizeof(state->sites.reply),
             text != NULL && text[0] != '\0' ? text : "NO REPLY");
}

/* The site book is a wrapping list too. A wrap starts a fresh window instead
 * of leaving the old offset behind: down shows the top, up shows the last
 * window (R24, AE11). */
static void move_site_selection(UiState *state, int delta)
{
    UiSiteList *sites = &state->sites;
    if (sites->count <= 0)
        return;

    int raw = sites->selected + delta;
    int next = ((raw % sites->count) + sites->count) % sites->count;
    int wrapped = raw < 0 || raw >= sites->count;
    sites->selected = next;

    if (wrapped) {
        sites->first = delta > 0 ? 0 : sites->count - UI_SITES_VISIBLE;
        if (sites->first < 0)
            sites->first = 0;
    }
    if (sites->selected < sites->first)
        sites->first = sites->selected;
    if (sites->selected >= sites->first + UI_SITES_VISIBLE)
        sites->first = sites->selected - UI_SITES_VISIBLE + 1;
}

static void handle_sites_key(UiState *state, const UiStateEvent *event,
                             UiCommand *out, int max, int *count)
{
    switch (event->key) {
    case UI_KEY_UP:
        move_site_selection(state, -1);
        return;
    case UI_KEY_DOWN:
        move_site_selection(state, 1);
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
        begin_remote_connect(state, 1);
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
            /* The port is text, so Cross opens the IME seeded with the live
             * value and the answer comes back as an edit command. Emitting
             * EDIT_PORT with the current value here made the field a no-op. */
            char value[8];
            snprintf(value, sizeof(value), "%u", (unsigned)state->server.port);
            UiCommand *c = emit(out, max, count, UI_CMD_OPEN_IME);
            if (c != NULL) {
                c->field = UI_FIELD_PORT;
                c->index = 0;
                c->value = 5;
                fmt_copy(c->title, sizeof(c->title), "SERVER PORT");
                fmt_copy(c->text, sizeof(c->text), value);
            }
            state->text.open = 1;
            state->text.field = UI_FIELD_PORT;
            state->text.masked = 0;
            state->text.max = 5;
            fmt_copy(state->text.title, sizeof(state->text.title),
                     "SERVER PORT");
            fmt_copy(state->text.value, sizeof(state->text.value), value);
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
    if (result == PATH_READ_ONLY)
        return "READ-ONLY MOUNT";
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

        /* The right-pane switch is reducer-only: no worker command exists for
         * it. Flipping the view preserves the session and both views' state
         * (R8, KTD3). It stays reachable when the remote view is lost or
         * disconnected because nothing here reads the session. */
        if (action == UI_ACTION_SWITCH_LOCAL ||
            action == UI_ACTION_SWITCH_REMOTE) {
            UiPane view = action == UI_ACTION_SWITCH_LOCAL
                              ? UI_PANE_LOCAL_RIGHT
                              : ui_pane_remote_view();
            state->right_view = view;
            state->active_pane = view;
            leave_screen(state);
            set_status(state, action == UI_ACTION_SWITCH_LOCAL
                                  ? "SWITCHED TO LOCAL"
                                  : "SWITCHED TO REMOTE");
            return;
        }

        /* COPY and MOVE arm the clipboard. Nothing is touched until PASTE,
         * after the user has navigated to the destination folder. The marked
         * set wins; with no marks the row under the cursor is the source. */
        if (action == UI_ACTION_COPY || action == UI_ACTION_MOVE) {
            int count = pane->mark_count;
            if (count <= 0) {
                if (!state->actions.has_selection ||
                    state->actions.path[0] == '\0') {
                    set_status(state, "NO SELECTION");
                    return;
                }
                state->clip_mode =
                    action == UI_ACTION_COPY ? UI_CLIP_COPY : UI_CLIP_MOVE;
                state->clip_count = 1;
                fmt_copy(state->clip_paths[0].text,
                         sizeof(state->clip_paths[0].text),
                         state->actions.path);
                leave_screen(state);
                return;
            }
            if (count > UI_CLIP_MAX) {
                set_status(state, "TOO MANY ITEMS");
                return;
            }
            state->clip_mode =
                action == UI_ACTION_COPY ? UI_CLIP_COPY : UI_CLIP_MOVE;
            state->clip_count = count;
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

            /* Two sources with one basename resolve to one destination: refuse
             * before any command. A move source inside another is already
             * carried by the outer one, so it is skipped (D73). */
            int keep[UI_CLIP_MAX];
            int kept = 0;
            for (int i = 0; i < state->clip_count; ++i)
                keep[i] = 1;
            for (int i = 0; i < state->clip_count; ++i) {
                for (int j = i + 1; j < state->clip_count; ++j) {
                    if (ci_equal(last_segment(state->clip_paths[i].text),
                                 last_segment(state->clip_paths[j].text))) {
                        set_status(state, "SAME DESTINATION");
                        return;
                    }
                }
            }
            if (state->clip_mode == UI_CLIP_MOVE) {
                for (int i = 0; i < state->clip_count; ++i) {
                    for (int j = 0; j < state->clip_count; ++j) {
                        if (i != j &&
                            path_within(state->clip_paths[j].text,
                                        state->clip_paths[i].text)) {
                            keep[i] = 0;
                            break;
                        }
                    }
                }
            }
            for (int i = 0; i < state->clip_count; ++i)
                kept += keep[i];
            if (kept == 0) {
                set_status(state, "NOTHING TO PASTE");
                return;
            }

            int first = -1;
            for (int i = 0; i < state->clip_count; ++i) {
                if (!keep[i])
                    continue;
                Path target;
                if (path_join(destination,
                              last_segment(state->clip_paths[i].text),
                              &target) != PATH_OK) {
                    set_status(state, "PATH TOO LONG");
                    return;
                }
                if (first < 0)
                    first = i;
                PathResult result = path_check_write(target.text, 0, NULL);
                if (result != PATH_OK) {
                    set_status(state, protection_reason(result, target.text));
                    return;
                }
            }

            state->pending_action = action;
            state->pending_action_valid = 0;
            state->pending_pane = state->active_pane;
            fmt_copy(state->pending_path.text, sizeof(state->pending_path.text),
                     state->clip_paths[first].text);
            fmt_copy(state->pending_dest.text, sizeof(state->pending_dest.text),
                     destination);
            state->confirm_from_worker = 0;

            state->batch_seq++;
            int index = 0;
            for (int i = 0; i < state->clip_count; ++i) {
                if (!keep[i])
                    continue;
                UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
                if (c == NULL) {
                    set_status(state, "COMMAND LIMIT");
                    break;
                }
                stamp_batch(state, c, index, kept);
                index++;
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
                c->pane = ui_pane_remote_view();
            }
            leave_screen(state);
            set_status(state, "DISCONNECTED");
            return;
        }

        if (action == UI_ACTION_REFRESH) {
            UiCommand *c = emit(out, max, count, UI_CMD_REFRESH);
            if (c != NULL)
                c->pane = ui_pane_remote_view();
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

        /* A remote path is not a Vita path: the local protection table would
         * reject every remote delete as INVALID PATH. Remote deletes are
         * confirmed here and executed by the FTP client. */
        if (action == UI_ACTION_DELETE &&
            ui_pane_is_local(state->active_pane)) {
            for (int i = 0; i < source_count; ++i) {
                PathResult result = path_check_delete(sources[i], 0, NULL);
                if (result != PATH_OK) {
                    set_status(state, protection_reason(result, sources[i]));
                    return;
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
                PathResult result = path_check_write(target.text, 0, NULL);
                if (result != PATH_OK) {
                    set_status(state, protection_reason(result, target.text));
                    return;
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

        if (action == UI_ACTION_UPLOAD || action == UI_ACTION_DOWNLOAD) {
            state->batch_seq++;
            for (int i = 0; i < source_count; ++i) {
                UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
                if (c == NULL) {
                    set_status(state, "COMMAND LIMIT");
                    break;
                }
                stamp_batch(state, c, i, source_count);
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

        state->batch_seq++;
        UiCommand *c = emit(out, max, count, UI_CMD_RUN_ACTION);
        if (c != NULL) {
            stamp_batch(state, c, 0, 1);
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

/* The Cross and Circle answers share one resolver. It returns 0 when the
 * answer only closes the dialog (the two-lane KEEP). */
static int resolve_confirm_answer(UiState *state, const UiConfirm *c,
                                  UiChoice choice, UiCommandType *type)
{
    *type = UI_CMD_CONFIRM;
    if (c->kind == UI_CONFIRM_CANCEL_XFER && choice == UI_CHOICE_CANCEL) {
        *type = UI_CMD_CANCEL_TRANSFER;
    } else if (c->kind == UI_CONFIRM_CANCEL_LANES) {
        if (choice == UI_CHOICE_KEEP)
            return 0;
        *type = choice == UI_CHOICE_CANCEL_NETWORK ? UI_CMD_CANCEL_NETWORK
                                                   : UI_CMD_CANCEL_LOCAL;
    }
    if (c->kind == UI_CONFIRM_PATH_BUSY && choice == UI_CHOICE_WAIT) {
        if (c->pane == (int)ui_pane_remote_view()) {
            state->dock.waiting = 1;
            state->dock_primary = UI_DOCK_LANE_NETWORK;
        } else {
            state->local_shown = 1;
            state->local_dock.waiting = 1;
            if (!state->dock.active)
                state->dock_primary = UI_DOCK_LANE_LOCAL;
        }
    }
    return 1;
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
        if (!from_worker && c->kind == UI_CONFIRM_DELETE) {
            int approved = choice == UI_CHOICE_DELETE;
            int approved_value = 0;
            if (approved) {
                UiPaneState *pending =
                    &state->panes[state->pending_pane];
                if (state->pending_action == UI_ACTION_PASTE) {
                    state->batch_seq++;
                    for (int i = 0; i < state->clip_count; ++i) {
                        UiCommand *run = emit(out, max, count,
                                              UI_CMD_RUN_ACTION);
                        if (run == NULL)
                            break;
                        stamp_batch(state, run, i, state->clip_count);
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
                    state->batch_seq++;
                    for (int i = 0; i < pending->mark_count; ++i) {
                        UiCommand *run = emit(out, max, count,
                                              UI_CMD_RUN_ACTION);
                        if (run == NULL)
                            break;
                        stamp_batch(state, run, i, pending->mark_count);
                        run->action = state->pending_action;
                        run->pane = state->pending_pane;
                        run->source_type = pending->marks[i].type;
                        run->value = approved_value;
                        fmt_copy(run->path, sizeof(run->path), pending->marks[i].path.text);
                        fmt_copy(run->path2, sizeof(run->path2), state->pending_dest.text);
                    }
                } else {
                    state->batch_seq++;
                    UiCommand *run = emit(out, max, count,
                                          UI_CMD_RUN_ACTION);
                    if (run != NULL) {
                        stamp_batch(state, run, 0, 1);
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
        UiCommandType type;
        if (!resolve_confirm_answer(state, c, choice, &type)) {
            close_confirm(state, 0);
            return;
        }
        UiCommand *command = emit(out, max, count, type);
        if (command != NULL && type == UI_CMD_CONFIRM) {
            command->confirm = c->kind;
            command->choice = choice;
            command->prompt_id = c->prompt_id;
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
        if (c->kind == UI_CONFIRM_DELETE && !state->confirm_from_worker) {
            close_confirm(state, 0);
            return;
        }
        UiChoice choice = UI_CHOICE_CANCEL;
        if (c->kind == UI_CONFIRM_DISCARD ||
            c->kind == UI_CONFIRM_CANCEL_XFER ||
            c->kind == UI_CONFIRM_CANCEL_LANES ||
            c->kind == UI_CONFIRM_STOP_SERVER)
            choice = UI_CHOICE_KEEP;
        UiCommandType type;
        if (!resolve_confirm_answer(state, c, choice, &type)) {
            close_confirm(state, 0);
            return;
        }
        UiCommand *command = emit(out, max, count, type);
        if (command != NULL && type == UI_CMD_CONFIRM) {
            command->confirm = c->kind;
            command->choice = choice;
            command->prompt_id = c->prompt_id;
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

void ui_state_init(UiState *state, UiMark *marks[UI_PANE_COUNT],
                   const int caps[UI_PANE_COUNT])
{
    if (state == NULL)
        return;
    memset(state, 0, sizeof(*state));

    state->screen = UI_SCREEN_WORKSPACE;
    state->return_screen = UI_SCREEN_WORKSPACE;
    state->confirm_return = UI_SCREEN_WORKSPACE;
    state->editor_return = UI_SCREEN_WORKSPACE;
    state->active_pane = UI_PANE_LOCAL;
    state->right_view = ui_pane_remote_view();
    state->focus = UI_FOCUS_LIST;

    for (int pane = 0; pane < UI_PANE_COUNT; ++pane) {
        UiPaneState *view = &state->panes[pane];
        if (ui_pane_is_local((UiPane)pane)) {
            /* Both local views open at the virtual ALL DRIVES root. */
            snprintf(view->path, sizeof(view->path), "*");
            view->connected = 1;
        } else {
            snprintf(view->path, sizeof(view->path), "/");
            view->connected = 0;
        }
        view->marks = marks != NULL ? marks[pane] : NULL;
        view->mark_cap = caps != NULL ? caps[pane] : 0;
    }

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
    int restored = -1;
    if (p->cursor_valid) {
        /* Put the cursor back on the name it held before the reload; a name
         * that is gone keeps the old index and lets clamp_pane settle it. */
        int local = ui_pane_is_local(pane);
        for (int r = 0; r < count; ++r) {
            int same = local ? ci_equal(rows[r].name, p->cursor_name)
                             : strcmp(rows[r].name, p->cursor_name) == 0;
            if (same) {
                restored = r;
                break;
            }
        }
        if (restored >= 0)
            p->selected = restored;
        p->cursor_valid = 0;
    }
    p->rows = rows;
    p->row_count = count;
    p->truncated = truncated;
    p->loading = 0;
    p->empty = count == 0;
    prune_marks(p, !ui_pane_is_local(pane));
    clamp_pane(p);
    if (restored >= 0) {
        /* Keep the restored row visible without moving it further. */
        if (p->selected < p->scroll)
            p->scroll = p->selected;
        else if (p->selected >= p->scroll + UI_ROWS_VISIBLE)
            p->scroll = p->selected - UI_ROWS_VISIBLE + 1;
        clamp_pane(p);
    }
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
        if (event->stick_y == 0)
            break;
        if (state->screen == UI_SCREEN_WORKSPACE &&
            state->focus == UI_FOCUS_LIST) {
            /* The stick moves the cursor like a list control: the step is the
             * deflection and the view follows, never a detached scroll. */
            move_selection(&state->panes[state->active_pane], event->stick_y,
                           1);
        } else if (state->screen == UI_SCREEN_SITES &&
                   state->focus == UI_FOCUS_SITES) {
            move_site_selection(state, event->stick_y);
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
        /* A reload lists the folder already open: remember the cursor's name
         * so the new rows can put the cursor back on it (R9, KTD7). */
        pane->cursor_valid = 0;
        if (!path_changed && !new_session && pane->rows != NULL &&
            pane->selected >= 0 && pane->selected < pane->row_count) {
            fmt_copy(pane->cursor_name, sizeof(pane->cursor_name),
                     pane->rows[pane->selected].name);
            pane->cursor_valid = pane->cursor_name[0] != '\0';
        }
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
                /* No rows arrived, so a reload has nothing to restore. */
                pane->cursor_valid = 0;
                fmt_copy(pane->error, sizeof(pane->error), event->text);
            } else {
                pane->error[0] = '\0';
            }
        }
        break;
    case UI_EVT_SESSION_LOST: {
        /* Only a connect that started on SITES blames the site card; a session
         * that drops while browsing reports on its pane and dock instead. */
        int from_sites = state->sites.connect_from_sites &&
                         event->pane == (int)ui_pane_remote_view();
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
        if (from_sites) {
            /* The reason stays on the card, and on the dock's message line for
             * a user who already left SITES (R22). */
            set_site_failure(state, event->text);
            set_status(state, event->text);
        } else {
            state->sites.connect_from_sites = 0;
        }
        /* The session's queued batch dies with it, so a dead queue must not
         * keep the dock entry alive. */
        state->dock.queued = 0;
        if (state->screen == UI_SCREEN_ACTIONS &&
            state->active_pane == ui_pane_remote_view()) {
            /* The ledger's enabled flags describe the dead session. Return to
             * the workspace and its SESSION LOST prompt. */
            state->screen = UI_SCREEN_WORKSPACE;
            set_focus(state, UI_FOCUS_LIST);
        }
        break;
    }
    case UI_EVT_SESSION_OK: {
        int from_sites = state->sites.connect_from_sites;
        state->sites.connecting = 0;
        state->sites.connect_from_sites = 0;
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
            state->sites.reply[0] = '\0';
            snprintf(state->panes[ui_pane_remote_view()].site,
                     sizeof(state->panes[ui_pane_remote_view()].site), "%s",
                     event->site);
        }
        /* The connection exists, so the remote view is the one to show
         * (R13). */
        state->right_view = ui_pane_remote_view();
        if (from_sites && state->screen == UI_SCREEN_SITES) {
            /* SITES was the connect surface: it closes and the workspace
             * returns with the remote view active and its listing requested
             * (R21). A user who left SITES keeps the screen and focus they
             * chose; only the right view switches. */
            leave_screen(state);
            state->active_pane = ui_pane_remote_view();
        }
        break;
    }
    case UI_EVT_ERROR: {
        int from_sites = 0;
        if (event->pane == (int)ui_pane_remote_view()) {
            /* A connect can fail before any session event exists: a bad host,
             * no network, or a refused attempt. Only the pending SITES
             * attempt owns the error; an unrelated remote error must not
             * consume its reason (R22). */
            from_sites = state->sites.connect_from_sites;
            state->sites.connecting = 0;
            /* A command can fail before any LISTING_END exists (busy client,
             * refused CWD, not connected). Clear the eager READING state here
             * or the pane stays on READING forever. */
            UiPaneState *remote = &state->panes[ui_pane_remote_view()];
            remote->loading = 0;
            remote->pending_path[0] = '\0';
        } else if (event->pane >= 0 && event->pane < UI_PANE_COUNT) {
            /* A local OPEN that never started a listing leaves the same
             * READING latch; only clear it when an eager open is pending, so
             * an unrelated action error cannot expose a half-written arena. */
            UiPaneState *local = &state->panes[event->pane];
            if (local->pending_path[0] != '\0') {
                local->loading = 0;
                local->pending_path[0] = '\0';
            }
        }
        set_status(state, event->text);
        if (event->pane >= 0 && event->pane < UI_PANE_COUNT)
            fmt_copy(state->panes[event->pane].error,
                     sizeof(state->panes[event->pane].error), event->text);
        if (from_sites)
            set_site_failure(state, event->text);
        break;
    }
    case UI_EVT_TRANSFER_STARTED: {
        UiDock *lane = lane_for(state, event);
        lane->active = 1;
        if (lane == &state->dock && lane->focused)
            set_focus(state, UI_FOCUS_LIST);
        if (event->direction_known)
            lane->direction = (XferDirection)event->direction;
        else
            lane->direction = ui_pane_is_local((UiPane)event->pane)
                                  ? XFER_DIR_UPLOAD
                                  : XFER_DIR_DOWNLOAD;
        fmt_copy(lane->name, sizeof(lane->name), event->text);
        lane->skipped = 0;
        lane->failed = 0;
        lane->cancelled = 0;
        lane->notice = 0;
        set_transfer_dock(lane, event, UI_DOCK_ACTIVE);
        lane->done = 0;
        lane->item_index = event->item_index;
        lane->item_total = event->item_total;
        set_lane_queue(lane, event);
        lane->elapsed_ms = 0;
        lane->elapsed_known = 0;
        lane->rate_known = 0;
        lane_started(state, lane);
        break;
    }
    case UI_EVT_TRANSFER_PROGRESS: {
        UiDock *lane = event->direction_known ? lane_for(state, event)
                                              : &state->dock;
        if (!lane->active &&
            (lane->state == UI_DOCK_DONE || lane->state == UI_DOCK_FAILED))
            break;
        lane->active = 1;
        set_transfer_dock(lane, event, UI_DOCK_ACTIVE);
        lane->rate = event->rate;
        lane->rate_known = event->rate_known;
        lane->elapsed_ms = event->elapsed_ms;
        lane->elapsed_known = event->elapsed_known;
        if (event->item_index > 0)
            lane->item_index = event->item_index;
        if (event->item_total > 0)
            lane->item_total = event->item_total;
        break;
    }
    case UI_EVT_TRANSFER_DONE: {
        UiDock *lane = lane_for(state, event);
        lane->active = 0;
        if (lane == &state->dock && lane->focused)
            set_focus(state, UI_FOCUS_LIST);
        set_transfer_dock(lane, event, UI_DOCK_DONE);
        lane->file_kept = 0;
        lane->notice = event->notice;
        lane->rate = event->rate;
        lane->rate_known = event->rate_known;
        lane->elapsed_ms = event->elapsed_ms;
        lane->elapsed_known = event->elapsed_known;
        set_lane_queue(lane, event);
        lane_terminal(state, lane);
        break;
    }
    case UI_EVT_TRANSFER_CANCELLED: {
        UiDock *lane = event->direction_known ? lane_for(state, event)
                                              : &state->dock;
        lane->active = 0;
        lane->waiting = 0;
        lane->state = UI_DOCK_IDLE;
        lane->message[0] = '\0';
        if (lane == &state->dock && lane->focused)
            set_focus(state, UI_FOCUS_LIST);
        set_status(state, lane == &state->dock ? "NETWORK CANCELLED"
                                               : "LOCAL CANCELLED");
        break;
    }
    case UI_EVT_TRANSFER_FAILED: {
        UiDock *lane = lane_for(state, event);
        lane->active = 0;
        if (lane == &state->dock && lane->focused)
            set_focus(state, UI_FOCUS_LIST);
        set_transfer_dock(lane, event, UI_DOCK_FAILED);
        lane->notice = 0;
        lane->file_kept = event->file_kept;
        if (event->item_index > 0)
            lane->item_index = event->item_index;
        if (event->item_total > 0)
            lane->item_total = event->item_total;
        set_lane_queue(lane, event);
        lane_terminal(state, lane);
        break;
    }
    case UI_EVT_BATCH_DONE: {
        UiDock *lane = &state->local_dock;
        lane->active = 0;
        lane->queued = 0;
        state->local_shown = 1;
        if (event->direction_known)
            lane->direction = (XferDirection)event->direction;
        lane->state = UI_DOCK_DONE;
        lane->done = event->bytes;
        lane->total = event->total;
        lane->total_known = event->total_known;
        lane->item_index = event->item_index;
        lane->item_total = event->item_total;
        lane->skipped = event->batch_skipped;
        lane->failed = event->batch_failed;
        lane->cancelled = event->batch_cancelled;
        /* The last item's done event owns the notice flag; a batch started
         * later clears it. */
        lane->rate = event->rate;
        lane->rate_known = event->rate_known;
        lane->elapsed_ms = event->elapsed_ms;
        lane->elapsed_known = event->elapsed_known;
        if (event->text[0] != '\0')
            fmt_copy(lane->message, sizeof(lane->message), event->text);
        lane_terminal(state, lane);
        /* D18: a completed batch does not clear marks. */
        break;
    }
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
        state->confirm.pane = event->pane;
        state->confirm.prompt_id = event->prompt_id;
        state->confirm.items = event->items;
        state->confirm.item_count = event->item_count;
        state->confirm.existing_count = event->existing_count;
        /* The request's own context line, e.g. the folder a duplicate paste
         * would fill (R16). A kind without one keeps the empty fallback. */
        if (event->text[0] != '\0')
            fmt_copy(state->confirm.context, sizeof(state->confirm.context),
                     event->text);
        break;
    case UI_EVT_IME_RESULT:
        if (state->pending_action_valid) {
            state->pending_action_valid = 0;
            if (event->ok) {
                state->batch_seq++;
                UiCommand *c = emit(out, max, &count, UI_CMD_RUN_ACTION);
                if (c != NULL) {
                    stamp_batch(state, c, 0, 1);
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
        if (event->ok && state->screen == UI_SCREEN_SERVER &&
            state->text.field == UI_FIELD_PORT) {
            int port = 0;
            if (sscanf(event->text, "%d", &port) == 1) {
                UiCommand *c = emit(out, max, &count, UI_CMD_EDIT_PORT);
                if (c != NULL)
                    c->value = port;
            }
            break;
        }
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
            state->sites.connect_from_sites = 0;
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
        if (state->sites.count > UI_SITES_VISIBLE &&
            state->sites.selected >=
                state->sites.first + UI_SITES_VISIBLE)
            state->sites.first =
                state->sites.selected - UI_SITES_VISIBLE + 1;
        break;
    default:
        break;
    }

    return count;
}
