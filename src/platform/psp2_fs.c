/* SPDX-License-Identifier: GPL-3.0-only */

#include "platform/psp2_fs.h"

#include <errno.h>
#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

/*
 * The one adapter that owns sceIo. A handle is a pool slot, never a raw fd, so
 * the core cannot mistake a Vita file descriptor for a domain value. Directory
 * names and mtimes are reported with their uncertainty explicit.
 */

#define PSP2_FS_HANDLES 16
#define PSP2_FS_FILE_MODE 0666
#define PSP2_FS_DIR_MODE 0777
/* The device-info command used by the SDK's own sceIoDevctl example. */
#define PSP2_FS_DEVCTL_DEV_INFO 0x3001u
#define PSP2_FS_VOLUME_MAX 16

struct FsDir {
    int used;
    SceUID fd;
};

struct FsFile {
    int used;
    SceUID fd;
};

static struct FsDir s_dirs[PSP2_FS_HANDLES];
static struct FsFile s_files[PSP2_FS_HANDLES];
static FsPort s_port;

/*
 * SCE file errors are 0x80010000 | errno. The SDK's libc converts them with
 * __vita_sce_errno_to_errno, which keeps the low byte as the newlib errno, so
 * the newlib constants are the right key. Anything outside this table is
 * FS_IO rather than a guessed category.
 */
static FsResult fs_result_from_error(int code)
{
    if (code >= 0)
        return FS_OK;

    switch (code) {
    case (int)(0x80010000u | (unsigned)ENOENT):
        return FS_NOT_FOUND;
    case (int)(0x80010000u | (unsigned)EACCES):
    case (int)(0x80010000u | (unsigned)EPERM):
        return FS_DENIED;
    case (int)(0x80010000u | (unsigned)EEXIST):
        return FS_EXISTS;
    case (int)(0x80010000u | (unsigned)ENOTEMPTY):
        return FS_NOT_EMPTY;
    case (int)(0x80010000u | (unsigned)ENOSPC):
        return FS_NO_SPACE;
    case (int)(0x80010000u | (unsigned)EBUSY):
        return FS_BUSY;
    case (int)(0x80010000u | (unsigned)EINVAL):
    case (int)(0x80010000u | (unsigned)ENOTDIR):
    case (int)(0x80010000u | (unsigned)EISDIR):
        return FS_INVALID;
    default:
        return FS_IO;
    }
}

static void copy_name(char *dst, size_t cap, const char *src, size_t src_cap)
{
    if (cap == 0)
        return;

    size_t len = 0;
    while (len < src_cap && src[len] != '\0')
        len++;
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* The final component of a device path: after the last '/' or ':'. */
static const char *path_name(const char *path)
{
    const char *start = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == ':')
            start = p + 1;
    }
    return start;
}

static void fill_entry(const SceIoStat *stat, const char *name, size_t name_cap,
                       FsEntry *out)
{
    memset(out, 0, sizeof(*out));

    if (SCE_S_ISDIR(stat->st_mode))
        out->type = FS_ENTRY_DIR;
    else if (SCE_S_ISREG(stat->st_mode))
        out->type = FS_ENTRY_FILE;
    else
        out->type = FS_ENTRY_OTHER;

    if (out->type == FS_ENTRY_FILE && stat->st_size >= 0) {
        out->size = (uint64_t)stat->st_size;
        out->size_known = 1;
    }

    /* [C] FsEntry.mtime is an open decision: the server's MDTM/MLSD formatter
     * assumes Unix seconds and the platform unit is unverified, so report
     * unknown rather than a value that could be misread. */
    out->mtime = 0;
    out->mtime_known = 0;

    copy_name(out->name, sizeof(out->name), name, name_cap);
}

static FsDir *dir_alloc(void)
{
    for (int i = 0; i < PSP2_FS_HANDLES; ++i) {
        if (!s_dirs[i].used) {
            s_dirs[i].used = 1;
            s_dirs[i].fd = -1;
            return &s_dirs[i];
        }
    }
    return NULL;
}

static FsFile *file_alloc(void)
{
    for (int i = 0; i < PSP2_FS_HANDLES; ++i) {
        if (!s_files[i].used) {
            s_files[i].used = 1;
            s_files[i].fd = -1;
            return &s_files[i];
        }
    }
    return NULL;
}

static FsResult fs_list_open(void *ctx, const char *path, FsDir **out)
{
    (void)ctx;
    if (path == NULL || out == NULL)
        return FS_INVALID;

    FsDir *dir = dir_alloc();
    if (dir == NULL)
        return FS_BUSY;

    SceUID fd = sceIoDopen(path);
    if (fd < 0) {
        dir->used = 0;
        return fs_result_from_error(fd);
    }

    dir->fd = fd;
    *out = dir;
    return FS_OK;
}

static FsResult fs_list_next(void *ctx, FsDir *dir, FsEntry *out)
{
    (void)ctx;
    if (dir == NULL || !dir->used || out == NULL)
        return FS_INVALID;

    for (;;) {
        SceIoDirent entry;
        memset(&entry, 0, sizeof(entry));

        int rc = sceIoDread(dir->fd, &entry);
        if (rc < 0)
            return fs_result_from_error(rc);
        if (rc == 0)
            return FS_END;

        if (strcmp(entry.d_name, ".") == 0 ||
            strcmp(entry.d_name, "..") == 0)
            continue;

        fill_entry(&entry.d_stat, entry.d_name, sizeof(entry.d_name), out);
        return FS_OK;
    }
}

static void fs_list_close(void *ctx, FsDir *dir)
{
    (void)ctx;
    if (dir == NULL || !dir->used)
        return;

    int rc = sceIoDclose(dir->fd);
    (void)rc; /* the slot is released even when the close reports an error */
    dir->used = 0;
    dir->fd = -1;
}

static FsResult fs_stat(void *ctx, const char *path, FsEntry *out)
{
    (void)ctx;
    if (path == NULL || out == NULL)
        return FS_INVALID;

    SceIoStat stat;
    memset(&stat, 0, sizeof(stat));

    int rc = sceIoGetstat(path, &stat);
    if (rc < 0)
        return fs_result_from_error(rc);

    const char *name = path_name(path);
    fill_entry(&stat, name, strlen(name), out);
    return FS_OK;
}

static FsResult fs_open(void *ctx, const char *path, FsOpenMode mode,
                        FsFile **out)
{
    (void)ctx;
    if (path == NULL || out == NULL)
        return FS_INVALID;

    int flags;
    switch (mode) {
    case FS_OPEN_READ:
        flags = SCE_O_RDONLY;
        break;
    case FS_OPEN_WRITE_CREATE:
        flags = SCE_O_WRONLY | SCE_O_CREAT;
        break;
    case FS_OPEN_WRITE_TRUNC:
        flags = SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC;
        break;
    default:
        return FS_INVALID;
    }

    FsFile *file = file_alloc();
    if (file == NULL)
        return FS_BUSY;

    SceUID fd = sceIoOpen(path, flags, PSP2_FS_FILE_MODE);
    if (fd < 0) {
        file->used = 0;
        return fs_result_from_error(fd);
    }

    file->fd = fd;
    *out = file;
    return FS_OK;
}

static FsResult fs_read(void *ctx, FsFile *file, void *buf, size_t cap,
                        size_t *got)
{
    (void)ctx;
    if (file == NULL || !file->used || got == NULL ||
        (buf == NULL && cap != 0))
        return FS_INVALID;

    *got = 0;
    if (cap == 0)
        return FS_OK;

    SceSSize rc = sceIoRead(file->fd, buf, (SceSize)cap);
    if (rc < 0)
        return fs_result_from_error(rc);
    if (rc == 0)
        return FS_END;

    *got = (size_t)rc;
    return FS_OK;
}

static FsResult fs_write(void *ctx, FsFile *file, const void *buf, size_t len,
                         size_t *written)
{
    (void)ctx;
    if (file == NULL || !file->used || written == NULL ||
        (buf == NULL && len != 0))
        return FS_INVALID;

    *written = 0;
    if (len == 0)
        return FS_OK;

    const char *bytes = (const char *)buf;
    size_t total = 0;

    while (total < len) {
        SceSSize rc = sceIoWrite(file->fd, bytes + total,
                                 (SceSize)(len - total));
        if (rc < 0) {
            *written = total;
            return fs_result_from_error(rc);
        }
        if (rc == 0) {
            *written = total;
            return FS_IO; /* no progress: do not spin on a stuck device */
        }
        total += (size_t)rc;
    }

    *written = total;
    return FS_OK;
}

static FsResult fs_sync(void *ctx, FsFile *file)
{
    (void)ctx;
    if (file == NULL || !file->used)
        return FS_INVALID;

    int rc = sceIoSyncByFd(file->fd, 0);
    return fs_result_from_error(rc);
}

static FsResult fs_close(void *ctx, FsFile *file)
{
    (void)ctx;
    if (file == NULL || !file->used)
        return FS_INVALID;

    int rc = sceIoClose(file->fd);
    file->used = 0;
    file->fd = -1;
    return fs_result_from_error(rc);
}

static FsResult fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    if (path == NULL)
        return FS_INVALID;
    return fs_result_from_error(sceIoMkdir(path, PSP2_FS_DIR_MODE));
}

static FsResult fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    if (path == NULL)
        return FS_INVALID;
    return fs_result_from_error(sceIoRmdir(path));
}

static FsResult fs_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    if (from == NULL || to == NULL)
        return FS_INVALID;
    return fs_result_from_error(sceIoRename(from, to));
}

static FsResult fs_remove(void *ctx, const char *path)
{
    (void)ctx;
    if (path == NULL)
        return FS_INVALID;
    return fs_result_from_error(sceIoRemove(path));
}

/* Copy the device prefix, including the colon. Zero means the path names no
 * device, which is an unknown capacity rather than an error. */
static size_t volume_prefix(const char *path, char *out, size_t cap)
{
    size_t len = 0;
    while (path[len] != '\0' && path[len] != ':')
        len++;

    if (path[len] != ':' || len + 2 > cap)
        return 0;

    memcpy(out, path, len + 1);
    out[len + 1] = '\0';
    return len + 1;
}

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int volume_is(const char *volume, const char *wanted)
{
    while (*volume != '\0' && *wanted != '\0') {
        if (lower_char(*volume) != lower_char(*wanted))
            return 0;
        volume++;
        wanted++;
    }
    return *volume == '\0' && *wanted == '\0';
}

static FsResult fs_free_space(void *ctx, const char *path, FsFreeSpace *out)
{
    (void)ctx;
    if (path == NULL || out == NULL)
        return FS_INVALID;

    out->known = 0;
    out->bytes = 0;

    char volume[PSP2_FS_VOLUME_MAX];
    if (volume_prefix(path, volume, sizeof(volume)) == 0)
        return FS_OK;

    /* VitaShell queries free space on the device entry; the worker now does
     * that once per volume. Only ux0: is proven here. The b21 console warning
     * about the memory card followed repeated devctl calls on ur0:/uma0:, so
     * those volumes report unknown and the UI renders --. */
    if (!volume_is(volume, "ux0:"))
        return FS_OK;

    SceIoDevInfo info;
    memset(&info, 0, sizeof(info));

    int rc = sceIoDevctl(volume, PSP2_FS_DEVCTL_DEV_INFO, NULL, 0, &info,
                         (int)sizeof(info));
    if (rc >= 0 && info.free_size >= 0) {
        out->known = 1;
        out->bytes = (uint64_t)info.free_size;
        return FS_OK;
    }

    /* The app-manager query is documented for ux0: only. */
    uint64_t max_size = 0;
    uint64_t free_size = 0;
    if (sceAppMgrGetDevInfo("ux0:", &max_size, &free_size) >= 0) {
        (void)max_size;
        out->known = 1;
        out->bytes = free_size;
        return FS_OK;
    }

    /* Both queries failed: unknown is reported, never a guessed zero. */
    return FS_OK;
}

static int fs_rename_replaces(void *ctx)
{
    (void)ctx;
    /* [C] Whether sceIoRename replaces an existing target is unverified on
     * hardware (the plan's deferred list). Report the conservative answer so
     * the core takes the .vftp.old aside path from KTD12. */
    return 0;
}

FsPort *psp2_fs_port(void)
{
    s_port.ctx = NULL;
    s_port.list_open = fs_list_open;
    s_port.list_next = fs_list_next;
    s_port.list_close = fs_list_close;
    s_port.stat = fs_stat;
    s_port.open = fs_open;
    s_port.read = fs_read;
    s_port.write = fs_write;
    s_port.sync = fs_sync;
    s_port.close = fs_close;
    s_port.mkdir = fs_mkdir;
    s_port.rmdir = fs_rmdir;
    s_port.rename = fs_rename;
    s_port.remove = fs_remove;
    s_port.free_space = fs_free_space;
    s_port.rename_replaces = fs_rename_replaces;
    return &s_port;
}
