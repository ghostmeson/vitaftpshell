#include "core/store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/path.h"

#define SITES_HEADER "vitaFTPShell-sites 1"
#define SETTINGS_HEADER "vitaFTPShell-settings 1"
#define LINE_BUF 4096
#define READ_BUF 256

typedef struct {
    FsPort *fs;
    FsFile *file;
    char buf[READ_BUF];
    size_t len;
    size_t pos;
} StoreReader;

static int write_all(FsPort *fs, FsFile *file, const char *data, size_t len)
{
    size_t total = 0;
    while (total < len) {
        size_t written = 0;
        FsResult r = fs->write(fs->ctx, file, data + total, len - total,
                               &written);
        if (r != FS_OK)
            return r == FS_NO_SPACE ? STORE_NO_SPACE : STORE_IO;
        if (written == 0)
            return STORE_IO;
        total += written;
    }
    return STORE_OK;
}

static int reader_getc(StoreReader *reader, char *out)
{
    if (reader->pos == reader->len) {
        size_t got = 0;
        FsResult r = reader->fs->read(reader->fs->ctx, reader->file,
                                      reader->buf, sizeof(reader->buf), &got);
        if (r == FS_END)
            return 0;
        if (r != FS_OK || got == 0)
            return -1;
        reader->len = got;
        reader->pos = 0;
    }
    *out = reader->buf[reader->pos++];
    return 1;
}

/* 1 line, 0 EOF, -1 read error or an overlong line. */
static int reader_line(StoreReader *reader, char *out, size_t cap,
                       size_t *out_len)
{
    size_t used = 0;
    int overlong = 0;
    int got_any = 0;

    for (;;) {
        char c = 0;
        int r = reader_getc(reader, &c);
        if (r < 0)
            return -1;
        if (r == 0) {
            if (!got_any)
                return 0;
            break;
        }

        got_any = 1;
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        if (used + 1 >= cap) {
            overlong = 1;
            continue;
        }
        out[used++] = c;
    }

    out[used] = '\0';
    if (out_len != NULL)
        *out_len = used;
    return overlong ? -1 : 1;
}

static int safe_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
           c == '~';
}

static int encode_field(char *out, size_t cap, const char *value)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t used = 0;

    for (const unsigned char *p = (const unsigned char *)value; *p != '\0';
         ++p) {
        if (safe_char((char)*p)) {
            if (used + 1 >= cap)
                return 0;
            out[used++] = (char)*p;
        } else {
            if (used + 3 >= cap)
                return 0;
            out[used++] = '%';
            out[used++] = HEX[*p >> 4];
            out[used++] = HEX[*p & 0x0F];
        }
    }

    out[used] = '\0';
    return 1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int decode_field(char *out, size_t cap, const char *value)
{
    size_t used = 0;
    while (*value != '\0') {
        if (*value == '%') {
            if (value[1] == '\0' || value[2] == '\0')
                return 0;
            int hi = hex_value(value[1]);
            int lo = hex_value(value[2]);
            if (hi < 0 || lo < 0)
                return 0;
            if (used + 1 >= cap)
                return 0;
            out[used++] = (char)((hi << 4) | lo);
            value += 3;
        } else {
            if (used + 1 >= cap)
                return 0;
            out[used++] = *value++;
        }
    }
    out[used] = '\0';
    return 1;
}

static void build_path(char *out, size_t cap, const char *dir,
                       const char *name)
{
    snprintf(out, cap, "%s/%s", dir, name);
}

/*
 * An interrupted fallback can leave the only complete copy under `.old`. Before
 * a load or save touches the target, restore that copy when the target is
 * missing; when both exist, the target is at least as new and the aside is
 * stale. Without this, one orphan wedges every later save.
 */
static void recover_file(FsPort *fs, const char *target, const char *aside)
{
    FsEntry entry;
    int target_exists = fs->stat(fs->ctx, target, &entry) == FS_OK;
    int aside_exists = fs->stat(fs->ctx, aside, &entry) == FS_OK;

    if (!target_exists && aside_exists)
        fs->rename(fs->ctx, aside, target);
    else if (target_exists && aside_exists)
        fs->remove(fs->ctx, aside);
}

static StoreResult replace_file(FsPort *fs, const char *temp,
                                const char *target)
{
    char aside[PATH_MAX_LEN];
    snprintf(aside, sizeof(aside), "%s.old", target);
    recover_file(fs, target, aside);

    if (fs->rename_replaces(fs->ctx)) {
        if (fs->rename(fs->ctx, temp, target) == FS_OK)
            return STORE_OK;
        fs->remove(fs->ctx, temp);
        return STORE_IO;
    }

    FsEntry entry;
    int target_exists = fs->stat(fs->ctx, target, &entry) == FS_OK;
    if (!target_exists) {
        if (fs->rename(fs->ctx, temp, target) == FS_OK)
            return STORE_OK;
        fs->remove(fs->ctx, temp);
        return STORE_IO;
    }

    if (fs->rename(fs->ctx, target, aside) != FS_OK) {
        fs->remove(fs->ctx, temp);
        return STORE_IO;
    }
    if (fs->rename(fs->ctx, temp, target) != FS_OK) {
        fs->rename(fs->ctx, aside, target);
        fs->remove(fs->ctx, temp);
        return STORE_IO;
    }
    fs->remove(fs->ctx, aside);
    return STORE_OK;
}

StoreResult store_load_sites(FsPort *fs, const char *dir, SiteBook *book)
{
    if (fs == NULL || dir == NULL || book == NULL)
        return STORE_IO;

    char path[PATH_MAX_LEN];
    char aside[PATH_MAX_LEN];
    build_path(path, sizeof(path), dir, STORE_SITES_NAME);
    snprintf(aside, sizeof(aside), "%s.old", path);
    recover_file(fs, path, aside);

    FsFile *file = NULL;
    FsResult r = fs->open(fs->ctx, path, FS_OPEN_READ, &file);
    if (r == FS_NOT_FOUND)
        return STORE_NOT_FOUND;
    if (r != FS_OK)
        return STORE_IO;

    StoreReader reader = {fs, file, {0}, 0, 0};
    char line[LINE_BUF];
    size_t len = 0;
    int got = reader_line(&reader, line, sizeof(line), &len);
    if (got <= 0 || strcmp(line, SITES_HEADER) != 0) {
        fs->close(fs->ctx, file);
        site_book_clear(book);
        book->parsed = 0;
        return STORE_BAD_VERSION;
    }

    site_book_clear(book);
    book->parsed = 1;
    int partial = 0;

    for (;;) {
        got = reader_line(&reader, line, sizeof(line), &len);
        if (got == 0)
            break;
        if (got < 0) {
            partial = 1;
            break;
        }
        if (len == 0)
            continue;

        Site site;
        memset(&site, 0, sizeof(site));
        char work[LINE_BUF];
        strncpy(work, line, sizeof(work) - 1);
        work[sizeof(work) - 1] = '\0';

        int have_name = 0;
        int have_host = 0;
        int have_port = 0;
        int have_anon = 0;
        int bad = 0;

        char *field = work;
        while (field != NULL && *field != '\0') {
            char *tab = strchr(field, '\t');
            if (tab != NULL)
                *tab = '\0';
            char *eq = strchr(field, '=');
            if (eq != NULL) {
                *eq = '\0';
                const char *key = field;
                const char *value = eq + 1;
                if (strcmp(key, "name") == 0) {
                    if (!decode_field(site.name, sizeof(site.name), value))
                        bad = 1;
                    else
                        have_name = 1;
                } else if (strcmp(key, "host") == 0) {
                    if (!decode_field(site.host, sizeof(site.host), value))
                        bad = 1;
                    else
                        have_host = 1;
                } else if (strcmp(key, "user") == 0) {
                    if (!decode_field(site.username, sizeof(site.username),
                                      value))
                        bad = 1;
                } else if (strcmp(key, "pass") == 0) {
                    if (!decode_field(site.password, sizeof(site.password),
                                      value))
                        bad = 1;
                } else if (strcmp(key, "port") == 0) {
                    char *end = NULL;
                    long port = strtol(value, &end, 10);
                    if (end == value || *end != '\0' || port < 1 ||
                        port > 65535)
                        bad = 1;
                    else {
                        site.port = (uint16_t)port;
                        have_port = 1;
                    }
                } else if (strcmp(key, "anon") == 0) {
                    if (strcmp(value, "0") == 0) {
                        site.anonymous = 0;
                        have_anon = 1;
                    } else if (strcmp(value, "1") == 0) {
                        site.anonymous = 1;
                        have_anon = 1;
                    } else {
                        bad = 1;
                    }
                }
            }
            field = tab != NULL ? tab + 1 : NULL;
        }

        if (bad || !have_name || !have_host || !have_port || !have_anon ||
            site_book_add(book, &site) < 0)
            partial = 1;
    }

    fs->close(fs->ctx, file);
    book->partial = partial;
    return partial ? STORE_PARTIAL : STORE_OK;
}

StoreResult store_save_sites(FsPort *fs, const char *dir,
                             const SiteBook *book)
{
    if (fs == NULL || dir == NULL || book == NULL)
        return STORE_IO;

    char target[PATH_MAX_LEN];
    char temp[PATH_MAX_LEN];
    build_path(target, sizeof(target), dir, STORE_SITES_NAME);
    snprintf(temp, sizeof(temp), "%s.tmp", target);

    fs->remove(fs->ctx, temp);

    FsFile *file = NULL;
    FsResult r = fs->open(fs->ctx, temp, FS_OPEN_WRITE_TRUNC, &file);
    if (r != FS_OK)
        return r == FS_NO_SPACE ? STORE_NO_SPACE : STORE_IO;

    StoreResult result = write_all(fs, file, SITES_HEADER "\n",
                                   strlen(SITES_HEADER) + 1);
    if (result != STORE_OK)
        goto fail;

    for (unsigned i = 0; i < book->count; ++i) {
        const Site *site = &book->items[i];
        char name[SITE_NAME_MAX * 3 + 1];
        char host[SITE_HOST_MAX * 3 + 1];
        char user[SITE_USER_MAX * 3 + 1];
        char pass[SITE_PASS_MAX * 3 + 1];

        if (!encode_field(name, sizeof(name), site->name) ||
            !encode_field(host, sizeof(host), site->host) ||
            !encode_field(user, sizeof(user), site->username) ||
            !encode_field(pass, sizeof(pass), site->password)) {
            result = STORE_IO;
            goto fail;
        }

        char line[LINE_BUF];
        int n = snprintf(line, sizeof(line),
                         "name=%s\thost=%s\tport=%u\tanon=%d\tuser=%s\tpass=%s\n",
                         name, host, (unsigned)site->port, site->anonymous ? 1 : 0,
                         user, pass);
        if (n < 0 || (size_t)n >= sizeof(line)) {
            result = STORE_IO;
            goto fail;
        }
        result = write_all(fs, file, line, (size_t)n);
        if (result != STORE_OK)
            goto fail;
    }

    if (fs->sync(fs->ctx, file) != FS_OK) {
        result = STORE_IO;
        goto fail;
    }
    if (fs->close(fs->ctx, file) != FS_OK) {
        file = NULL;
        fs->remove(fs->ctx, temp);
        return STORE_IO;
    }
    file = NULL;

    return replace_file(fs, temp, target);

fail:
    if (file != NULL)
        fs->close(fs->ctx, file);
    fs->remove(fs->ctx, temp);
    return result;
}

StoreResult store_load_settings(FsPort *fs, const char *dir,
                                Settings *settings)
{
    if (fs == NULL || dir == NULL || settings == NULL)
        return STORE_IO;

    /* Define the output even when the file is absent or corrupt. */
    settings_init(settings);

    char path[PATH_MAX_LEN];
    char aside[PATH_MAX_LEN];
    build_path(path, sizeof(path), dir, STORE_SETTINGS_NAME);
    snprintf(aside, sizeof(aside), "%s.old", path);
    recover_file(fs, path, aside);

    FsFile *file = NULL;
    FsResult r = fs->open(fs->ctx, path, FS_OPEN_READ, &file);
    if (r == FS_NOT_FOUND)
        return STORE_NOT_FOUND;
    if (r != FS_OK)
        return STORE_IO;

    StoreReader reader = {fs, file, {0}, 0, 0};
    char line[LINE_BUF];
    size_t len = 0;
    int got = reader_line(&reader, line, sizeof(line), &len);
    if (got <= 0 || strcmp(line, SETTINGS_HEADER) != 0) {
        fs->close(fs->ctx, file);
        return STORE_BAD_VERSION;
    }

    int partial = 0;

    for (;;) {
        got = reader_line(&reader, line, sizeof(line), &len);
        if (got == 0)
            break;
        if (got < 0) {
            partial = 1;
            break;
        }
        if (len == 0)
            continue;

        char *eq = strchr(line, '=');
        if (eq == NULL)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        if (strcmp(key, "last_local") == 0) {
            char decoded[SETTINGS_LAST_LOCAL_MAX];
            if (decode_field(decoded, sizeof(decoded), value))
                strncpy(settings->last_local, decoded,
                        sizeof(settings->last_local) - 1);
        } else if (strcmp(key, "last_site") == 0) {
            char decoded[SETTINGS_LAST_SITE_MAX];
            if (decode_field(decoded, sizeof(decoded), value))
                strncpy(settings->last_site, decoded,
                        sizeof(settings->last_site) - 1);
        } else if (strcmp(key, "server_port") == 0) {
            char *end = NULL;
            long port = strtol(value, &end, 10);
            if (end != value && *end == '\0' && port >= 1 && port <= 65535)
                settings->server_port = (uint16_t)port;
        }
    }

    fs->close(fs->ctx, file);
    return partial ? STORE_PARTIAL : STORE_OK;
}

StoreResult store_save_settings(FsPort *fs, const char *dir,
                                const Settings *settings)
{
    if (fs == NULL || dir == NULL || settings == NULL)
        return STORE_IO;

    char target[PATH_MAX_LEN];
    char temp[PATH_MAX_LEN];
    build_path(target, sizeof(target), dir, STORE_SETTINGS_NAME);
    snprintf(temp, sizeof(temp), "%s.tmp", target);

    fs->remove(fs->ctx, temp);

    FsFile *file = NULL;
    FsResult r = fs->open(fs->ctx, temp, FS_OPEN_WRITE_TRUNC, &file);
    if (r != FS_OK)
        return r == FS_NO_SPACE ? STORE_NO_SPACE : STORE_IO;

    StoreResult result = write_all(fs, file, SETTINGS_HEADER "\n",
                                   strlen(SETTINGS_HEADER) + 1);
    if (result != STORE_OK)
        goto fail;

    char local[SETTINGS_LAST_LOCAL_MAX * 3 + 1];
    char site[SETTINGS_LAST_SITE_MAX * 3 + 1];
    if (!encode_field(local, sizeof(local), settings->last_local) ||
        !encode_field(site, sizeof(site), settings->last_site)) {
        result = STORE_IO;
        goto fail;
    }

    char line[LINE_BUF];
    int n = snprintf(line, sizeof(line),
                     "last_local=%s\nlast_site=%s\nserver_port=%u\n", local,
                     site, (unsigned)settings->server_port);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        result = STORE_IO;
        goto fail;
    }
    result = write_all(fs, file, line, (size_t)n);
    if (result != STORE_OK)
        goto fail;

    if (fs->sync(fs->ctx, file) != FS_OK) {
        result = STORE_IO;
        goto fail;
    }
    if (fs->close(fs->ctx, file) != FS_OK) {
        file = NULL;
        fs->remove(fs->ctx, temp);
        return STORE_IO;
    }
    file = NULL;

    return replace_file(fs, temp, target);

fail:
    if (file != NULL)
        fs->close(fs->ctx, file);
    fs->remove(fs->ctx, temp);
    return result;
}
