/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_UISTATE_H
#define VFTP_CORE_UISTATE_H

#include <stdint.h>

#include "core/input.h"
#include "core/path.h"
#include "core/site.h"
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
#define UI_CONFIRM_MAX 64

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
    char site[SITE_NAME_MAX];
    char free_text[32];
    int free_known;

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
    int item_index; /* 1-based, 0 when unknown */
    int item_total;
    uint64_t elapsed_ms;
    int elapsed_known;
    uint64_t rate;
    int rate_known;
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
    int existing_count;
    char risk_path[PATH_MAX_LEN];
    char title[64];
    char context[128];
    char warning[160];
    /* Reducer-owned item storage, so a local confirmation never points at a
     * caller buffer that has gone out of scope. */
    UiConfirmItem local_items[UI_CONFIRM_MAX];
    char local_names[UI_CONFIRM_MAX][PATH_NAME_MAX];
} UiConfirm;

#define UI_SITES_MAX SITE_BOOK_MAX

typedef struct {
    char name[SITE_NAME_MAX];
    char host[SITE_HOST_MAX];
    uint16_t port;
    int anonymous;
    int password_set;
    char username[SITE_USER_MAX];
} UiSiteRow;

typedef struct {
    int count;
    int selected;
    int first;
    int save_unavailable;
    int login_failed;
    char reply[160];
    UiSiteRow rows[UI_SITES_MAX];
} UiSiteList;

/* The worker's site-book view for the UI. Small enough for the handoff bulk
 * lane, and deliberately without passwords. */
typedef struct {
    int count;
    int save_unavailable;
    UiSiteRow rows[UI_SITES_MAX];
} UiSiteSnapshot;

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
    int new_site;
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
    char address[32];
    int address_known;
    int port_focus;
    int client_active;
    char client_addr[32];
    char client_op[32];
    char client_path[PATH_MAX_LEN];
    uint64_t client_bytes;
    uint64_t client_total;
    int client_total_known;
} UiServer;

typedef struct {
    int selected;
    int marked;
    int enabled[UI_ACTION_COUNT];
    char reason[UI_ACTION_COUNT][40];
    char path[PATH_MAX_LEN];
    char dest[PATH_MAX_LEN];
    char other[PATH_MAX_LEN];
    int pane;
    int visible[UI_ACTION_COUNT];
    int visible_count;
} UiActions;

typedef struct {
    int open;
    UiField field;
    char title[64];
    char value[256];
    int masked;
    int max;
    int unavailable;
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

    char site_name[SITE_NAME_MAX];
    char site_host[SITE_HOST_MAX];
    int site_port;
    int site_known;

    int confirm_from_worker;
    int pending_action_valid;
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
    UI_EVT_SITES,
    UI_EVT_IME_UNAVAILABLE,
    UI_EVT_SERVER_CLIENT,
    UI_EVT_TRANSFER_CANCELLED,
    UI_EVT_PANE_INFO,
    UI_EVT_EDIT_SITE
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
    const UiSiteRow *site_rows;
    int site_count;
    int save_unavailable;
    char site[SITE_NAME_MAX];
    char host[SITE_HOST_MAX];
    int port;
    char address[32];
    int address_known;
    char op[32];
    int item_index;
    int item_total;
    uint64_t elapsed_ms;
    int elapsed_known;
    uint64_t rate;
    int rate_known;
    int direction;
    int direction_known;
    int existing_count;
    Site edit_site;
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

/* Recompute which actions apply to the active pane and why a disabled one is
 * disabled. The reducer calls this when ACTIONS opens; tests may call it
 * directly against a constructed state. */
void ui_actions_prepare(UiState *state);

#endif
