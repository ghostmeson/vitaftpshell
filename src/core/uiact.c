/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/uiact.h"

const char *ui_action_context(UiAction action)
{
    switch (action) {
    case UI_ACTION_MOVE:
        return "MOVE";
    case UI_ACTION_RENAME:
        return "RENAME";
    case UI_ACTION_NEW_FOLDER:
        return "NEW FOLDER";
    case UI_ACTION_DELETE:
        return "DELETE";
    case UI_ACTION_UPLOAD:
    case UI_ACTION_DOWNLOAD:
        return "TRANSFER";
    default:
        return "COPY";
    }
}

const char *ui_command_name(UiCommandType type)
{
    switch (type) {
    case UI_CMD_OPEN_PATH:
        return "open_path";
    case UI_CMD_GO_PARENT:
        return "go_parent";
    case UI_CMD_TRANSFER:
        return "transfer";
    case UI_CMD_REFRESH:
        return "refresh";
    case UI_CMD_CONNECT_SITE:
        return "connect_site";
    case UI_CMD_EDIT_SITE:
        return "edit_site";
    case UI_CMD_NEW_SITE:
        return "new_site";
    case UI_CMD_DELETE_SITE:
        return "delete_site";
    case UI_CMD_SAVE_SITE:
        return "save_site";
    case UI_CMD_RUN_ACTION:
        return "run_action";
    case UI_CMD_CONFIRM:
        return "confirm";
    case UI_CMD_OPEN_IME:
        return "open_ime";
    case UI_CMD_START_SERVER:
        return "start_server";
    case UI_CMD_STOP_SERVER:
        return "stop_server";
    case UI_CMD_EDIT_PORT:
        return "edit_port";
    case UI_CMD_RECONNECT:
        return "reconnect";
    case UI_CMD_DISMISS_ERROR:
        return "dismiss_error";
    case UI_CMD_CANCEL_TRANSFER:
        return "cancel_transfer";
    case UI_CMD_CANCEL_NETWORK:
        return "cancel_network";
    case UI_CMD_CANCEL_LOCAL:
        return "cancel_local";
    case UI_CMD_CLEAR_QUEUE:
        return "clear_queue";
    case UI_CMD_QUIT:
        return "quit";
    case UI_CMD_NONE:
    default:
        return "none";
    }
}
