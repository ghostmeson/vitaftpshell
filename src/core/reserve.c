/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/reserve.h"

#include <stdio.h>
#include <string.h>

static int lower_ascii(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int ci_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return 0;
    while (*a != '\0' && *b != '\0') {
        if (lower_ascii((unsigned char)*a) != lower_ascii((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int ci_prefix(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL)
        return 0;
    while (*prefix != '\0') {
        if (*text == '\0' ||
            lower_ascii((unsigned char)*text) !=
                lower_ascii((unsigned char)*prefix))
            return 0;
        text++;
        prefix++;
    }
    return 1;
}

/* a is a proper ancestor of b. A volume root ("ux0:") is an ancestor of its
 * contents without a separator. */
static int path_is_ancestor(const char *a, const char *b)
{
    if (a == NULL || b == NULL || a[0] == '\0' || b[0] == '\0')
        return 0;

    size_t len = strlen(a);
    while (len > 0 && a[len - 1] == '/')
        len--;
    if (len == 0)
        return 0;
    if (!ci_prefix(b, a))
        return 0;
    if (b[len] == '\0')
        return 0; /* equal, not an ancestor */
    if (a[len - 1] == ':')
        return 1;
    return b[len] == '/';
}

static int sidecar_matches(const char *held, const char *candidate)
{
    static const char *const suffixes[] = {".vftp.part", ".vftp.old"};
    char with_suffix[PATH_MAX_LEN];
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        int n = snprintf(with_suffix, sizeof(with_suffix), "%s%s", held,
                         suffixes[i]);
        if (n > 0 && (size_t)n < sizeof(with_suffix) &&
            ci_equal(candidate, with_suffix))
            return 1;
        n = snprintf(with_suffix, sizeof(with_suffix), "%s%s", candidate,
                     suffixes[i]);
        if (n > 0 && (size_t)n < sizeof(with_suffix) &&
            ci_equal(held, with_suffix))
            return 1;
    }
    return 0;
}

static int has_suffix(const char *text, const char *suffix)
{
    size_t n = strlen(text);
    size_t m = strlen(suffix);
    if (m > n)
        return 0;
    return ci_equal(text + (n - m), suffix);
}

int reserve_is_sidecar(const char *name_or_path)
{
    if (name_or_path == NULL)
        return 0;
    return has_suffix(name_or_path, ".vftp.part") ||
           has_suffix(name_or_path, ".vftp.old");
}

void reserve_reset(Reserve *reserve)
{
    if (reserve == NULL)
        return;
    reserve->count = 0;
    reserve->used = 0;
}

int reserve_add(Reserve *reserve, ReserveLane lane, ReserveKind kind,
                const char *path)
{
    if (reserve == NULL || path == NULL || path[0] == '\0')
        return 0;
    if (reserve->count >= RESERVE_MAX)
        return 0;

    Path canonical;
    const char *text = path;
    if (path_canonicalize(path, &canonical) == PATH_OK)
        text = canonical.text;

    size_t len = strlen(text) + 1;
    if (reserve->used + len > RESERVE_ARENA_MAX)
        return 0;

    for (int i = 0; i < reserve->count; ++i) {
        const ReserveEntry *entry = &reserve->entries[i];
        if (entry->lane == (uint8_t)lane &&
            entry->kind == (uint8_t)kind &&
            ci_equal(reserve->arena + entry->off, text))
            return 1; /* already held by this lane */
    }

    memcpy(reserve->arena + reserve->used, text, len);
    reserve->entries[reserve->count].lane = (uint8_t)lane;
    reserve->entries[reserve->count].kind = (uint8_t)kind;
    reserve->entries[reserve->count].off = (uint32_t)reserve->used;
    reserve->count++;
    reserve->used += len;
    return 1;
}

void reserve_release(Reserve *reserve, ReserveLane lane)
{
    if (reserve == NULL)
        return;

    /* Entries are a compact list; keep the arena as-is and drop the lane's
     * rows. The arena resets when the last lane releases or on reset. */
    int write = 0;
    for (int i = 0; i < reserve->count; ++i) {
        if (reserve->entries[i].lane == (uint8_t)lane)
            continue;
        reserve->entries[write++] = reserve->entries[i];
    }
    reserve->count = write;

    /* Reclaim the released strings. Entries keep insertion order, so each live
     * string moves toward the front; a long session cannot exhaust the arena. */
    size_t dst = 0;
    for (int i = 0; i < reserve->count; ++i) {
        ReserveEntry *entry = &reserve->entries[i];
        const char *src = reserve->arena + entry->off;
        size_t len = strlen(src) + 1;
        if ((size_t)entry->off != dst)
            memmove(reserve->arena + dst, src, len);
        entry->off = (uint32_t)dst;
        dst += len;
    }
    reserve->used = dst;
}

int reserve_conflict(const Reserve *reserve, ReserveKind kind, const char *path,
                     char *blocker, size_t cap)
{
    if (reserve == NULL || path == NULL)
        return 0;

    Path canonical;
    const char *text = path;
    if (path_canonicalize(path, &canonical) == PATH_OK)
        text = canonical.text;

    for (int i = 0; i < reserve->count; ++i) {
        const ReserveEntry *entry = &reserve->entries[i];
        const char *held = reserve->arena + entry->off;

        int equal = ci_equal(held, text);
        int sidecar = sidecar_matches(held, text);

        int conflict = 0;
        if (equal || sidecar) {
            if (kind == RESERVE_READ && entry->kind == RESERVE_READ)
                conflict = 0;
            else
                conflict = 1;
        } else if (kind == RESERVE_READ) {
            /* A server read of an ancestor is allowed; reading inside a held
             * tree is not. */
            if (entry->kind == RESERVE_TREE &&
                path_is_ancestor(held, text))
                conflict = 1;
        } else if (entry->kind == RESERVE_TREE || kind == RESERVE_TREE) {
            if (path_is_ancestor(held, text) || path_is_ancestor(text, held))
                conflict = 1;
        }

        if (conflict) {
            if (blocker != NULL && cap > 0)
                snprintf(blocker, cap, "%s", held);
            return 1;
        }
    }
    return 0;
}
