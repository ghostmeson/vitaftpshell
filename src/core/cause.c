/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/cause.h"

const char *op_cause_name(OpCause cause)
{
    switch (cause) {
    case OP_CAUSE_SERVER:
        return "server";
    case OP_CAUSE_NETWORK:
        return "network";
    case OP_CAUSE_STORAGE:
        return "storage";
    case OP_CAUSE_USER:
        return "user";
    case OP_CAUSE_APP:
    default:
        return "app";
    }
}

const char *op_stage_name(OpStage stage)
{
    switch (stage) {
    case OP_STAGE_RESOLVE:
        return "resolve";
    case OP_STAGE_CONNECT:
        return "connect";
    case OP_STAGE_GREETING:
        return "greeting";
    case OP_STAGE_LOGIN:
        return "login";
    case OP_STAGE_FIRST_LISTING:
        return "first_listing";
    case OP_STAGE_COMMAND:
        return "command";
    case OP_STAGE_DATA:
        return "data";
    case OP_STAGE_TRANSFER:
        return "transfer";
    case OP_STAGE_PLAN:
        return "plan";
    case OP_STAGE_WRITE:
        return "write";
    case OP_STAGE_COMMIT:
        return "commit";
    case OP_STAGE_ROLLBACK:
        return "rollback";
    case OP_STAGE_NONE:
    default:
        return "none";
    }
}
