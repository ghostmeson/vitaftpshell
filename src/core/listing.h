/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_LISTING_H
#define VFTP_CORE_LISTING_H

#include <stddef.h>
#include <stdint.h>

/*
 * Directory-listing policy. MLSD, Unix LIST, and DOS LIST all become the same
 * entry; the pane never sees a format. `.` and `..` are skipped here, and the
 * caller sorts with listing_sort.
 */

#define LISTING_NAME_MAX 256

typedef enum {
    LISTING_FILE = 0,
    LISTING_DIR,
    LISTING_OTHER
} ListingType;

typedef struct {
    char name[LISTING_NAME_MAX];
    ListingType type;
    uint64_t size;
    int size_known;
    uint64_t mtime; /* YYYYMMDDHHMMSS, or 0 when absent */
    int mtime_known;
} ListingEntry;

typedef enum {
    LISTING_OK = 0,
    LISTING_SKIP,
    LISTING_MALFORMED
} ListingResult;

ListingResult listing_parse_mlsd(const char *line, ListingEntry *out);
ListingResult listing_parse_unix(const char *line, ListingEntry *out);
ListingResult listing_parse_dos(const char *line, ListingEntry *out);

/* Directories first, then name. Stable enough for the one visible list. */
int listing_compare(const void *a, const void *b);
void listing_sort(ListingEntry *entries, size_t count);

#endif
