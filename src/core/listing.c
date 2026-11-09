/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/listing.h"

#include <stdlib.h>
#include <string.h>

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int set_name(ListingEntry *out, const char *name, size_t len)
{
    if (len >= sizeof(out->name))
        return 0;
    memcpy(out->name, name, len);
    out->name[len] = '\0';
    return 1;
}

static int parse_u64_span(const char *text, size_t len, uint64_t *value)
{
    if (text == NULL || len == 0)
        return 0;

    uint64_t v = 0;
    for (size_t i = 0; i < len; ++i) {
        if (!is_digit(text[i]))
            return 0;
        uint64_t digit = (uint64_t)(text[i] - '0');
        if (v > (UINT64_MAX - digit) / 10)
            return 0;
        v = v * 10 + digit;
    }
    *value = v;
    return 1;
}

static int name_is_dot(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int ci_equal(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'a' && ca <= 'z')
            ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = (char)(cb - 'a' + 'A');
        if (ca != cb)
            return 0;
        if (ca == '\0')
            return 1;
    }
    return 1;
}

ListingResult listing_parse_mlsd(const char *line, ListingEntry *out)
{
    if (line == NULL || out == NULL)
        return LISTING_MALFORMED;

    memset(out, 0, sizeof(*out));

    const char *space = strchr(line, ' ');
    if (space == NULL || space == line)
        return LISTING_MALFORMED;

    const char *facts_end = space;
    const char *p = line;
    int have_type = 0;
    int skip = 0;

    while (p < facts_end) {
        const char *semi = memchr(p, ';', (size_t)(facts_end - p));
        if (semi == NULL)
            semi = facts_end;

        const char *eq = memchr(p, '=', (size_t)(semi - p));
        if (eq != NULL) {
            size_t key_len = (size_t)(eq - p);
            const char *value = eq + 1;
            size_t value_len = (size_t)(semi - value);

            if (key_len == 4 && ci_equal(p, "type", 4)) {
                if (value_len == 3 && ci_equal(value, "dir", 3)) {
                    out->type = LISTING_DIR;
                    have_type = 1;
                } else if (value_len == 4 && ci_equal(value, "file", 4)) {
                    out->type = LISTING_FILE;
                    have_type = 1;
                } else if (value_len == 4 && ci_equal(value, "cdir", 4)) {
                    skip = 1;
                } else if (value_len == 4 && ci_equal(value, "pdir", 4)) {
                    skip = 1;
                } else if (value_len > 0) {
                    out->type = LISTING_OTHER;
                    have_type = 1;
                }
            } else if (key_len == 4 && ci_equal(p, "size", 4)) {
                uint64_t parsed = 0;
                if (parse_u64_span(value, value_len, &parsed)) {
                    out->size = parsed;
                    out->size_known = 1;
                }
            } else if (key_len == 6 && ci_equal(p, "modify", 6)) {
                uint64_t stamp = 0;
                int digits = 0;
                for (size_t i = 0; i < value_len; ++i) {
                    if (!is_digit(value[i]))
                        break;
                    stamp = stamp * 10 + (uint64_t)(value[i] - '0');
                    digits++;
                }
                if (digits == 14) {
                    out->mtime = stamp;
                    out->mtime_known = 1;
                }
            }
        }

        if (semi == facts_end)
            break;
        p = semi + 1;
    }

    if (skip)
        return LISTING_SKIP;
    if (!have_type)
        return LISTING_MALFORMED;

    const char *name = space + 1;
    while (*name == ' ')
        name++;
    if (name_is_dot(name))
        return LISTING_SKIP;

    if (!set_name(out, name, strlen(name)) || out->name[0] == '\0')
        return LISTING_MALFORMED;
    return LISTING_OK;
}

static const char *skip_token(const char *p)
{
    while (*p != '\0' && *p != ' ')
        p++;
    while (*p == ' ')
        p++;
    return p;
}

ListingResult listing_parse_unix(const char *line, ListingEntry *out)
{
    if (line == NULL || out == NULL)
        return LISTING_MALFORMED;
    if (strncmp(line, "total ", 6) == 0)
        return LISTING_SKIP;

    memset(out, 0, sizeof(*out));

    char kind = line[0];
    if (kind == 'd')
        out->type = LISTING_DIR;
    else if (kind == '-')
        out->type = LISTING_FILE;
    else if (kind == 'l')
        out->type = LISTING_OTHER;
    else
        return LISTING_MALFORMED;

    const char *p = line;
    if (*p == '\0' || *p == ' ')
        return LISTING_MALFORMED;
    p = skip_token(p); /* permissions */

    p = skip_token(p); /* links */
    p = skip_token(p); /* owner */
    p = skip_token(p); /* group */

    const char *size_start = p;
    while (is_digit(*p))
        p++;
    size_t size_len = (size_t)(p - size_start);
    uint64_t size = 0;
    if (!parse_u64_span(size_start, size_len, &size))
        return LISTING_MALFORMED;
    out->size = size;
    out->size_known = 1;
    p = size_start;

    p = skip_token(p); /* size */
    p = skip_token(p); /* month */
    p = skip_token(p); /* day */
    p = skip_token(p); /* time or year */

    if (*p == '\0')
        return LISTING_MALFORMED;

    const char *name = p;
    const char *arrow = strstr(name, " -> ");
    size_t name_len = arrow != NULL ? (size_t)(arrow - name) : strlen(name);

    char tmp[LISTING_NAME_MAX];
    if (name_len >= sizeof(tmp))
        return LISTING_MALFORMED;
    memcpy(tmp, name, name_len);
    tmp[name_len] = '\0';

    if (name_is_dot(tmp))
        return LISTING_SKIP;

    if (!set_name(out, tmp, name_len))
        return LISTING_MALFORMED;
    return LISTING_OK;
}

ListingResult listing_parse_dos(const char *line, ListingEntry *out)
{
    if (line == NULL || out == NULL)
        return LISTING_MALFORMED;

    memset(out, 0, sizeof(*out));

    const char *p = line;
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return LISTING_MALFORMED;

    p = skip_token(p); /* date */
    p = skip_token(p); /* time */
    if (*p == '\0')
        return LISTING_MALFORMED;

    const char *size_token = p;
    while (*p != '\0' && *p != ' ')
        p++;

    size_t token_len = (size_t)(p - size_token);
    if (token_len == 5 && ci_equal(size_token, "<DIR>", 5)) {
        out->type = LISTING_DIR;
        out->size_known = 0;
    } else {
        uint64_t size = 0;
        if (!parse_u64_span(size_token, token_len, &size))
            return LISTING_MALFORMED;
        out->type = LISTING_FILE;
        out->size = size;
        out->size_known = 1;
    }

    while (*p == ' ')
        p++;
    if (*p == '\0' || name_is_dot(p))
        return LISTING_SKIP;

    if (!set_name(out, p, strlen(p)))
        return LISTING_MALFORMED;
    return LISTING_OK;
}

int listing_compare(const void *a, const void *b)
{
    const ListingEntry *ea = a;
    const ListingEntry *eb = b;

    int a_dir = ea->type == LISTING_DIR;
    int b_dir = eb->type == LISTING_DIR;
    if (a_dir != b_dir)
        return a_dir ? -1 : 1;

    return strcmp(ea->name, eb->name);
}

void listing_sort(ListingEntry *entries, size_t count)
{
    if (entries == NULL || count < 2)
        return;
    qsort(entries, count, sizeof(entries[0]), listing_compare);
}
