/* SPDX-License-Identifier: GPL-3.0-only */

#include "fake_peer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_ftpd.h"

/* The command and path buffers keep the sizes the two client suites used
 * before the responder was shared, so a longer path truncates as it always
 * did rather than changing what a test sees. */
#define PEER_LINE_MAX 512
#define PEER_CWD_MAX 64
#define PEER_PATH_MAX 256
#define PEER_REPLY_MAX 640
/* Per-path listing scripts. A U2 walk lists a handful of folders; the bound
 * keeps the instance fixed-size and a test that scripts more is a bug. */
#define PEER_PATH_SCRIPTS 8

typedef struct {
    char path[PEER_PATH_MAX];
    const char *listing; /* borrowed; NULL is an empty folder */
    int refused;
    int reset; /* the data channel dies instead of serving this listing */
} PeerPathScript;

struct FakePeer {
    FakeFtpd server;
    NetSocket *control;
    NetSocket *data;
    NetSocket *stor_data;

    /* Scripted answers. Borrowed pointers; NULL means "answer nothing" for a
     * listing or a payload and "keep the default" for the rest. */
    const char *listing;
    const char *refresh_listing;
    const char *sub_listing;
    const char *payload;
    const char *exists_path;
    const char *exists_path2;
    uint64_t exists_size;
    FakePeerMlst mlst;
    /* The `perm` facts MLST reports at "/" and elsewhere (R18). NULL answers
     * the filesystem script instead. */
    const char *root_perm;
    const char *sub_perm;
    int size_supported;
    int mkd_fail;
    int hold_data;
    int hold_final;
    int list_fail;

    /* U2: per-path listings, keyed by the path the MLSD/LIST named. */
    PeerPathScript scripts[PEER_PATH_SCRIPTS];
    int script_count;
    char last_list_path[PEER_PATH_MAX];

    char cwd[PEER_CWD_MAX];
    char last_stor[PEER_PATH_MAX];

    unsigned listings;
    unsigned list_fails;
    unsigned sizes;
    unsigned retrs;
    unsigned aborts;
    unsigned stors;
    unsigned cwds;
    unsigned mkds;
    unsigned deles;
    unsigned rmds;
    unsigned renames;
};

FakePeer *fake_peer_create(void)
{
    FakePeer *peer = calloc(1, sizeof(*peer));
    if (peer == NULL)
        return NULL;
    snprintf(peer->cwd, sizeof(peer->cwd), "/");
    return peer;
}

void fake_peer_destroy(FakePeer *peer)
{
    free(peer);
}

void fake_peer_attach(FakePeer *peer, NetSocket *control)
{
    if (peer == NULL)
        return;

    peer->control = control;
    peer->data = NULL;
    peer->stor_data = NULL;
    peer->last_stor[0] = '\0';
    snprintf(peer->cwd, sizeof(peer->cwd), "/");
    snprintf(peer->last_list_path, sizeof(peer->last_list_path), "/");

    peer->listings = 0;
    peer->list_fails = 0;
    peer->sizes = 0;
    peer->retrs = 0;
    peer->aborts = 0;
    peer->stors = 0;
    peer->cwds = 0;
    peer->mkds = 0;
    peer->deles = 0;
    peer->rmds = 0;
    peer->renames = 0;

    fake_ftpd_init(&peer->server, control);
    fake_ftpd_reply(&peer->server, 220, "Welcome.");
}

void fake_peer_open(FakePeer *peer)
{
    fake_peer_attach(peer, fake_net_last_connected());
}

NetSocket *fake_peer_control(FakePeer *peer)
{
    return peer != NULL ? peer->control : NULL;
}

void fake_peer_set_listing(FakePeer *peer, const char *listing)
{
    if (peer != NULL)
        peer->listing = listing;
}

void fake_peer_set_refresh_listing(FakePeer *peer, const char *listing)
{
    if (peer != NULL)
        peer->refresh_listing = listing;
}

void fake_peer_set_sub_listing(FakePeer *peer, const char *listing)
{
    if (peer != NULL)
        peer->sub_listing = listing;
}

/* The script for one path, or NULL when the path falls back to the
 * single-listing script. */
static PeerPathScript *peer_script(FakePeer *peer, const char *path)
{
    for (int i = 0; i < peer->script_count; ++i) {
        if (strcmp(peer->scripts[i].path, path) == 0)
            return &peer->scripts[i];
    }
    return NULL;
}

/* One entry per path: a second call for the same path replaces the first, so a
 * test can restate a folder between two walks without running out of slots. */
static PeerPathScript *peer_script_slot(FakePeer *peer, const char *path)
{
    PeerPathScript *script = peer_script(peer, path);
    if (script != NULL)
        return script;
    if (peer->script_count >= PEER_PATH_SCRIPTS)
        return NULL;
    script = &peer->scripts[peer->script_count++];
    memset(script, 0, sizeof(*script));
    snprintf(script->path, sizeof(script->path), "%s", path);
    return script;
}

void fake_peer_set_path_listing(FakePeer *peer, const char *path,
                                const char *listing)
{
    if (peer == NULL || path == NULL)
        return;
    PeerPathScript *script = peer_script_slot(peer, path);
    if (script == NULL)
        return;
    script->refused = 0;
    script->listing = listing;
}

void fake_peer_set_path_refused(FakePeer *peer, const char *path, int refused)
{
    if (peer == NULL || path == NULL)
        return;
    PeerPathScript *script = peer_script_slot(peer, path);
    if (script != NULL)
        script->refused = refused ? 1 : 0;
}

void fake_peer_set_path_reset(FakePeer *peer, const char *path, int reset)
{
    if (peer == NULL || path == NULL)
        return;
    PeerPathScript *script = peer_script_slot(peer, path);
    if (script != NULL)
        script->reset = reset ? 1 : 0;
}

void fake_peer_set_payload(FakePeer *peer, const char *payload)
{
    if (peer != NULL)
        peer->payload = payload;
}

void fake_peer_set_mlst(FakePeer *peer, FakePeerMlst mode)
{
    if (peer != NULL)
        peer->mlst = mode;
}

/* MLST answers with the given perm facts: `root_perm` at "/", `sub_perm`
 * elsewhere. Either may be NULL to keep the filesystem script (R18). */
void fake_peer_set_mlst_perm(FakePeer *peer, const char *root_perm,
                             const char *sub_perm)
{
    if (peer != NULL) {
        peer->root_perm = root_perm;
        peer->sub_perm = sub_perm;
    }
}

/* MKD answers 550 instead of 257, for a write refusal that must not mark the
 * folder read-only (R18). */
void fake_peer_set_mkd_fail(FakePeer *peer, int fail)
{
    if (peer != NULL)
        peer->mkd_fail = fail;
}

void fake_peer_set_exists_path(FakePeer *peer, const char *path)
{
    if (peer != NULL)
        peer->exists_path = path;
}

void fake_peer_set_second_exists_path(FakePeer *peer, const char *path)
{
    if (peer != NULL)
        peer->exists_path2 = path;
}

void fake_peer_set_exists_size(FakePeer *peer, uint64_t size)
{
    if (peer != NULL)
        peer->exists_size = size;
}

void fake_peer_set_size_supported(FakePeer *peer, int supported)
{
    if (peer != NULL)
        peer->size_supported = supported;
}

void fake_peer_set_hold_data(FakePeer *peer, int hold)
{
    if (peer != NULL)
        peer->hold_data = hold;
}

void fake_peer_set_hold_final(FakePeer *peer, int hold)
{
    if (peer != NULL)
        peer->hold_final = hold;
}

void fake_peer_set_list_fail(FakePeer *peer, int fail)
{
    if (peer != NULL)
        peer->list_fail = fail;
}

unsigned fake_peer_listing_count(const FakePeer *peer)
{
    return peer != NULL ? peer->listings : 0;
}

unsigned fake_peer_list_fail_count(const FakePeer *peer)
{
    return peer != NULL ? peer->list_fails : 0;
}

unsigned fake_peer_size_count(const FakePeer *peer)
{
    return peer != NULL ? peer->sizes : 0;
}

unsigned fake_peer_retr_count(const FakePeer *peer)
{
    return peer != NULL ? peer->retrs : 0;
}

unsigned fake_peer_abort_count(const FakePeer *peer)
{
    return peer != NULL ? peer->aborts : 0;
}

unsigned fake_peer_stor_count(const FakePeer *peer)
{
    return peer != NULL ? peer->stors : 0;
}

unsigned fake_peer_cwd_count(const FakePeer *peer)
{
    return peer != NULL ? peer->cwds : 0;
}

unsigned fake_peer_mkd_count(const FakePeer *peer)
{
    return peer != NULL ? peer->mkds : 0;
}

unsigned fake_peer_dele_count(const FakePeer *peer)
{
    return peer != NULL ? peer->deles : 0;
}

unsigned fake_peer_rmd_count(const FakePeer *peer)
{
    return peer != NULL ? peer->rmds : 0;
}

unsigned fake_peer_rename_count(const FakePeer *peer)
{
    return peer != NULL ? peer->renames : 0;
}

const char *fake_peer_last_stor(const FakePeer *peer)
{
    return peer != NULL ? peer->last_stor : "";
}

const char *fake_peer_cwd(const FakePeer *peer)
{
    return peer != NULL ? peer->cwd : "";
}

const char *fake_peer_last_list_path(const FakePeer *peer)
{
    return peer != NULL ? peer->last_list_path : "";
}

/* The socket the client opened for the transfer, or NULL when the last connect
 * was the control session itself. */
static NetSocket *peer_data_socket(FakePeer *peer)
{
    NetSocket *socket = fake_net_last_connected();
    return socket == peer->control ? NULL : socket;
}

/* A STOR is complete when the client closes the data socket it opened; the
 * reply goes out on the control connection after the bytes are drained. The
 * drain must happen first: the app writes the file on the worker thread, so a
 * 226 before the close would report a transfer the client is still sending. */
static void peer_reap_stor(FakePeer *peer)
{
    if (peer->stor_data == NULL)
        return;

    char sink[PEER_LINE_MAX];
    while (fake_net_read_tx(peer->stor_data, sink, sizeof(sink)) > 0) {
    }
    if (!fake_net_is_open(peer->stor_data)) {
        fake_ftpd_reply(&peer->server, 226, "Transfer complete.");
        peer->stor_data = NULL;
    }
}

static void peer_send_listing(FakePeer *peer, const char *path)
{
    PeerPathScript *script = peer_script(peer, path);
    if (script != NULL) {
        if (script->refused) {
            peer->list_fails++;
            fake_ftpd_reply(&peer->server, 550, "Listing refused.");
            return;
        }
        peer->data = peer_data_socket(peer);
        fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
        if (script->reset) {
            /* The data channel dies after the 150 and no 226 follows; the
             * client should read this as a failed transfer of one folder, not
             * as the end of the session. */
            if (peer->data != NULL)
                fake_net_reset_peer(peer->data);
            peer->data = NULL;
            return;
        }
        if (script->listing != NULL && peer->data != NULL) {
            fake_net_queue_rx(peer->data, script->listing,
                              strlen(script->listing));
            fake_net_shutdown(peer->data);
        }
        fake_ftpd_reply(&peer->server, 226, "Transfer complete.");
        peer->listings++;
        peer->data = NULL;
        return;
    }

    /* No script for this path: the single-listing fallback keeps every older
     * suite reading the same rows. */
    const char *listing = peer->listing;
    if (strcmp(peer->cwd, "/") != 0 && peer->sub_listing != NULL)
        listing = peer->sub_listing;
    else if (peer->refresh_listing != NULL && peer->listings >= 1)
        listing = peer->refresh_listing;

    peer->data = peer_data_socket(peer);
    fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
    if (listing != NULL && peer->data != NULL) {
        fake_net_queue_rx(peer->data, listing, strlen(listing));
        fake_net_shutdown(peer->data);
    }
    fake_ftpd_reply(&peer->server, 226, "Transfer complete.");
    peer->listings++;
    peer->data = NULL;
}

static void peer_send_mlst(FakePeer *peer, const char *path)
{
    char reply_text[PEER_REPLY_MAX];
    const char *perm =
        strcmp(peer->cwd, "/") == 0 ? peer->root_perm : peer->sub_perm;

    if (peer->mlst == FAKE_PEER_MLST_UNSUPPORTED) {
        fake_ftpd_reply(&peer->server, 502, "Not implemented.");
    } else if (peer->mlst == FAKE_PEER_MLST_FAIL) {
        fake_ftpd_reply(&peer->server, 421, "Service not available.");
    } else if (perm != NULL) {
        snprintf(reply_text, sizeof(reply_text),
                 "250-Listing %s\r\n"
                 " type=dir;perm=%s; %s\r\n"
                 "250 End\r\n",
                 path, perm, path);
        fake_ftpd_send_raw(&peer->server, reply_text);
    } else if (peer->mlst == FAKE_PEER_MLST_DIR) {
        snprintf(reply_text, sizeof(reply_text),
                 "250-Listing %s\r\n"
                 " type=dir; %s\r\n"
                 "250 End\r\n",
                 path, path);
        fake_ftpd_send_raw(&peer->server, reply_text);
    } else if (peer->exists_path != NULL &&
               strcmp(path, peer->exists_path) == 0) {
        snprintf(reply_text, sizeof(reply_text),
                 "250-Listing %s\r\n"
                 " type=file;size=%llu; %s\r\n"
                 "250 End\r\n",
                 path, (unsigned long long)peer->exists_size, path);
        fake_ftpd_send_raw(&peer->server, reply_text);
    } else {
        fake_ftpd_reply(&peer->server, 550, "Not found.");
    }
}

static void peer_send_size(FakePeer *peer, const char *path)
{
    peer->sizes++;

    int exists = (peer->exists_path != NULL &&
                  strcmp(path, peer->exists_path) == 0) ||
                 (peer->exists_path2 != NULL &&
                  strcmp(path, peer->exists_path2) == 0);
    if (peer->size_supported && exists) {
        char size[32];
        snprintf(size, sizeof(size), "%llu",
                 (unsigned long long)peer->exists_size);
        fake_ftpd_reply(&peer->server, 213, size);
    } else if (peer->size_supported) {
        fake_ftpd_reply(&peer->server, 550, "Not found.");
    } else {
        fake_ftpd_reply(&peer->server, 502, "Not implemented.");
    }
}

static void peer_send_retr(FakePeer *peer)
{
    peer->data = peer_data_socket(peer);
    fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
    peer->retrs++;
    if (!peer->hold_data && peer->payload != NULL && peer->data != NULL) {
        fake_net_queue_rx(peer->data, peer->payload, strlen(peer->payload));
        fake_net_shutdown(peer->data);
        if (!peer->hold_final)
            fake_ftpd_reply(&peer->server, 226, "Transfer complete.");
        peer->data = NULL;
    }
}

static void peer_handle_command(FakePeer *peer, const char *line)
{
    if (strncmp(line, "USER ", 5) == 0) {
        fake_ftpd_reply(&peer->server, 331, "Password required.");
    } else if (strncmp(line, "PASS ", 5) == 0) {
        fake_ftpd_reply(&peer->server, 230, "Logged in.");
    } else if (strcmp(line, "TYPE I") == 0) {
        fake_ftpd_reply(&peer->server, 200, "Type set.");
    } else if (strcmp(line, "PWD") == 0) {
        fake_ftpd_reply(&peer->server, 257,
                        "\"/\" is the current directory.");
    } else if (strcmp(line, "FEAT") == 0) {
        fake_ftpd_send_raw(&peer->server,
                           "211-Features:\r\n"
                           " MLST type*;size*;modify*;perm*;\r\n"
                           "211 End\r\n");
    } else if (strcmp(line, "PASV") == 0) {
        fake_ftpd_reply(&peer->server, 227,
                        "Entering Passive Mode (192,168,1,42,19,140).");
        peer->data = NULL;
    } else if (strncmp(line, "CWD ", 4) == 0) {
        snprintf(peer->cwd, sizeof(peer->cwd), "%s", line + 4);
        peer->cwds++;
        fake_ftpd_reply(&peer->server, 250, "Directory changed.");
    } else if (strncmp(line, "MKD ", 4) == 0) {
        peer->mkds++;
        if (peer->mkd_fail)
            fake_ftpd_reply(&peer->server, 550, "Permission denied.");
        else
            fake_ftpd_reply(&peer->server, 257, "Created.");
    } else if (strncmp(line, "DELE ", 5) == 0) {
        peer->deles++;
        fake_ftpd_reply(&peer->server, 250, "Deleted.");
    } else if (strncmp(line, "RMD ", 4) == 0) {
        peer->rmds++;
        fake_ftpd_reply(&peer->server, 250, "Removed.");
    } else if (strncmp(line, "RNFR ", 5) == 0) {
        peer->renames++;
        fake_ftpd_reply(&peer->server, 350, "Ready for RNTO.");
    } else if (strncmp(line, "RNTO ", 5) == 0) {
        fake_ftpd_reply(&peer->server, 250, "Renamed.");
    } else if (strncmp(line, "NOOP", 4) == 0) {
        fake_ftpd_reply(&peer->server, 200, "Still here.");
    } else if (strncmp(line, "MLST ", 5) == 0) {
        peer_send_mlst(peer, line + 5);
    } else if (strncmp(line, "STOR ", 5) == 0) {
        snprintf(peer->last_stor, sizeof(peer->last_stor), "%s", line + 5);
        peer->stors++;
        peer->stor_data = peer_data_socket(peer);
        fake_ftpd_reply(&peer->server, 150, "Opening data connection.");
    } else if ((strncmp(line, "MLSD", 4) == 0 &&
                (line[4] == ' ' || line[4] == '\0')) ||
               (strncmp(line, "LIST", 4) == 0 &&
                (line[4] == ' ' || line[4] == '\0'))) {
        const char *path = line[4] == ' ' ? line + 5 : "/";
        snprintf(peer->last_list_path, sizeof(peer->last_list_path), "%s",
                 path);
        if (peer->list_fail) {
            /* The client retries MLSD as LIST; both must fail for the listing
             * to end as a read error. */
            peer->list_fails++;
            fake_ftpd_reply(&peer->server, 550, "Listing refused.");
        } else {
            peer_send_listing(peer, path);
        }
    } else if (strncmp(line, "SIZE ", 5) == 0) {
        peer_send_size(peer, line + 5);
    } else if (strncmp(line, "RETR ", 5) == 0) {
        peer_send_retr(peer);
    } else if (strcmp(line, "ABOR") == 0) {
        fake_ftpd_reply(&peer->server, 426, "Transfer aborted.");
        fake_ftpd_reply(&peer->server, 226, "Abort successful.");
        peer->aborts++;
    } else if (strcmp(line, "QUIT") == 0) {
        fake_ftpd_reply(&peer->server, 221, "Bye.");
    } else {
        fake_ftpd_reply(&peer->server, 200, "OK.");
    }
}

void fake_peer_serve(FakePeer *peer)
{
    if (peer == NULL)
        return;

    peer_reap_stor(peer);

    char line[PEER_LINE_MAX];
    while (fake_ftpd_next_command(&peer->server, line, sizeof(line)) == 1)
        peer_handle_command(peer, line);
}
