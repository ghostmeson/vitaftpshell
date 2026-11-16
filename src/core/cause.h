/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_CAUSE_H
#define VFTP_CORE_CAUSE_H

/*
 * One failure vocabulary for the whole core. A transfer, a local operation,
 * and a server session all record one cause and the stage they reached, so the
 * dock, the log, and the release metric cannot drift (D33).
 */

typedef enum {
    OP_CAUSE_APP = 0,
    OP_CAUSE_SERVER,
    OP_CAUSE_NETWORK,
    OP_CAUSE_STORAGE,
    OP_CAUSE_USER
} OpCause;

typedef enum {
    OP_STAGE_NONE = 0,
    OP_STAGE_RESOLVE,
    OP_STAGE_CONNECT,
    OP_STAGE_GREETING,
    OP_STAGE_LOGIN,
    OP_STAGE_FIRST_LISTING,
    OP_STAGE_COMMAND,
    OP_STAGE_DATA,
    OP_STAGE_TRANSFER,
    OP_STAGE_PLAN,
    OP_STAGE_WRITE,
    OP_STAGE_COMMIT,
    OP_STAGE_ROLLBACK
} OpStage;

const char *op_cause_name(OpCause cause);
const char *op_stage_name(OpStage stage);

#endif
