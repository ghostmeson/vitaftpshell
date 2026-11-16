/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_RESERVE_H
#define VFTP_CORE_RESERVE_H

#include <stddef.h>
#include <stdint.h>

#include "core/path.h"

/*
 * Canonical-path reservations between the local job, the network lane, and the
 * FTP server (U26, KTD21, D72). A reservation covers a path and its templed
 * write siblings. Equal paths conflict unless both sides read; a tree
 * operation (delete, rename, recursive move) conflicts with an ancestor or
 * descendant. A server read of an ancestor is allowed so a PC can list a folder
 * while a file inside it transfers; the exact held path and its sidecars are
 * refused. The containing directory is not locked (D72 amendment).
 */

typedef enum {
    RESERVE_LANE_LOCAL = 0,
    RESERVE_LANE_NETWORK,
    RESERVE_LANE_SERVER,
    RESERVE_LANE_COUNT
} ReserveLane;

typedef enum {
    RESERVE_READ = 0,
    RESERVE_WRITE,
    RESERVE_TREE
} ReserveKind;

/* Opaque: the table and arena are an implementation detail. One allocation at
 * start, one free at shutdown (about 288 KiB). */
typedef struct Reserve Reserve;

Reserve *reserve_create(void);
void reserve_destroy(Reserve *reserve);
void reserve_reset(Reserve *reserve);

/* Canonicalize and store. 1 on success; 0 when the table or arena is full. */
int reserve_add(Reserve *reserve, ReserveLane lane, ReserveKind kind,
                const char *path);

/* Release every reservation owned by one lane. */
void reserve_release(Reserve *reserve, ReserveLane lane);

/*
 * 1 when any lane holds a reservation that conflicts with `kind` on `path`,
 * including the caller's own lane; the caller must not check a path it already
 * holds. Writes the blocking path into `blocker` when it is non-NULL and large
 * enough.
 */
int reserve_conflict(const Reserve *reserve, ReserveKind kind, const char *path,
                     char *blocker, size_t cap);


#endif
