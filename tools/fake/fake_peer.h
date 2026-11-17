/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_FAKE_PEER_H
#define VFTP_FAKE_PEER_H

#include <stdint.h>

#include "fake_net.h"

/*
 * Scripted FTP server for the client and app tests.
 *
 * One instance answers one control session. It owns no socket of its own: the
 * commands are read from the client's control socket through fake_net, and the
 * replies and data payloads are queued back on the same fake sockets. A test
 * drives it by alternating `app_tick` and `fake_peer_serve`, exactly like a
 * real peer that only gets to run between frames.
 *
 * All state lives in the instance, never in a file static, so two sessions can
 * run in one process. The sockets are touched only through fake_net, whose one
 * host mutex already covers the worker thread and a peer thread sharing the
 * singleton; a suite that runs the peer on its own thread therefore adds no
 * second lock and no shared global.
 *
 * The MLST and SIZE knobs model the upload preflight (D65, D91): the tests
 * script what the server answers for the destination instead of racing a real
 * filesystem.
 */

typedef struct FakePeer FakePeer;

/* The MLST answer the upload preflight gets. */
typedef enum {
    FAKE_PEER_MLST_NORMAL = 0,  /* 550 unless the path is `exists` */
    FAKE_PEER_MLST_UNSUPPORTED, /* 502; the client falls back to SIZE */
    FAKE_PEER_MLST_DIR,         /* every path answers type=dir */
    FAKE_PEER_MLST_FAIL         /* 421; the preflight fails outright */
} FakePeerMlst;

FakePeer *fake_peer_create(void);
void fake_peer_destroy(FakePeer *peer);

/* Attach to a control socket and send the 220 greeting. The session state
 * (CWD, the held data sockets, every counter) resets; the scripted knobs are
 * kept, so a test can script the peer before the client connects. */
void fake_peer_attach(FakePeer *peer, NetSocket *control);
/* Attach to the socket fake_net recorded last, for a client that just
 * connected. */
void fake_peer_open(FakePeer *peer);

/* The control socket the session is attached to, NULL before an attach. */
NetSocket *fake_peer_control(FakePeer *peer);

/* One non-blocking service pass: reap a completed STOR, then answer every
 * complete command on the control socket. */
void fake_peer_serve(FakePeer *peer);

/*
 * Scripted answers. Strings are borrowed, not copied: the caller keeps the
 * literal alive for the peer's lifetime. A NULL listing still answers 150/226
 * with no data, which is how a test scripts an empty directory.
 */
void fake_peer_set_listing(FakePeer *peer, const char *listing);
/* Served for the second and later listings when set; NULL serves `listing`
 * again. Models the post-STORE listing refresh the upload preflight asks
 * for. */
void fake_peer_set_refresh_listing(FakePeer *peer, const char *listing);
/* Served for a listing while the peer is not at "/" when set. */
void fake_peer_set_sub_listing(FakePeer *peer, const char *listing);
/* The bytes a RETR queues on the data socket. */
void fake_peer_set_payload(FakePeer *peer, const char *payload);

void fake_peer_set_mlst(FakePeer *peer, FakePeerMlst mode);
/* MLST answers with the given perm facts: `root_perm` at "/", `sub_perm`
 * elsewhere. Either may be NULL to keep the filesystem script (R18). */
void fake_peer_set_mlst_perm(FakePeer *peer, const char *root_perm,
                             const char *sub_perm);
/* MKD answers 550 instead of 257, for a write refusal that must not mark the
 * folder read-only (R18). */
void fake_peer_set_mkd_fail(FakePeer *peer, int fail);
/* The destination MLST and SIZE report as an existing file. */
void fake_peer_set_exists_path(FakePeer *peer, const char *path);
/* A second destination SIZE reports, for a batch of two names. */
void fake_peer_set_second_exists_path(FakePeer *peer, const char *path);
void fake_peer_set_exists_size(FakePeer *peer, uint64_t size);
/* 0 answers SIZE with 502, as a server without the extension does. */
void fake_peer_set_size_supported(FakePeer *peer, int supported);

/* Hold the RETR data open so the transfer stays in flight; the test then
 * cancels or lets the clock expire. */
void fake_peer_set_hold_data(FakePeer *peer, int hold);
/* As `hold_data`, but the payload is sent and only the 226 is withheld. */
void fake_peer_set_hold_final(FakePeer *peer, int hold);
/* Refuse MLSD and LIST with 550, for the read-error path. */
void fake_peer_set_list_fail(FakePeer *peer, int fail);

unsigned fake_peer_listing_count(const FakePeer *peer);
unsigned fake_peer_list_fail_count(const FakePeer *peer);
unsigned fake_peer_size_count(const FakePeer *peer);
unsigned fake_peer_retr_count(const FakePeer *peer);
unsigned fake_peer_abort_count(const FakePeer *peer);
unsigned fake_peer_stor_count(const FakePeer *peer);
unsigned fake_peer_cwd_count(const FakePeer *peer);
unsigned fake_peer_mkd_count(const FakePeer *peer);
unsigned fake_peer_dele_count(const FakePeer *peer);
unsigned fake_peer_rmd_count(const FakePeer *peer);
unsigned fake_peer_rename_count(const FakePeer *peer);

/* The path of the last STOR, or "" when none has arrived. */
const char *fake_peer_last_stor(const FakePeer *peer);
/* The path the last CWD named, "/" before any. */
const char *fake_peer_cwd(const FakePeer *peer);

#endif
