#ifndef VFTP_PLATFORM_FS_H
#define VFTP_PLATFORM_FS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Filesystem contract. Handles are opaque; the core never sees a sceIo fd.
 *
 * A directory is read one entry per call against an open handle, so a 2000-row
 * listing needs no allocation of its own. A read or listing that fails
 * mid-way returns an error, never a short result that looks complete.
 */

#define FS_NAME_MAX 256

typedef enum {
    FS_OK = 0,
    FS_END,
    FS_NOT_FOUND,
    FS_DENIED,
    FS_EXISTS,
    FS_NOT_EMPTY,
    FS_NO_SPACE,
    FS_BUSY,
    FS_IO,
    FS_INVALID,
    FS_UNSUPPORTED
} FsResult;

typedef enum {
    FS_ENTRY_FILE = 0,
    FS_ENTRY_DIR,
    FS_ENTRY_OTHER
} FsEntryType;

typedef struct {
    char name[FS_NAME_MAX];
    FsEntryType type;
    uint64_t size;
    int size_known;
    uint32_t mtime;
    int mtime_known;
} FsEntry;

typedef struct FsDir FsDir;
typedef struct FsFile FsFile;

typedef enum {
    FS_OPEN_READ = 0,
    FS_OPEN_WRITE_CREATE, /* create when absent, keep existing contents */
    FS_OPEN_WRITE_TRUNC   /* create or truncate */
} FsOpenMode;

/* known == 0 means the platform cannot report it; bytes is then meaningless. */
typedef struct {
    int known;
    uint64_t bytes;
} FsFreeSpace;

typedef struct FsPort FsPort;

struct FsPort {
    void *ctx;

    FsResult (*list_open)(void *ctx, const char *path, FsDir **out);
    FsResult (*list_next)(void *ctx, FsDir *dir, FsEntry *out);
    void (*list_close)(void *ctx, FsDir *dir);

    FsResult (*stat)(void *ctx, const char *path, FsEntry *out);

    FsResult (*open)(void *ctx, const char *path, FsOpenMode mode, FsFile **out);
    FsResult (*read)(void *ctx, FsFile *file, void *buf, size_t cap,
                     size_t *got);
    FsResult (*write)(void *ctx, FsFile *file, const void *buf, size_t len,
                      size_t *written);
    FsResult (*sync)(void *ctx, FsFile *file);
    FsResult (*close)(void *ctx, FsFile *file);

    FsResult (*mkdir)(void *ctx, const char *path);
    FsResult (*rmdir)(void *ctx, const char *path);
    FsResult (*rename)(void *ctx, const char *from, const char *to);
    FsResult (*remove)(void *ctx, const char *path);

    FsResult (*free_space)(void *ctx, const char *path, FsFreeSpace *out);

    /* 1 when a rename over an existing target replaces it. */
    int (*rename_replaces)(void *ctx);
};

#endif
