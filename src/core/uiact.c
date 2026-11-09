/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/uiact.h"

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
    case UI_CMD_CLEAR_QUEUE:
        return "clear_queue";
    case UI_CMD_QUIT:
        return "quit";
    case UI_CMD_NONE:
    default:
        return "none";
    }
}

const char *ui_confirm_name(UiConfirmKind kind)
{
    switch (kind) {
    case UI_CONFIRM_DELETE:
        return "delete";
    case UI_CONFIRM_CONFLICT:
        return "conflict";
    case UI_CONFIRM_CANCEL_XFER:
        return "cancel_transfer";
    case UI_CONFIRM_STOP_SERVER:
        return "stop_server";
    case UI_CONFIRM_DISCARD:
        return "discard";
    case UI_CONFIRM_HIGH_RISK:
        return "high_risk";
    case UI_CONFIRM_DELETE_SITE:
        return "delete_site";
    case UI_CONFIRM_NONE:
    default:
        return "none";
    }
}

const char *ui_field_name(UiField field)
{
    switch (field) {
    case UI_FIELD_NAME:
        return "NAME";
    case UI_FIELD_HOST:
        return "HOST";
    case UI_FIELD_PORT:
        return "PORT";
    case UI_FIELD_ANONYMOUS:
        return "ANONYMOUS";
    case UI_FIELD_USERNAME:
        return "USERNAME";
    case UI_FIELD_PASSWORD:
        return "PASSWORD";
    case UI_FIELD_COUNT:
    default:
        return "";
    }
}
