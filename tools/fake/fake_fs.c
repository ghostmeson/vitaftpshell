#include "fake_fs.h"

#include <stdio.h>
#include <string.h>

#define FAKE_FS_NODES 128
#define FAKE_FS_PATH_MAX 1024
#define FAKE_FS_CONTENT_MAX 8192
#define FAKE_FS_HANDLES 16
#define FAKE_FS_VOLUMES 3

typedef struct {
    int used;
    char path[FAKE_FS_PATH_MAX];
    char parent[FAKE_FS_PATH_MAX];
    char name[FS_NAME_MAX];
    FsEntryType type;
    uint64_t size;
    uint32_t mtime;
    int mtime_known;
    unsigned char content[FAKE_FS_CONTENT_MAX];
} FakeNode;

struct FsDir {
    int used;
    char parent[FAKE_FS_PATH_MAX];
    int next;
    int returned;
};

struct FsFile {
    int used;
    int node;
    size_t offset;
    FsOpenMode mode;
};

typedef struct {
    char volume[16];
    uint64_t bytes;
    int known;
} FakeVolume;

static FakeNode s_nodes[FAKE_FS_NODES];
static struct FsDir s_dirs[FAKE_FS_HANDLES];
static struct FsFile s_files[FAKE_FS_HANDLES];
static FakeVolume s_volumes[FAKE_FS_VOLUMES];
static FsPort s_port;
static int s_rename_replaces;
static FsResult s_fail[FAKE_FS_OP_COUNT];
static int s_fail_list_after;
static int s_sync_count;

static FsResult take_failure(FakeFsOp op)
{
    FsResult r = s_fail[op];
    s_fail[op] = FS_OK;
    return r;
}

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

/* The exposed volumes are case-insensitive; the fake models that. */
static int path_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (lower_char(*a) != lower_char(*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int path_prefix(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (lower_char(*text) != lower_char(*prefix))
            return 0;
        text++;
        prefix++;
    }
    return 1;
}

static void parent_of(const char *path, char *out, size_t cap)
{
    size_t n = strlen(path);
    while (n > 0 && path[n - 1] == '/')
        n--;

    size_t cut = 0;
    for (size_t i = 0; i < n; ++i) {
        if (path[i] == '/' || path[i] == ':')
            cut = i;
    }

    if (cut == 0) {
        out[0] = '\0';
        return;
    }

    /* A volume root such as `ux0:` has no parent above it. */
    if (path[cut] == ':' && cut + 1 == n) {
        out[0] = '\0';
        return;
    }

    size_t len = cut;
    if (path[cut] == ':')
        len = cut + 1;
    if (len >= cap)
        len = cap - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static void name_of(const char *path, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    const char *colon = strrchr(path, ':');
    const char *start = path;
    if (slash != NULL)
        start = slash + 1;
    if (colon != NULL && colon + 1 > start)
        start = colon + 1;

    size_t n = strlen(start);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, start, n);
    out[n] = '\0';
}

static FakeNode *find(const char *path)
{
    for (int i = 0; i < FAKE_FS_NODES; ++i) {
        if (s_nodes[i].used && path_equal(s_nodes[i].path, path))
            return &s_nodes[i];
    }
    return NULL;
}

static FakeNode *create(const char *path, FsEntryType type)
{
    FakeNode *existing = find(path);
    if (existing != NULL)
        return existing;

    for (int i = 0; i < FAKE_FS_NODES; ++i) {
        if (s_nodes[i].used)
            continue;
        memset(&s_nodes[i], 0, sizeof(s_nodes[i]));
        s_nodes[i].used = 1;
        s_nodes[i].type = type;
        strncpy(s_nodes[i].path, path, sizeof(s_nodes[i].path) - 1);
        parent_of(path, s_nodes[i].parent, sizeof(s_nodes[i].parent));
        name_of(path, s_nodes[i].name, sizeof(s_nodes[i].name));
        return &s_nodes[i];
    }
    return NULL;
}

static int has_children(const char *path)
{
    for (int i = 0; i < FAKE_FS_NODES; ++i) {
        if (s_nodes[i].used && path_equal(s_nodes[i].parent, path))
            return 1;
    }
    return 0;
}

static void fill_entry(const FakeNode *node, FsEntry *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->name, node->name, sizeof(out->name) - 1);
    out->type = node->type;
    out->size = node->size;
    out->size_known = node->type == FS_ENTRY_FILE;
    out->mtime = node->mtime;
    out->mtime_known = node->mtime_known;
}

static FsResult fs_list_open(void *ctx, const char *path, FsDir **out)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_LIST);
    if (failure != FS_OK)
        return failure;

    FakeNode *node = find(path);
    if (node == NULL)
        return FS_NOT_FOUND;
    if (node->type != FS_ENTRY_DIR)
        return FS_INVALID;

    for (int i = 0; i < FAKE_FS_HANDLES; ++i) {
        if (s_dirs[i].used)
            continue;
        memset(&s_dirs[i], 0, sizeof(s_dirs[i]));
        s_dirs[i].used = 1;
        strncpy(s_dirs[i].parent, path, sizeof(s_dirs[i].parent) - 1);
        *out = &s_dirs[i];
        return FS_OK;
    }
    return FS_BUSY;
}

static FsResult fs_list_next(void *ctx, FsDir *dir, FsEntry *out)
{
    (void)ctx;
    if (dir == NULL || !dir->used)
        return FS_INVALID;

    if (s_fail_list_after >= 0 && dir->returned == s_fail_list_after)
        return FS_IO;

    for (int i = dir->next; i < FAKE_FS_NODES; ++i) {
        if (!s_nodes[i].used)
            continue;
        if (!path_equal(s_nodes[i].parent, dir->parent))
            continue;
        dir->next = i + 1;
        dir->returned++;
        fill_entry(&s_nodes[i], out);
        return FS_OK;
    }
    return FS_END;
}

static void fs_list_close(void *ctx, FsDir *dir)
{
    (void)ctx;
    if (dir != NULL)
        dir->used = 0;
}

static FsResult fs_stat(void *ctx, const char *path, FsEntry *out)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_STAT);
    if (failure != FS_OK)
        return failure;

    FakeNode *node = find(path);
    if (node == NULL)
        return FS_NOT_FOUND;
    fill_entry(node, out);
    return FS_OK;
}

static FsResult fs_open(void *ctx, const char *path, FsOpenMode mode,
                        FsFile **out)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_OPEN);
    if (failure != FS_OK)
        return failure;

    FakeNode *node = find(path);
    if (mode == FS_OPEN_READ) {
        if (node == NULL || node->type != FS_ENTRY_FILE)
            return FS_NOT_FOUND;
    } else {
        if (node == NULL)
            node = create(path, FS_ENTRY_FILE);
        if (node == NULL)
            return FS_NO_SPACE;
        if (node->type != FS_ENTRY_FILE)
            return FS_INVALID;
        if (mode == FS_OPEN_WRITE_TRUNC)
            node->size = 0;
    }

    for (int i = 0; i < FAKE_FS_HANDLES; ++i) {
        if (s_files[i].used)
            continue;
        memset(&s_files[i], 0, sizeof(s_files[i]));
        s_files[i].used = 1;
        s_files[i].node = (int)(node - s_nodes);
        s_files[i].mode = mode;
        s_files[i].offset = mode == FS_OPEN_READ ? 0 : (size_t)node->size;
        *out = &s_files[i];
        return FS_OK;
    }
    return FS_BUSY;
}

static FsResult fs_read(void *ctx, FsFile *file, void *buf, size_t cap,
                        size_t *got)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_READ);
    if (failure != FS_OK)
        return failure;
    if (file == NULL || !file->used)
        return FS_INVALID;

    FakeNode *node = &s_nodes[file->node];
    if (file->offset >= node->size) {
        *got = 0;
        return FS_END;
    }

    size_t n = (size_t)(node->size - file->offset);
    if (n > cap)
        n = cap;
    memcpy(buf, node->content + file->offset, n);
    file->offset += n;
    *got = n;
    return FS_OK;
}

static FsResult fs_write(void *ctx, FsFile *file, const void *buf, size_t len,
                         size_t *written)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_WRITE);
    if (failure != FS_OK)
        return failure;
    if (file == NULL || !file->used || file->mode == FS_OPEN_READ)
        return FS_INVALID;

    FakeNode *node = &s_nodes[file->node];
    if (file->offset + len > FAKE_FS_CONTENT_MAX)
        return FS_NO_SPACE;

    memcpy(node->content + file->offset, buf, len);
    file->offset += len;
    if (file->offset > node->size)
        node->size = file->offset;
    *written = len;
    return FS_OK;
}

static FsResult fs_sync(void *ctx, FsFile *file)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_SYNC);
    if (failure != FS_OK)
        return failure;
    if (file == NULL || !file->used)
        return FS_INVALID;
    s_sync_count++;
    return FS_OK;
}

static FsResult fs_close(void *ctx, FsFile *file)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_CLOSE);
    if (failure != FS_OK)
        return failure;
    if (file == NULL || !file->used)
        return FS_INVALID;
    file->used = 0;
    return FS_OK;
}

static FsResult fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_MKDIR);
    if (failure != FS_OK)
        return failure;
    if (find(path) != NULL)
        return FS_EXISTS;
    return create(path, FS_ENTRY_DIR) != NULL ? FS_OK : FS_NO_SPACE;
}

static FsResult fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_RMDIR);
    if (failure != FS_OK)
        return failure;

    FakeNode *node = find(path);
    if (node == NULL)
        return FS_NOT_FOUND;
    if (node->type != FS_ENTRY_DIR)
        return FS_INVALID;
    if (has_children(path))
        return FS_NOT_EMPTY;
    node->used = 0;
    return FS_OK;
}

static FsResult fs_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_RENAME);
    if (failure != FS_OK)
        return failure;

    FakeNode *src = find(from);
    if (src == NULL)
        return FS_NOT_FOUND;

    /* A rename to the same path is a no-op, not a delete-then-move. */
    if (strcmp(from, to) == 0)
        return FS_OK;

    FakeNode *dst = find(to);
    if (dst != NULL) {
        if (!s_rename_replaces)
            return FS_EXISTS;
        dst->used = 0;
    }

    char old_path[FAKE_FS_PATH_MAX];
    strncpy(old_path, src->path, sizeof(old_path) - 1);
    old_path[sizeof(old_path) - 1] = '\0';

    strncpy(src->path, to, sizeof(src->path) - 1);
    parent_of(to, src->parent, sizeof(src->parent));
    name_of(to, src->name, sizeof(src->name));

    if (src->type == FS_ENTRY_DIR) {
        size_t old_len = strlen(old_path);
        for (int i = 0; i < FAKE_FS_NODES; ++i) {
            if (!s_nodes[i].used)
                continue;
            if (!path_prefix(s_nodes[i].path, old_path) ||
                s_nodes[i].path[old_len] != '/')
                continue;
            char suffix[FAKE_FS_PATH_MAX];
            strncpy(suffix, s_nodes[i].path + old_len, sizeof(suffix) - 1);
            suffix[sizeof(suffix) - 1] = '\0';
            snprintf(s_nodes[i].path, sizeof(s_nodes[i].path), "%s%s", to,
                     suffix);
            parent_of(s_nodes[i].path, s_nodes[i].parent,
                      sizeof(s_nodes[i].parent));
        }
    }
    return FS_OK;
}

static FsResult fs_remove(void *ctx, const char *path)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_REMOVE);
    if (failure != FS_OK)
        return failure;

    FakeNode *node = find(path);
    if (node == NULL)
        return FS_NOT_FOUND;
    if (node->type == FS_ENTRY_DIR)
        return FS_INVALID;
    node->used = 0;
    return FS_OK;
}

static FsResult fs_free_space(void *ctx, const char *path, FsFreeSpace *out)
{
    (void)ctx;
    FsResult failure = take_failure(FAKE_FS_OP_FREE_SPACE);
    if (failure != FS_OK)
        return failure;

    out->known = 0;
    out->bytes = 0;
    for (int i = 0; i < FAKE_FS_VOLUMES; ++i) {
        if (!s_volumes[i].known)
            continue;
        size_t n = strlen(s_volumes[i].volume);
        (void)n;
        if (path_prefix(path, s_volumes[i].volume)) {
            out->known = 1;
            out->bytes = s_volumes[i].bytes;
            return FS_OK;
        }
    }
    return FS_OK;
}

static int fs_rename_replaces(void *ctx)
{
    (void)ctx;
    return s_rename_replaces;
}

void fake_fs_reset(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_dirs, 0, sizeof(s_dirs));
    memset(s_files, 0, sizeof(s_files));
    memset(s_volumes, 0, sizeof(s_volumes));
    memset(s_fail, 0, sizeof(s_fail));
    s_rename_replaces = 1;
    s_fail_list_after = -1;
    s_sync_count = 0;

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
}

FsPort *fake_fs_port(void)
{
    return &s_port;
}

void fake_fs_add_dir(const char *path)
{
    (void)create(path, FS_ENTRY_DIR);
}

void fake_fs_add_file(const char *path, uint64_t size)
{
    FakeNode *node = create(path, FS_ENTRY_FILE);
    if (node != NULL)
        node->size = size > FAKE_FS_CONTENT_MAX ? FAKE_FS_CONTENT_MAX : size;
}

void fake_fs_seed_file(const char *path, const void *data, size_t len)
{
    FakeNode *node = create(path, FS_ENTRY_FILE);
    if (node == NULL)
        return;
    if (len > FAKE_FS_CONTENT_MAX)
        len = FAKE_FS_CONTENT_MAX;
    memcpy(node->content, data, len);
    node->size = len;
}

size_t fake_fs_read_file(const char *path, void *buf, size_t cap)
{
    FakeNode *node = find(path);
    if (node == NULL || node->type != FS_ENTRY_FILE)
        return 0;
    size_t n = node->size < cap ? (size_t)node->size : cap;
    memcpy(buf, node->content, n);
    return n;
}

int fake_fs_exists(const char *path)
{
    return find(path) != NULL;
}

size_t fake_fs_node_count(void)
{
    size_t n = 0;
    for (int i = 0; i < FAKE_FS_NODES; ++i)
        n += s_nodes[i].used != 0;
    return n;
}

void fake_fs_set_free(const char *volume, uint64_t bytes, int known)
{
    for (int i = 0; i < FAKE_FS_VOLUMES; ++i) {
        if (s_volumes[i].known || s_volumes[i].volume[0] != '\0') {
            if (strcmp(s_volumes[i].volume, volume) == 0) {
                s_volumes[i].bytes = bytes;
                s_volumes[i].known = known;
                return;
            }
        }
    }
    for (int i = 0; i < FAKE_FS_VOLUMES; ++i) {
        if (s_volumes[i].volume[0] == '\0') {
            strncpy(s_volumes[i].volume, volume,
                    sizeof(s_volumes[i].volume) - 1);
            s_volumes[i].bytes = bytes;
            s_volumes[i].known = known;
            return;
        }
    }
}

void fake_fs_set_rename_replaces(int yes)
{
    s_rename_replaces = yes;
}

void fake_fs_set_mtime(const char *path, uint32_t mtime)
{
    FakeNode *node = find(path);
    if (node == NULL)
        return;
    node->mtime_known = 1;
    node->mtime = mtime;
}

void fake_fs_fail_op(FakeFsOp op, FsResult result)
{
    if (op >= 0 && op < FAKE_FS_OP_COUNT)
        s_fail[op] = result;
}

void fake_fs_fail_list_after(int entries)
{
    s_fail_list_after = entries;
}

int fake_fs_sync_count(void)
{
    return s_sync_count;
}
