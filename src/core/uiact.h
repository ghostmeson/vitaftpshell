/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_UIACT_H
#define VFTP_CORE_UIACT_H

#include <stdint.h>

#include "core/path.h"
#include "core/site.h"

/*
 * The command vocabulary the reducer emits and the worker consumes. A command
 * is a value, never a callback: the UI never calls the engine directly.
 */

/*
 * The view key, not the slot. UI_PANE_LOCAL is the left slot and is always
 * local; the right slot shows either UI_PANE_REMOTE or UI_PANE_LOCAL_RIGHT,
 * selected by UiState.right_view (KTD1). The left keeps index 0 so every
 * existing local reference keeps its meaning.
 */
typedef enum {
    UI_PANE_LOCAL = 0,
    UI_PANE_REMOTE,
    UI_PANE_LOCAL_RIGHT,
    UI_PANE_COUNT
} UiPane;

typedef enum {
    UI_SCREEN_WORKSPACE = 0,
    UI_SCREEN_SITES,
    UI_SCREEN_SITE_EDITOR,
    UI_SCREEN_SERVER,
    UI_SCREEN_ACTIONS,
    UI_SCREEN_CONFIRM,
    UI_SCREEN_TEXT_ENTRY
} UiScreen;

typedef enum {
    UI_ACTION_COPY = 0,
    UI_ACTION_MOVE,
    UI_ACTION_PASTE,
    UI_ACTION_CLEAR_CLIPBOARD,
    UI_ACTION_UPLOAD,
    UI_ACTION_DOWNLOAD,
    UI_ACTION_NEW_FOLDER,
    UI_ACTION_RENAME,
    UI_ACTION_DELETE,
    UI_ACTION_REFRESH,
    UI_ACTION_DISCONNECT,
    UI_ACTION_SWITCH_LOCAL,
    UI_ACTION_SWITCH_REMOTE,
    UI_ACTION_COUNT
} UiAction;

typedef enum {
    UI_CONFIRM_NONE = 0,
    UI_CONFIRM_DELETE,
    UI_CONFIRM_CONFLICT,
    UI_CONFIRM_CANCEL_XFER,
    UI_CONFIRM_STOP_SERVER,
    UI_CONFIRM_DISCARD,
    UI_CONFIRM_DELETE_SITE,
    UI_CONFIRM_PATH_BUSY,
    UI_CONFIRM_CANCEL_LANES,
    /* A same-folder paste asks SAVE DUPLICATES or CANCEL (R16, KTD6). */
    UI_CONFIRM_DUPLICATE,
    /* A cross-pane move asks once before it starts (R15). */
    UI_CONFIRM_MOVE
} UiConfirmKind;

typedef enum {
    UI_CHOICE_CANCEL = 0,
    UI_CHOICE_SKIP,
    UI_CHOICE_OVERWRITE,
    UI_CHOICE_DELETE,
    UI_CHOICE_KEEP,
    UI_CHOICE_DISCARD,
    UI_CHOICE_STOP,
    UI_CHOICE_WAIT,
    UI_CHOICE_CANCEL_NETWORK,
    UI_CHOICE_CANCEL_LOCAL,
    UI_CHOICE_SAVE_DUPLICATE,
    UI_CHOICE_MOVE
} UiChoice;

typedef enum {
    UI_FIELD_NAME = 0,
    UI_FIELD_HOST,
    UI_FIELD_PORT,
    UI_FIELD_ANONYMOUS,
    UI_FIELD_USERNAME,
    UI_FIELD_PASSWORD,
    UI_FIELD_COUNT
} UiField;

typedef enum {
    UI_CMD_NONE = 0,
    UI_CMD_OPEN_PATH,
    UI_CMD_GO_PARENT,
    UI_CMD_TRANSFER,
    UI_CMD_REFRESH,
    UI_CMD_CONNECT_SITE,
    UI_CMD_EDIT_SITE,
    UI_CMD_NEW_SITE,
    UI_CMD_DELETE_SITE,
    UI_CMD_SAVE_SITE,
    UI_CMD_RUN_ACTION,
    UI_CMD_CONFIRM,
    UI_CMD_OPEN_IME,
    UI_CMD_START_SERVER,
    UI_CMD_STOP_SERVER,
    UI_CMD_EDIT_PORT,
    UI_CMD_RECONNECT,
    UI_CMD_DISMISS_ERROR,
    UI_CMD_CANCEL_TRANSFER,
    UI_CMD_CANCEL_NETWORK,
    UI_CMD_CANCEL_LOCAL,
    UI_CMD_CLEAR_QUEUE,
    UI_CMD_QUIT
} UiCommandType;

typedef struct {
    UiCommandType type;
    UiPane pane;
    UiAction action;
    UiConfirmKind confirm;
    UiChoice choice;
    UiField field;
    int index;
    int value;
    int masked;
    int source_type; /* UI_ROW_FILE or UI_ROW_DIR for a remote source */
    uint64_t total;
    int total_known;
    unsigned batch_id;
    unsigned prompt_id;
    int batch_index;
    int batch_total;
    int batch_last;
    /* A remote MOVE is a DOWNLOAD that verifies and then deletes its source
     * (R12). The action stays DOWNLOAD so a plain download and a move share
     * one transfer path; this flag is the only difference. */
    int move;
    char path[PATH_MAX_LEN];
    char path2[PATH_MAX_LEN];
    char text[256];
    char title[64];
    Site site; /* SAVE_SITE carries the whole site, never a pointer */
} UiCommand;

const char *ui_command_name(UiCommandType type);
const char *ui_action_context(UiAction action);

#endif
