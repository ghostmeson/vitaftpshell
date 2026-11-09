/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_UISTATE_H
#define VFTP_CORE_UISTATE_H

#include <stdint.h>

#include "core/input.h"
#include "core/path.h"
#include "core/uiact.h"
#include "core/xfer.h"

/*
 * The one writer of UiState (U9). The reducer applies an input or engine event
 * and emits commands; it never draws and never calls the filesystem or the
 * network. Tests assert the state field by field after a scripted sequence.
 *
 * Rows and mark storage are owned by the caller and referenced here, so the
 * reducer allocates nothing and the listing arena can replace a generation
 * without copying every row through the UI.
 */

#define UI_ROWS_VISIBLE 10
#define UI_CONFIRM_VIEW 5

typedef enum {
    UI_ROW_FILE = 0,
    UI_ROW_DIR,
    UI_ROW_UP
} UiRowType;

typedef struct {
    char name[PATH_NAME_MAX];
    uint8_t type;
    uint8_t risk;
    uint64_t size;
    int size_known;
} UiRow;

typedef struct {
    Path path;
    uint64_t size;
    int size_known;
} UiMark;

typedef struct {
    char path[PATH_MAX_LEN];
    char pending_path[PATH_MAX_LEN];
    int connected;
    int loading;
    int empty;
    int session_lost;
    int truncated;
    char error[128];

    const UiRow *rows;
    int row_count;
    int selected;
    int scroll;

    UiMark *marks;
    int mark_count;
    int mark_cap;
    uint64_t marked_bytes;
    int marked_bytes_known;
} UiPaneState;

typedef enum {
    UI_DOCK_IDLE = 0,
    UI_DOCK_ACTIVE,
    UI_DOCK_DONE,
    UI_DOCK_FAILED
} UiDockState;

typedef struct {
    int active;
    int queued;
    int focused;
    UiDockState state;
    XferDirection direction;
    char name[PATH_NAME_MAX];
    uint64_t done;
    uint64_t total;
    int total_known;
    char message[128];
} UiDock;

typedef struct {
    const char *name;
    uint64_t size;
    int size_known;
} UiConfirmItem;

typedef struct {
    UiConfirmKind kind;
    const UiConfirmItem *items;
    int item_count;
    int item_scroll;
    UiChoice choices[3];
    int choice_count;
    int choice_focus;
    int high_risk;
    char risk_path[PATH_MAX_LEN];
    char title[64];
    char context[128];
    char warning[160];
} UiConfirm;

typedef struct {
    int count;
    int selected;
    int save_unavailable;
} UiSiteList;

typedef struct {
    int field;
    int dirty;
    int anonymous;
    int port;
    char name[64];
    char host[256];
    char username[64];
    char password[128];
    char error[128];
    int password_set;
} UiEditor;

typedef enum {
    UI_SERVER_OFF = 0,
    UI_SERVER_STARTING,
    UI_SERVER_ON
} UiServerRun;

typedef struct {
    UiServerRun run;
    int clients;
    int port;
    int focus_start;
    char error[128];
} UiServer;

typedef struct {
    int selected;
    int marked;
    int enabled[UI_ACTION_COUNT];
    char path[PATH_MAX_LEN];
    char dest[PATH_MAX_LEN];
    char other[PATH_MAX_LEN];
} UiActions;

typedef struct {
    int open;
    UiField field;
    char title[64];
    char value[256];
    int masked;
    int max;
} UiTextEntry;

typedef enum {
    UI_FOCUS_LIST = 0,
    UI_FOCUS_DOCK,
    UI_FOCUS_SITES,
    UI_FOCUS_EDITOR,
    UI_FOCUS_SERVER,
    UI_FOCUS_ACTIONS,
    UI_FOCUS_CONFIRM
} UiFocus;

typedef struct {
    UiScreen screen;
    UiScreen return_screen;
    UiScreen confirm_return;
    UiScreen editor_return;
    UiPane active_pane;
    UiFocus focus;

    UiPaneState panes[UI_PANE_COUNT];
    UiDock dock;
    UiConfirm confirm;
    UiSiteList sites;
    UiEditor editor;
    UiServer server;
    UiActions actions;
    UiTextEntry text;

    char status[128];
    int quit;

    int confirm_from_worker;
    UiAction pending_action;
    UiPane pending_pane;
    Path pending_path;
    Path pending_dest;
} UiState;

typedef enum {
    UI_EVT_KEY = 0,
    UI_EVT_STICK,
    UI_EVT_LISTING_BEGIN,
    UI_EVT_SET_ROWS,
    UI_EVT_LISTING_END,
    UI_EVT_SESSION_LOST,
    UI_EVT_SESSION_OK,
    UI_EVT_ERROR,
    UI_EVT_TRANSFER_STARTED,
    UI_EVT_TRANSFER_PROGRESS,
    UI_EVT_TRANSFER_DONE,
    UI_EVT_TRANSFER_FAILED,
    UI_EVT_BATCH_DONE,
    UI_EVT_SERVER_STATE,
    UI_EVT_CONFIRM_REQUEST,
    UI_EVT_IME_RESULT,
    UI_EVT_SITE_SAVED,
    UI_EVT_SITES
} UiStateEventKind;

typedef struct {
    UiStateEventKind kind;
    UiPane pane;
    UiKey key;
    int repeat;
    int stick_y;
    int ok;
    char path[PATH_MAX_LEN];
    char text[256];
    const UiRow *rows;
    int row_count;
    int truncated;
    UiConfirmKind confirm;
    const UiConfirmItem *items;
    int item_count;
    int high_risk;
    char risk_path[PATH_MAX_LEN];
    int value;
    int clients;
    uint64_t bytes;
    uint64_t total;
    int total_known;
} UiStateEvent;

void ui_state_init(UiState *state, UiMark *local_marks, int local_cap,
                   UiMark *remote_marks, int remote_cap);

int ui_state_handle(UiState *state, const UiStateEvent *event, UiCommand *out,
                    int max);

void ui_state_set_rows(UiState *state, UiPane pane, const UiRow *rows,
                       int count, int truncated);
int ui_state_mark_count(const UiState *state, UiPane pane);
int ui_state_is_marked(const UiState *state, UiPane pane, const char *path);
const UiRow *ui_state_selected_row(const UiState *state, UiPane pane);

#endif
