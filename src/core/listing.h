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

/* MLSD `perm` facts, reduced to the four that decide whether a folder is
 * writable (R18). A server that reports no perm fact leaves `perm_known` 0,
 * and an unknown folder stays enabled. */
#define LISTING_PERM_WRITE 0x01u  /* w: store files */
#define LISTING_PERM_CREATE 0x02u /* c: create files */
#define LISTING_PERM_DELETE 0x04u /* d: delete files */
#define LISTING_PERM_MKDIR 0x08u  /* m: create directories */
#define LISTING_PERM_WRITES                                                \
    (LISTING_PERM_WRITE | LISTING_PERM_CREATE | LISTING_PERM_DELETE |      \
     LISTING_PERM_MKDIR)

typedef struct {
    char name[LISTING_NAME_MAX];
    ListingType type;
    uint64_t size;
    int size_known;
    uint64_t mtime; /* YYYYMMDDHHMMSS, or 0 when absent */
    int mtime_known;
    uint8_t perm_mask; /* LISTING_PERM_* bits from `perm` */
    int perm_known;    /* a `perm` fact was present */
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
