/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/reserve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESERVE_MAX 4096
#define RESERVE_ARENA_MAX (256 * 1024)

typedef struct {
    uint8_t lane;
    uint8_t kind;
    uint32_t off;
} ReserveEntry;

struct Reserve {
    ReserveEntry entries[RESERVE_MAX];
    int count;
    char arena[RESERVE_ARENA_MAX];
    size_t used;
};

Reserve *reserve_create(void)
{
    return calloc(1, sizeof(Reserve));
}

void reserve_destroy(Reserve *reserve)
{
    free(reserve);
}

static int sidecar_matches(const char *held, const char *candidate)
{
    return path_is_sidecar_of(candidate, held) ||
           path_is_sidecar_of(held, candidate);
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
            path_ci_equal(reserve->arena + entry->off, text))
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

        int equal = path_ci_equal(held, text);
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
