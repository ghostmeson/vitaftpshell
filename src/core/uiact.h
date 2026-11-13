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

typedef enum {
    UI_PANE_LOCAL = 0,
    UI_PANE_REMOTE,
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
    UI_ACTION_COUNT
} UiAction;

typedef enum {
    UI_CONFIRM_NONE = 0,
    UI_CONFIRM_DELETE,
    UI_CONFIRM_CONFLICT,
    UI_CONFIRM_CANCEL_XFER,
    UI_CONFIRM_STOP_SERVER,
    UI_CONFIRM_DISCARD,
    UI_CONFIRM_HIGH_RISK,
    UI_CONFIRM_DELETE_SITE
} UiConfirmKind;

typedef enum {
    UI_CHOICE_CANCEL = 0,
    UI_CHOICE_SKIP,
    UI_CHOICE_OVERWRITE,
    UI_CHOICE_DELETE,
    UI_CHOICE_KEEP,
    UI_CHOICE_DISCARD,
    UI_CHOICE_CONTINUE,
    UI_CHOICE_STOP
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
    char path[PATH_MAX_LEN];
    char path2[PATH_MAX_LEN];
    char text[256];
    char title[64];
    Site site; /* SAVE_SITE carries the whole site, never a pointer */
} UiCommand;

const char *ui_command_name(UiCommandType type);
const char *ui_confirm_name(UiConfirmKind kind);
const char *ui_field_name(UiField field);

#endif
