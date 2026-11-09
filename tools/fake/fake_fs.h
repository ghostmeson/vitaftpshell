/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_FAKE_FS_H
#define VFTP_FAKE_FS_H

#include <stddef.h>
#include <stdint.h>

#include "platform/fs.h"

/*
 * In-memory filesystem for host tests. One process, one singleton.
 *
 * Files carry their own bytes so store/save tests can round-trip real content.
 * Failures are one-shot and per operation, so a test can fail the exact call it
 * wants without poisoning the rest of the run.
 */

typedef enum {
    FAKE_FS_OP_LIST = 0,
    FAKE_FS_OP_STAT,
    FAKE_FS_OP_OPEN,
    FAKE_FS_OP_READ,
    FAKE_FS_OP_WRITE,
    FAKE_FS_OP_SYNC,
    FAKE_FS_OP_CLOSE,
    FAKE_FS_OP_MKDIR,
    FAKE_FS_OP_RMDIR,
    FAKE_FS_OP_RENAME,
    FAKE_FS_OP_REMOVE,
    FAKE_FS_OP_FREE_SPACE,
    FAKE_FS_OP_COUNT
} FakeFsOp;

void fake_fs_reset(void);
FsPort *fake_fs_port(void);

void fake_fs_add_dir(const char *path);
void fake_fs_add_file(const char *path, uint64_t size);
void fake_fs_seed_file(const char *path, const void *data, size_t len);
size_t fake_fs_read_file(const char *path, void *buf, size_t cap);
int fake_fs_exists(const char *path);
size_t fake_fs_node_count(void);

void fake_fs_set_free(const char *volume, uint64_t bytes, int known);
void fake_fs_set_mtime(const char *path, uint32_t mtime);
void fake_fs_set_rename_replaces(int yes);
void fake_fs_fail_op(FakeFsOp op, FsResult result);
/* Make the Nth list_next call (0-based) on every new handle fail. */
void fake_fs_fail_list_after(int entries);

int fake_fs_sync_count(void);
/* Fail one specific sync or rename by its 0-based call index. */
void fake_fs_fail_sync_at(int index);
void fake_fs_fail_rename_at(int index);
/* Fail every remove at or after a 0-based call index. */
void fake_fs_fail_remove_from(int index);
/* Fail one specific remove by its 0-based call index. */
void fake_fs_fail_remove_at(int index);

#endif
