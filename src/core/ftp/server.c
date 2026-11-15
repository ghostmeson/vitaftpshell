/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/ftp/server.h"

#include "core/fmt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SERVER_TX_MAX 16384
#define SERVER_MAX_CLIENTS 4
#define SERVER_DATA_BUF 8192
#define SERVER_DATA_BYTES_PER_TICK (32u * 1024u)
#define SERVER_DATA_SLICE_MS 6
#define SERVER_DATA_IN_BUF 4096
#define SERVER_LINE_MAX 2048
#define SERVER_ACCEPT_MS 5000
#define SERVER_DATA_IDLE_MS 15000
#define SERVER_CONTROL_IDLE_MS 30000
#define SERVER_ACCEPT_BATCH 4
#define SERVER_TX_HIGH_WATER (SERVER_TX_MAX * 3 / 4)
#define SERVER_LINK_CHECK_MS 1000

typedef enum {
    SDATA_LIST = 0,
    SDATA_MLSD,
    SDATA_NLST,
    SDATA_RETR,
    SDATA_STOR
} ServerDataKind;

typedef struct ServerSession {
    FtpServer *server;
    int used;
    NetSocket *control;
    NetAddress peer;

    FtpLineReader lines;
    char cwd[FTP_PATH_MAX];

    int user_seen;
    int user_ok;
    int logged_in;

    char tx[SERVER_TX_MAX];
    size_t tx_len;
    size_t tx_off;
    int tx_backlog;
    uint64_t control_deadline;

    int rnfr_valid;
    char rnfr_local[PATH_MAX_LEN];
    char rnfr_virtual[FTP_PATH_MAX];
} ServerSession;

struct FtpServer {
    NetPort *net;
    FsPort *fs;
    ClockPort *clock;
    FtpServerEventFn emit;
    void *emit_ctx;
    FtpServerTraceFn trace;
    void *trace_ctx;

    FtpServerState state;
    uint16_t control_port;
    uint16_t data_port;
    NetAddress lan_address;

    NetSocket *control_listener;
    NetSocket *data_listener;

    ServerSession sessions[SERVER_MAX_CLIENTS];
    NetAddress last_peer;

    FtpDataState data_state;
    int data_session;
    NetSocket *data;
    ServerDataKind data_kind;
    int pending_accept;
    uint64_t pending_deadline;
    uint64_t data_deadline;

    char active_virtual[FTP_PATH_MAX];
    char active_local[PATH_MAX_LEN];
    int listing_root;
    int listing_single;
    int listing_single_used;
    FsEntry single_entry;
    FsDir *dir;
    FsFile *file;
    FtpTempWrite temp;

    int data_eof;
    char data_buf[SERVER_DATA_BUF];
    size_t data_len;
    size_t data_off;
    unsigned data_budget;
    uint64_t data_slice_start;
    uint64_t transferred;
    uint64_t total;
    int total_known;

    char last_command[16];
    FtpServerGuardFn guard;
    void *guard_ctx;
    int root_index;

    uint64_t next_link_check;
    int clients;

    char detail[96];
};

static uint64_t server_now(FtpServer *server)
{
    return server->clock->now_ms(server->clock->ctx);
}

static void event_init(FtpServerEvent *event, FtpServerEventKind kind)
{
    memset(event, 0, sizeof(*event));
    event->kind = kind;
}

static void server_emit(FtpServer *server, const FtpServerEvent *event)
{
    if (server->emit != NULL)
        server->emit(server->emit_ctx, event);
}

static void server_state_event(FtpServer *server)
{
    FtpServerEvent event;
    event_init(&event, FTP_SERVER_EVENT_STATE);
    event.state = server->state;
    event.clients = server->clients;
    event.data_state = server->data_state;
    server_emit(server, &event);
}

static void server_client_event(FtpServer *server)
{
    FtpServerEvent event;
    event_init(&event, FTP_SERVER_EVENT_CLIENT);
    event.state = server->state;
    event.clients = server->clients;
    event.data_state = server->data_state;
    server_emit(server, &event);
}

static void server_progress_event(FtpServer *server)
{
    FtpServerEvent event;
    event_init(&event, FTP_SERVER_EVENT_PROGRESS);
    event.state = server->state;
    event.clients = server->clients;
    event.data_state = server->data_state;
    event.stage = FTP_STAGE_DATA;
    event.path = server->active_virtual;
    event.bytes = server->transferred;
    event.total = server->total;
    event.total_known = server->total_known;
    server_emit(server, &event);
}

static void server_transfer_event(FtpServer *server, int ok, FtpCause cause,
                                  const char *detail)
{
    FtpServerEvent event;
    event_init(&event, FTP_SERVER_EVENT_TRANSFER_DONE);
    event.state = server->state;
    event.clients = server->clients;
    event.data_state = server->data_state;
    event.stage = FTP_STAGE_TRANSFER;
    event.cause = cause;
    event.path = server->active_virtual;
    event.detail = detail;
    event.ok = ok;
    event.bytes = server->transferred;
    event.total = server->total;
    event.total_known = server->total_known;
    server_emit(server, &event);
}

static void server_error_event(FtpServer *server, const char *detail)
{
    FtpServerEvent event;
    event_init(&event, FTP_SERVER_EVENT_ERROR);
    event.state = server->state;
    event.clients = server->clients;
    event.data_state = server->data_state;
    event.detail = detail;
    server_emit(server, &event);
}

static ServerSession *server_session_at(FtpServer *server, int index)
{
    if (server == NULL || index < 0 || index >= SERVER_MAX_CLIENTS)
        return NULL;
    ServerSession *session = &server->sessions[index];
    return session->used ? session : NULL;
}

static int server_session_index(FtpServer *server, const ServerSession *session)
{
    if (server == NULL || session == NULL)
        return -1;
    return (int)(session - server->sessions);
}

static ServerSession *server_data_owner(FtpServer *server)
{
    return server_session_at(server, server->data_session);
}

static int server_append(ServerSession *session, const char *data, size_t len)
{
    if (session == NULL || len == 0)
        return 1;
    if (session->tx_len + len > sizeof(session->tx))
        return 0;
    memcpy(session->tx + session->tx_len, data, len);
    session->tx_len += len;
    return 1;
}

static int server_append_raw(ServerSession *session, const char *text)
{
    return server_append(session, text, strlen(text));
}

static void server_trace(FtpServer *server, const char *fmt, ...)
{
    if (server == NULL || server->trace == NULL)
        return;

    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    server->trace(server->trace_ctx, line);
}

static void server_trace_peer(FtpServer *server, const char *prefix,
                              NetAddress peer)
{
    server_trace(server, "%s %u.%u.%u.%u:%u", prefix, (peer.addr >> 24) & 0xFF,
                 (peer.addr >> 16) & 0xFF, (peer.addr >> 8) & 0xFF,
                 peer.addr & 0xFF, (unsigned)peer.port);
}

static void server_close_data(FtpServer *server);
static void server_close_transfer(FtpServer *server);
static void server_control_failed(FtpServer *server, ServerSession *session);
static void server_close_session(FtpServer *server, ServerSession *session,
                                 int send_221, const char *detail);

static void server_reply(ServerSession *session, int code, const char *fmt, ...)
{
    if (session == NULL || !session->used)
        return;
    FtpServer *server = session->server;

    char body[FTP_PATH_MAX + 64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    char line[FTP_PATH_MAX + 80];
    int n = snprintf(line, sizeof(line), "%03d %s\r\n", code, body);
    if (n > 0)
        server_trace(server, "FTPR %.*s", n > 2 ? n - 2 : n, line);
    if (n > 0 && !server_append(session, line, (size_t)n) &&
        !session->tx_backlog) {
        /* The peer is not draining its replies. Record it once; the command
         * loop stops reading non-ABOR/QUIT input until the buffer drains. */
        session->tx_backlog = 1;
        server_error_event(server, "REPLY BACKLOG");
    }
}

static void server_flush(ServerSession *session)
{
    if (session == NULL || !session->used || session->control == NULL)
        return;
    FtpServer *server = session->server;

    while (session->tx_off < session->tx_len) {
        size_t sent = 0;
        NetResult r = server->net->send(
            server->net->ctx, session->control,
            session->tx + session->tx_off,
            session->tx_len - session->tx_off, &sent);
        if (r == NET_WOULD_BLOCK)
            return;
        if (r != NET_OK || sent == 0) {
            server_control_failed(server, session);
            return;
        }
        session->tx_off += sent;
    }

    session->tx_len = 0;
    session->tx_off = 0;
    session->tx_backlog = 0;
}

static void server_drain_listener(FtpServer *server)
{
    if (server->data_listener == NULL)
        return;

    for (;;) {
        NetSocket *stale = NULL;
        NetAddress peer;
        NetResult r =
            server->net->accept(server->net->ctx, server->data_listener, &stale,
                                &peer);
        if (r == NET_WOULD_BLOCK)
            return;
        if (r != NET_OK)
            return;
        server->net->close(server->net->ctx, stale);
    }
}

static void server_close_data(FtpServer *server)
{
    if (server->data != NULL) {
        server->net->close(server->net->ctx, server->data);
        server->data = NULL;
    }
    server->data_state = FTP_DATA_NONE;
    server->pending_accept = 0;
}

static void server_close_transfer(FtpServer *server)
{
    if (server->dir != NULL) {
        server->fs->list_close(server->fs->ctx, server->dir);
        server->dir = NULL;
    }
    if (server->file != NULL) {
        server->fs->close(server->fs->ctx, server->file);
        server->file = NULL;
    }
    if (server->temp.open)
        ftp_temp_abort(server->fs, &server->temp);
    server->data_eof = 0;
    server->data_len = 0;
    server->data_off = 0;
    server->listing_root = 0;
    server->listing_single = 0;
    server->listing_single_used = 0;
    server->root_index = 0;
    server->data_session = -1;
    if (server->guard != NULL)
        (void)server->guard(server->guard_ctx, FTP_GUARD_RELEASE, NULL, 0, 0);
}

static void server_disarm(FtpServer *server)
{
    if (server->data_state == FTP_DATA_ACTIVE)
        return;
    server_close_data(server);
    server_close_transfer(server);
    server_drain_listener(server);
}

/* The one data slot belongs to a session in any non-idle state. Releasing it
 * runs the same teardown as a normal transfer end: data socket, reservation,
 * and any upload temp. Only an active transfer has bytes worth a terminal. */
static int server_owns_data(FtpServer *server, ServerSession *session)
{
    return server->data_state != FTP_DATA_NONE &&
           server->data_session == server_session_index(server, session);
}

static void server_release_owned_data(FtpServer *server,
                                      ServerSession *session, FtpCause cause,
                                      const char *detail)
{
    if (!server_owns_data(server, session))
        return;
    if (server->data_state == FTP_DATA_ACTIVE)
        server_transfer_event(server, 0, cause, detail);
    server_close_transfer(server);
    server_close_data(server);
}

static int server_path_held(ServerSession *session, const char *local,
                            int write, int tree, int hold)
{
    FtpServer *server = session->server;
    if (server->guard == NULL)
        return 0;

    if (server->guard(server->guard_ctx, FTP_GUARD_CHECK, local, write, tree)) {
        server_reply(session, 450, "Path busy.");
        if (server->data_session < 0 ||
            server->data_session == server_session_index(server, session))
            server_disarm(server);
        return 1;
    }
    if (hold &&
        server->guard(server->guard_ctx, FTP_GUARD_HOLD, local, write, tree)) {
        server_reply(session, 451, "Resource busy.");
        if (server->data_session < 0 ||
            server->data_session == server_session_index(server, session))
            server_disarm(server);
        return 1;
    }
    return 0;
}

static void server_emit_state_and_client(FtpServer *server)
{
    server_state_event(server);
    server_client_event(server);
}


static void server_control_failed(FtpServer *server, ServerSession *session)
{
    /* One teardown body serves both close paths; the flush is skipped so a
     * failed send cannot recurse back here. */
    server_close_session(server, session, 0, "CONTROL LOST");
}

static void server_stop_internal(FtpServer *server, const char *detail)
{
    int had_transfer =
        server->data_state == FTP_DATA_ACTIVE || server->pending_accept;
    if (had_transfer) {
        server_transfer_event(server, 0,
                              detail != NULL ? FTP_CAUSE_NETWORK
                                             : FTP_CAUSE_USER,
                              detail != NULL ? detail : "STOPPED");
    }

    server_close_transfer(server);
    server_close_data(server);

    for (int i = 0; i < SERVER_MAX_CLIENTS; ++i) {
        ServerSession *session = &server->sessions[i];
        if (!session->used)
            continue;
        if (session->control != NULL)
            server->net->close(server->net->ctx, session->control);
        session->used = 0;
    }
    if (server->control_listener != NULL) {
        server->net->close(server->net->ctx, server->control_listener);
        server->control_listener = NULL;
    }
    if (server->data_listener != NULL) {
        server->net->close(server->net->ctx, server->data_listener);
        server->data_listener = NULL;
    }

    server->clients = 0;
    server->state = FTP_SERVER_OFF;
    server_emit_state_and_client(server);

    if (detail != NULL) {
        snprintf(server->detail, sizeof(server->detail), "%s", detail);
        server_error_event(server, server->detail);
    }
}

static void server_close_session(FtpServer *server, ServerSession *session,
                                 int send_221, const char *detail)
{
    if (session == NULL || !session->used)
        return;

    server_trace(server, "FTPD CLOSE");
    if (send_221) {
        server_reply(session, 221, "Goodbye.");
        server_flush(session);
        if (!session->used)
            return;
    }

    server_release_owned_data(server, session,
                              send_221 ? FTP_CAUSE_USER : FTP_CAUSE_NETWORK,
                              send_221 ? "QUIT" : "CONTROL LOST");

    if (session->control != NULL) {
        server->net->close(server->net->ctx, session->control);
        session->control = NULL;
    }
    session->used = 0;
    server->clients--;
    if (server->clients < 0)
        server->clients = 0;
    if (server->clients == 0)
        server->state = FTP_SERVER_ON;
    server_emit_state_and_client(server);
    if (detail != NULL) {
        snprintf(server->detail, sizeof(server->detail), "%s", detail);
        server_error_event(server, server->detail);
    }
}

static int ci_equal_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        if (ca == '\0')
            return 1;
    }
    return 1;
}

static int ci_equal(const char *a, const char *b)
{
    size_t n = strlen(b);
    if (strlen(a) != n)
        return 0;
    return ci_equal_n(a, b, n);
}

static int starts_with_ci(const char *text, const char *prefix)
{
    return ci_equal_n(text, prefix, strlen(prefix));
}

static int volume_index(const char *token, size_t len)
{
    static const char *const NAMES[VOLUME_COUNT] = {"ux0", "ur0", "uma0"};
    for (int v = 0; v < VOLUME_COUNT; ++v) {
        if (len == strlen(NAMES[v]) && ci_equal_n(token, NAMES[v], len))
            return v;
    }
    return -1;
}

/*
 * Resolve a remote path to the virtual path and the local canonical path.
 * `/` is the virtual root. A first segment must be a volume name, optionally
 * followed by `:` and any bytes below it. Returns 0 for an invalid path.
 */
static int server_resolve(ServerSession *session, const char *arg,
                          char *virtual_out, size_t vcap, Path *local,
                          int *is_root)
{
    const char *input = (arg != NULL && arg[0] != '\0') ? arg : session->cwd;
    char virtual[FTP_PATH_MAX];
    if (!ftp_remote_normalize(session->cwd, input, virtual, sizeof(virtual)))
        return 0;

    if (virtual[0] != '/')
        return 0;

    if (is_root != NULL)
        *is_root = 0;
    if (local != NULL)
        local->text[0] = '\0';

    if (strcmp(virtual, "/") == 0) {
        if (is_root != NULL)
            *is_root = 1;
        if (virtual_out != NULL)
            snprintf(virtual_out, vcap, "%s", virtual);
        return 1;
    }

    const char *p = virtual + 1;
    const char *end = p;
    while (*end != '\0' && *end != '/' && *end != ':')
        end++;

    int v = volume_index(p, (size_t)(end - p));
    if (v < 0)
        return 0;

    char vtoken[16];
    size_t token_len = (size_t)(end - p);
    if (token_len == 0 || token_len >= sizeof(vtoken))
        return 0;
    memcpy(vtoken, p, token_len);
    vtoken[token_len] = '\0';

    char vname[18];
    int vn = snprintf(vname, sizeof(vname), "%s:", vtoken);
    if (vn < 0 || (size_t)vn >= sizeof(vname))
        return 0;

    const char *rest = *end == ':' ? end + 1 : end;

    char combined[PATH_MAX_LEN];
    const char *name = path_volume_name((Volume)v);
    int n = snprintf(combined, sizeof(combined), "%s%s", name, rest);
    if (n < 0 || (size_t)n >= sizeof(combined))
        return 0;

    Path canonical;
    if (path_canonicalize(combined, &canonical) != PATH_OK)
        return 0;

    /* A second colon below the volume is a device-prefix ambiguity, not a
     * filename. Refuse it before any adapter sees the string. */
    if (strchr(canonical.text + strlen(name), ':') != NULL)
        return 0;

    if (local != NULL)
        *local = canonical;

    if (virtual_out != NULL) {
        int written;
        if (rest[0] == '/')
            written = snprintf(virtual_out, vcap, "/%s%s", vname, rest);
        else if (rest[0] != '\0')
            written = snprintf(virtual_out, vcap, "/%s/%s", vname, rest);
        else
            written = snprintf(virtual_out, vcap, "/%s", vname);
        if (written < 0 || (size_t)written >= vcap)
            return 0;
    }
    return 1;
}

static void format_mdtm(uint32_t mtime, char *out, size_t cap)
{
    time_t t = (time_t)mtime;
    struct tm *tm = gmtime(&t);
    if (tm == NULL) {
        snprintf(out, cap, "19700101000000");
        return;
    }
    int year = tm->tm_year + 1900;
    if (year < 0)
        year = 0;
    if (year > 9999)
        year = 9999;
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%04d%02d%02d%02d%02d%02d", year,
             tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
             tm->tm_sec);
    fmt_copy(out, cap, stamp);
}

static void format_unix_when(uint32_t mtime, char *out, size_t cap)
{
    static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr",
                                         "May", "Jun", "Jul", "Aug",
                                         "Sep", "Oct", "Nov", "Dec"};
    time_t t = (time_t)mtime;
    struct tm *tm = gmtime(&t);
    if (tm == NULL) {
        snprintf(out, cap, "Jan 01 00:00");
        return;
    }
    snprintf(out, cap, "%s %02d %02d:%02d", MONTHS[tm->tm_mon], tm->tm_mday,
             tm->tm_hour, tm->tm_min);
}

static int format_unix_line(const FsEntry *entry, const char *name, char *out,
                            size_t cap)
{
    int dir = entry->type == FS_ENTRY_DIR;
    uint64_t size = entry->size_known ? entry->size : 0;
    char when[24] = "Jan 01 00:00";
    if (entry->mtime_known)
        format_unix_when(entry->mtime, when, sizeof(when));

    int n = snprintf(out, cap, "%c%s 1 vftp vftp %llu %s %s\r\n",
                     dir ? 'd' : '-', dir ? "rwxr-xr-x" : "rw-r--r--",
                     (unsigned long long)size, when, name);
    return n > 0 && (size_t)n < cap;
}

static int format_mlsd_line(const FsEntry *entry, const char *name, char *out,
                            size_t cap)
{
    int dir = entry->type == FS_ENTRY_DIR;
    char facts[512];
    int used = 0;
    if (dir) {
        used = snprintf(facts, sizeof(facts), "type=dir;");
    } else {
        used = snprintf(facts, sizeof(facts), "type=file;");
    }
    if (used < 0 || (size_t)used >= sizeof(facts))
        return 0;

    if (entry->size_known && !dir) {
        int n = snprintf(facts + used, sizeof(facts) - (size_t)used,
                         "size=%llu;", (unsigned long long)entry->size);
        if (n < 0 || (size_t)n >= sizeof(facts) - (size_t)used)
            return 0;
        used += n;
    }
    if (entry->mtime_known) {
        char stamp[20];
        format_mdtm(entry->mtime, stamp, sizeof(stamp));
        int n = snprintf(facts + used, sizeof(facts) - (size_t)used,
                         "modify=%s;", stamp);
        if (n < 0 || (size_t)n >= sizeof(facts) - (size_t)used)
            return 0;
        used += n;
    }

    int n = snprintf(out, cap, "%s %s\r\n", facts, name);
    return n > 0 && (size_t)n < cap;
}

static int format_nlst_line(const FsEntry *entry, const char *name, char *out,
                            size_t cap)
{
    (void)entry;
    int n = snprintf(out, cap, "%s\r\n", name);
    return n > 0 && (size_t)n < cap;
}

static int format_listing_line(ServerDataKind kind, const FsEntry *entry,
                               const char *name, char *out, size_t cap)
{
    switch (kind) {
    case SDATA_MLSD:
        return format_mlsd_line(entry, name, out, cap);
    case SDATA_NLST:
        return format_nlst_line(entry, name, out, cap);
    case SDATA_LIST:
    default:
        return format_unix_line(entry, name, out, cap);
    }
}

static int server_next_listing_line(FtpServer *server, char *out, size_t cap)
{
    if (server->listing_root) {
        while (server->root_index < VOLUME_COUNT) {
            Volume v = (Volume)server->root_index++;
            const char *name = path_volume_name(v);
            FsEntry entry;
            memset(&entry, 0, sizeof(entry));
            if (server->fs->stat(server->fs->ctx, name, &entry) != FS_OK)
                continue;
            if (entry.type != FS_ENTRY_DIR)
                continue;
            char display[16];
            snprintf(display, sizeof(display), "%s", name);
            for (char *q = display; *q != '\0' && *q != ':'; ++q) {
                if (*q >= 'a' && *q <= 'z')
                    *q = (char)(*q - 'a' + 'A');
            }
            return format_listing_line(server->data_kind, &entry, display, out,
                                       cap)
                       ? 1
                       : -1;
        }
        return 0;
    }

    if (server->listing_single) {
        if (server->listing_single_used)
            return 0;
        server->listing_single_used = 1;
        const char *name = server->single_entry.name;
        return format_listing_line(server->data_kind, &server->single_entry,
                                   name, out, cap)
                   ? 1
                   : -1;
    }

    if (server->dir == NULL)
        return 0;

    FsEntry entry;
    for (;;) {
        FsResult r =
            server->fs->list_next(server->fs->ctx, server->dir, &entry);
        if (r == FS_END)
            return 0;
        if (r != FS_OK)
            return -1;
        if (path_sidecar_kind(entry.name) == PATH_SIDECAR_PART)
            continue;
        return format_listing_line(server->data_kind, &entry, entry.name, out,
                                   cap)
                   ? 1
                   : -1;
    }
}

static void server_data_error(FtpServer *server, int code, FtpCause cause,
                              const char *detail)
{
    ServerSession *owner = server_data_owner(server);
    server_close_data(server);
    server_close_transfer(server);
    if (owner != NULL)
        server_reply(owner, code, "%s", detail);
    server_transfer_event(server, 0, cause, detail);
}

static int server_fill_data(FtpServer *server)
{
    server->data_len = 0;
    server->data_off = 0;

    if (server->data_kind == SDATA_RETR) {
        if (server->file == NULL) {
            server->data_eof = 1;
            return 1;
        }
        size_t got = 0;
        FsResult r = server->fs->read(server->fs->ctx, server->file,
                                      server->data_buf,
                                      sizeof(server->data_buf), &got);
        if (r == FS_END) {
            server->data_eof = 1;
            return 1;
        }
        if (r != FS_OK) {
            server_data_error(server, 451, FTP_CAUSE_STORAGE,
                              "STORAGE READ FAILED");
            return 0;
        }
        if (got == 0) {
            server->data_eof = 1;
            return 1;
        }
        server->data_len = got;
        return 1;
    }

    while (server->data_len < sizeof(server->data_buf) - SERVER_LINE_MAX) {
        char line[SERVER_LINE_MAX];
        int r = server_next_listing_line(server, line, sizeof(line));
        if (r == 0) {
            server->data_eof = 1;
            break;
        }
        if (r < 0) {
            server_data_error(server, 451, FTP_CAUSE_STORAGE,
                              "STORAGE LIST FAILED");
            return 0;
        }
        size_t len = strlen(line);
        if (server->data_len + len > sizeof(server->data_buf)) {
            server_data_error(server, 451, FTP_CAUSE_APP,
                              "LISTING TOO LARGE");
            return 0;
        }
        memcpy(server->data_buf + server->data_len, line, len);
        server->data_len += len;
    }

    return 1;
}

static void server_finish_data(FtpServer *server, int ok, FtpCause cause,
                               const char *detail)
{
    ServerSession *owner = server_data_owner(server);
    server_close_data(server);

    server_trace(server, "FTPD FINISH kind=%d ok=%d open=%d", (int)server->data_kind,
                 ok, server->temp.open);
    if (server->data_kind == SDATA_STOR) {
        if (ok && server->temp.open) {
            FtpCommitStep step = FTP_COMMIT_OK;
            FsResult r = ftp_temp_finish(server->fs, &server->temp, &step);
            server_trace(server, "FTPD COMMIT r=%d step=%d", (int)r, (int)step);
            if (r != FS_OK) {
                server_close_transfer(server);
                if (owner != NULL)
                    server_reply(owner, 451, "Storage write failed.");
                server_transfer_event(server, 0, FTP_CAUSE_STORAGE,
                                      step == FTP_COMMIT_RENAME
                                          ? "DESTINATION RENAME FAILED"
                                          : "DESTINATION CLOSE FAILED");
                return;
            }
        } else if (server->temp.open) {
            ftp_temp_abort(server->fs, &server->temp);
        }
    }

    server_close_transfer(server);

    if (ok) {
        if (owner != NULL)
            server_reply(owner, 226, "Transfer complete.");
        server_transfer_event(server, 1, FTP_CAUSE_APP, NULL);
    } else {
        if (owner != NULL)
            server_reply(owner, 426, "%s",
                         detail != NULL ? detail : "Aborted.");
        server_transfer_event(server, 0, cause, detail);
    }
}

static int server_pump_send(FtpServer *server)
{
    if (server->data_off < server->data_len) {
        size_t sent = 0;
        NetResult r = server->net->send(
            server->net->ctx, server->data,
            server->data_buf + server->data_off,
            server->data_len - server->data_off, &sent);
        if (r == NET_WOULD_BLOCK)
            return 0;
        if (r != NET_OK || sent == 0) {
            server_data_error(server, 426, FTP_CAUSE_NETWORK,
                              "DATA CONNECTION LOST");
            return 0;
        }
        server->data_off += sent;
        server->transferred += sent;
        server->data_deadline = server_now(server) + SERVER_DATA_IDLE_MS;
        server->data_budget = sent >= server->data_budget
                                  ? 0
                                  : server->data_budget - (unsigned)sent;
        server_progress_event(server);
        if (server->data_off < server->data_len)
            return 1;
        server->data_len = 0;
        server->data_off = 0;
    }

    if (!server->data_eof) {
        if (!server_fill_data(server))
            return 0;
    }

    if (server->data_eof && server->data_len == 0) {
        server_finish_data(server, 1, FTP_CAUSE_APP, NULL);
        return 0;
    }
    return 1;
}

static int server_pump_stor(FtpServer *server)
{
    char buf[SERVER_DATA_IN_BUF];
    size_t got = 0;
    NetResult r = server->net->receive(server->net->ctx, server->data, buf,
                                       sizeof(buf), &got);
    if (r == NET_WOULD_BLOCK)
        return 0;

    if (r == NET_CLOSED) {
        server_trace(server, "FTPD EOF bytes=%llu",
                     (unsigned long long)server->transferred);
        server_finish_data(server, 1, FTP_CAUSE_APP, NULL);
        return 0;
    }
    if (r != NET_OK) {
        server_trace(server, "FTPD RECV FAIL r=%d", (int)r);
        server_data_error(server, 426, FTP_CAUSE_NETWORK,
                          "DATA CONNECTION LOST");
        return 0;
    }

    if (got > 0) {
        if (server->transferred == 0)
            server_trace(server, "FTPD RX %llu", (unsigned long long)got);
        if (server->temp.file == NULL || !server->temp.open) {
            server_data_error(server, 451, FTP_CAUSE_STORAGE,
                              "STORAGE WRITE FAILED");
            return 0;
        }
        size_t total = 0;
        while (total < got) {
            size_t written = 0;
            FsResult wr = server->fs->write(
                server->fs->ctx, server->temp.file, buf + total, got - total,
                &written);
            if (wr != FS_OK || written == 0) {
                server_trace(server, "FTPD WRITE FAIL r=%d written=%llu",
                             (int)wr, (unsigned long long)written);
                server_data_error(server, 451, FTP_CAUSE_STORAGE,
                                  "STORAGE WRITE FAILED");
                return 0;
            }
            total += written;
        }
        server->data_deadline = server_now(server) + SERVER_DATA_IDLE_MS;
        server->transferred += got;
        server->data_budget = got >= server->data_budget
                                  ? 0
                                  : server->data_budget - (unsigned)got;
        server_progress_event(server);
        return 1;
    }
    return 0;
}

static void server_pump_data(FtpServer *server)
{
    if (server->data_state != FTP_DATA_ACTIVE || server->data == NULL)
        return;

    while (server->data_budget > 0) {
        if (server_now(server) - server->data_slice_start >=
            SERVER_DATA_SLICE_MS)
            break;
        int moved = server->data_kind == SDATA_STOR
                        ? server_pump_stor(server)
                        : server_pump_send(server);
        if (!moved)
            break;
    }
}

static void server_begin_data(FtpServer *server)
{
    ServerSession *owner = server_data_owner(server);
    server->data_state = FTP_DATA_ACCEPTED;
    server->pending_accept = 0;

    if (server->data_kind == SDATA_RETR) {
        FsResult r = server->fs->open(server->fs->ctx, server->active_local,
                                      FS_OPEN_READ, &server->file);
        server_trace(server, "FTPD RETR OPEN %s r=%d", server->active_local,
                     (int)r);
        if (r != FS_OK) {
            if (owner != NULL) server_reply(owner, 550, "Not found.");
            server_close_data(server);
            server_close_transfer(server);
            return;
        }
    } else if (server->data_kind == SDATA_STOR) {
        FsResult r = ftp_temp_open(server->fs, server->active_local,
                                   &server->temp);
        server_trace(server, "FTPD STOR OPEN %s r=%d temp=%s",
                     server->active_local, (int)r, server->temp.temp.text);
        if (r != FS_OK) {
            if (owner != NULL) server_reply(owner, 550, "Not found.");
            server_close_data(server);
            server_close_transfer(server);
            return;
        }
    } else if (!server->listing_root && !server->listing_single) {
        FsResult r = server->fs->list_open(server->fs->ctx,
                                           server->active_local,
                                           &server->dir);
        server_trace(server, "FTPD LIST OPEN %s r=%d", server->active_local,
                     (int)r);
        if (r != FS_OK) {
            if (owner != NULL) server_reply(owner, 550, "Not found.");
            server_close_data(server);
            server_close_transfer(server);
            return;
        }
    }

    server->data_len = 0;
    server->data_off = 0;
    server->data_eof = 0;
    server->transferred = 0;
    server->total = 0;
    server->total_known = 0;
    if (server->data_kind == SDATA_RETR) {
        FsEntry entry;
        if (server->fs->stat(server->fs->ctx, server->active_local, &entry) ==
                FS_OK &&
            entry.size_known) {
            server->total = entry.size;
            server->total_known = 1;
        }
    }
    server->data_deadline = server_now(server) + SERVER_DATA_IDLE_MS;
    server->data_state = FTP_DATA_ACTIVE;
    if (owner != NULL) server_reply(owner, 150, "Opening data connection.");
}

static void server_try_accept(FtpServer *server)
{
    if (server->data_state != FTP_DATA_ARMED || !server->pending_accept)
        return;
    ServerSession *owner = server_data_owner(server);
    if (owner == NULL)
        return;

    /* Drain a bounded number of queued connections per tick. A foreign peer
     * is closed and the loop continues; the command only fails when the
     * pending deadline passes with no matching peer. */
    for (int i = 0; i < SERVER_ACCEPT_BATCH; ++i) {
        NetSocket *data = NULL;
        NetAddress peer;
        NetResult r = server->net->accept(server->net->ctx,
                                          server->data_listener, &data, &peer);
        if (r == NET_WOULD_BLOCK)
            return;
        if (r != NET_OK) {
            server_trace(server, "FTPD ACCEPT FAIL r=%d", (int)r);
            server_reply(owner, 425, "Cannot open data connection.");
            server_disarm(server);
            return;
        }

        if (peer.addr != owner->peer.addr) {
            server_trace_peer(server, "FTPD FOREIGN", peer);
            server->net->close(server->net->ctx, data);
            continue;
        }

        server_trace_peer(server, "FTPD ACCEPT", peer);
        server->data = data;
        server->data_state = FTP_DATA_ACCEPTED;
        server_begin_data(server);
        return;
    }
}

static int server_data_command(ServerSession *session, ServerDataKind kind,
                               const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root)) {
        server_reply(session, 550, "Not found.");
        server_disarm(server);
        return 0;
    }

    if ((kind == SDATA_RETR || kind == SDATA_STOR) && is_root) {
        server_reply(session, 550, "Not found.");
        server_disarm(server);
        return 0;
    }

    FsEntry entry;
    memset(&entry, 0, sizeof(entry));
    int single = 0;

    if (kind == SDATA_RETR || kind == SDATA_STOR) {
        PathRisk risk = PATH_RISK_NORMAL;
        PathResult pr = kind == SDATA_RETR
                            ? path_check_read(local.text, 1, &risk)
                            : path_check_write(local.text, 1, &risk);
        if (pr != PATH_OK) {
            server_reply(session, 550, "Not found.");
            server_disarm(server);
            return 0;
        }
    } else if (!is_root) {
        PathRisk risk = PATH_RISK_NORMAL;
        if (path_check_read(local.text, 1, &risk) != PATH_OK) {
            server_reply(session, 550, "Not found.");
            server_disarm(server);
            return 0;
        }
    }

    if (!is_root) {
        FsResult sr = server->fs->stat(server->fs->ctx, local.text, &entry);
        if (kind == SDATA_STOR) {
            /* A missing destination is the normal create case. */
            if (sr == FS_OK && entry.type == FS_ENTRY_DIR) {
                server_reply(session, 550, "Not found.");
                server_disarm(server);
                return 0;
            }
            if (sr != FS_OK && sr != FS_NOT_FOUND) {
                server_reply(session, 550, "Not found.");
                server_disarm(server);
                return 0;
            }
        } else {
            if (sr != FS_OK) {
                server_reply(session, 550, "Not found.");
                server_disarm(server);
                return 0;
            }
            if (kind == SDATA_RETR && entry.type != FS_ENTRY_FILE) {
                server_reply(session, 550, "Not found.");
                server_disarm(server);
                return 0;
            }
            single = entry.type != FS_ENTRY_DIR;
        }
    }

    /* The guard's refusal is answered on the owning session. */
    server->data_session = server_session_index(server, session);
    if (!is_root &&
        server_path_held(session, local.text, kind == SDATA_STOR, 0,
                         kind != SDATA_LIST))
        return 0;

    snprintf(server->active_virtual, sizeof(server->active_virtual), "%s",
             is_root ? "/" : virtual);
    snprintf(server->active_local, sizeof(server->active_local), "%s",
             is_root ? "" : local.text);
    server->listing_root = is_root;
    server->listing_single = single;
    server->listing_single_used = 0;
    server->single_entry = entry;
    server->root_index = 0;
    server->data_kind = kind;
    server->pending_accept = 1;
    server->pending_deadline = server_now(server) + SERVER_ACCEPT_MS;
    server->data_state = FTP_DATA_ARMED;

    server_try_accept(server);
    return 1;
}

static void server_abort_command(ServerSession *session)
{
    FtpServer *server = session->server;
    if ((server->data_state == FTP_DATA_ACTIVE || server->pending_accept) &&
        server->data_session != server_session_index(server, session)) {
        server_reply(session, 225, "No transfer in progress.");
        return;
    }
    if (server->data_state == FTP_DATA_ACTIVE || server->pending_accept) {
        server_close_transfer(server);
        server_close_data(server);
        server_reply(session, 426, "Transfer aborted.");
        server_reply(session, 226, "Abort successful.");
        server_transfer_event(server, 0, FTP_CAUSE_USER, "ABORTED");
        return;
    }

    if (server->data_state != FTP_DATA_NONE) {
        /* Only the owner may drop its own armed PASV. */
        if (server->data_session == server_session_index(server, session))
            server_disarm(server);
        server_reply(session, 225, "No transfer in progress.");
        return;
    }

    server_reply(session, 225, "No transfer in progress.");
}

static void server_pasv(ServerSession *session)
{
    FtpServer *server = session->server;
    if (server->data_listener == NULL) {
        server_reply(session, 425, "Cannot open data connection.");
        return;
    }

    if ((server->data_state == FTP_DATA_ACTIVE || server->pending_accept ||
         server->data_state == FTP_DATA_ARMED) &&
        server->data_session != server_session_index(server, session)) {
        server_reply(session, 450, "Another client is transferring.");
        return;
    }

    server_disarm(server);
    server_drain_listener(server);
    server->data_session = server_session_index(server, session);
    server->data_state = FTP_DATA_ARMED;

    uint32_t a = server->lan_address.addr;
    unsigned p1 = server->data_port / 256;
    unsigned p2 = server->data_port % 256;
    server_reply(session, 227,
                 "Entering Passive Mode (%u,%u,%u,%u,%u,%u).", (a >> 24) & 0xFF,
                 (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF, p1, p2);
}

static void server_mlst(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root)) {
        server_reply(session, 550, "Not found.");
        return;
    }

    FsEntry entry;
    memset(&entry, 0, sizeof(entry));
    const char *name = "/";

    if (!is_root) {
        if (server_path_held(session, local.text, 0, 0, 0))
            return;
        PathRisk risk = PATH_RISK_NORMAL;
        if (path_check_read(local.text, 1, &risk) != PATH_OK ||
            server->fs->stat(server->fs->ctx, local.text, &entry) != FS_OK) {
            server_reply(session, 550, "Not found.");
            return;
        }
        name = virtual;
    } else {
        entry.type = FS_ENTRY_DIR;
        entry.size_known = 0;
    }

    char line[1024];
    if (!format_mlsd_line(&entry, name, line, sizeof(line))) {
        server_reply(session, 550, "Not found.");
        return;
    }
    line[strlen(line) - 2] = '\0';

    char head[FTP_PATH_MAX + 32];
    snprintf(head, sizeof(head), "250-Listing %s\r\n ", name);
    server_append_raw(session, head);
    server_append_raw(session, line);
    server_append_raw(session, "\r\n250 End\r\n");
}

static void server_feat(ServerSession *session)
{
    server_append_raw(session, "211-Features:\r\n");
    server_append_raw(session, " MLST type*;size*;modify*;perm*;\r\n");
    server_append_raw(session, " UTF8\r\n");
    server_append_raw(session, "211 End\r\n");
}

static void server_handle_cwd(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root)) {
        server_reply(session, 550, "Not found.");
        return;
    }

    if (is_root) {
        snprintf(session->cwd, sizeof(session->cwd), "/");
        server_reply(session, 250, "Directory changed.");
        return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    FsEntry entry;
    if (path_check_read(local.text, 1, &risk) != PATH_OK ||
        server->fs->stat(server->fs->ctx, local.text, &entry) != FS_OK ||
        entry.type != FS_ENTRY_DIR) {
        server_reply(session, 550, "Not found.");
        return;
    }

    snprintf(session->cwd, sizeof(session->cwd), "%s", virtual);
    server_reply(session, 250, "Directory changed.");
}

static void server_handle_cdup(ServerSession *session)
{
    FtpServer *server = session->server;
    char next[FTP_PATH_MAX];
    if (!ftp_remote_normalize(session->cwd, "..", next, sizeof(next))) {
        server_reply(session, 550, "Not found.");
        return;
    }
    if (strcmp(next, "/") == 0) {
        snprintf(session->cwd, sizeof(session->cwd), "/");
        server_reply(session, 250, "Directory changed.");
        return;
    }

    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;
    if (!server_resolve(session, next, virtual, sizeof(virtual), &local,
                        &is_root) ||
        is_root) {
        server_reply(session, 550, "Not found.");
        return;
    }

    FsEntry entry;
    PathRisk risk = PATH_RISK_NORMAL;
    if (path_check_read(local.text, 1, &risk) != PATH_OK ||
        server->fs->stat(server->fs->ctx, local.text, &entry) != FS_OK ||
        entry.type != FS_ENTRY_DIR) {
        server_reply(session, 550, "Not found.");
        return;
    }

    snprintf(session->cwd, sizeof(session->cwd), "%s", virtual);
    server_reply(session, 250, "Directory changed.");
}

static void server_handle_mkd(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root) ||
        is_root) {
        server_reply(session, 550, "Not found.");
        return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    if (path_check_write(local.text, 1, &risk) != PATH_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }
    if (server_path_held(session, local.text, 1, 0, 0))
        return;

    if (server->fs->mkdir(server->fs->ctx, local.text) != FS_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }
    server_reply(session, 250, "Directory created.");
}

static void server_handle_dele(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root) ||
        is_root) {
        server_reply(session, 550, "Not found.");
        return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    if (path_check_delete(local.text, 1, &risk) != PATH_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }
    if (server_path_held(session, local.text, 1, 1, 0))
        return;

    if (server->fs->remove(server->fs->ctx, local.text) != FS_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }
    server_reply(session, 250, "File deleted.");
}

static void server_handle_rmd(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root) ||
        is_root) {
        server_reply(session, 550, "Not found.");
        return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    if (path_check_delete(local.text, 1, &risk) != PATH_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }
    if (server_path_held(session, local.text, 1, 1, 0))
        return;

    if (server->fs->rmdir(server->fs->ctx, local.text) != FS_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }
    server_reply(session, 250, "Directory removed.");
}

static void server_handle_rnfr(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    session->rnfr_valid = 0;
    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root) ||
        is_root) {
        server_reply(session, 550, "Not found.");
        return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    if (path_check_delete(local.text, 1, &risk) != PATH_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }
    if (server_path_held(session, local.text, 1, 1, 0))
        return;

    FsEntry entry;
    if (server->fs->stat(server->fs->ctx, local.text, &entry) != FS_OK) {
        server_reply(session, 550, "Not found.");
        return;
    }

    snprintf(session->rnfr_local, sizeof(session->rnfr_local), "%s", local.text);
    snprintf(session->rnfr_virtual, sizeof(session->rnfr_virtual), "%s", virtual);
    session->rnfr_valid = 1;
    server_reply(session, 350, "Ready for destination name.");
}

static void server_handle_rnto(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    if (!session->rnfr_valid) {
        server_reply(session, 503, "RNFR required first.");
        return;
    }

    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;
    int ok = 0;

    if (server_resolve(session, arg, virtual, sizeof(virtual), &local,
                       &is_root) &&
        !is_root) {
        PathRisk risk = PATH_RISK_NORMAL;
        if (path_check_rename(session->rnfr_local, local.text, 1, &risk) ==
            PATH_OK) {
            if (server_path_held(session, session->rnfr_local, 1, 1, 0) ||
                server_path_held(session, local.text, 1, 1, 0)) {
                /* The 450 is already on the wire; a second reply would desync
                 * the client's command/reply pairing. */
                session->rnfr_valid = 0;
                return;
            }
            ok = server->fs->rename(server->fs->ctx, session->rnfr_local,
                                    local.text) == FS_OK;
        }
    }

    session->rnfr_valid = 0;
    if (!ok) {
        server_reply(session, 550, "Not found.");
        return;
    }
    server_reply(session, 250, "Renamed.");
}

static void server_handle_size(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root) ||
        is_root) {
        server_reply(session, 550, "Not found.");
        return;
    }

    if (server_path_held(session, local.text, 0, 0, 0))
        return;

    PathRisk risk = PATH_RISK_NORMAL;
    FsEntry entry;
    if (path_check_read(local.text, 1, &risk) != PATH_OK ||
        server->fs->stat(server->fs->ctx, local.text, &entry) != FS_OK ||
        entry.type != FS_ENTRY_FILE || !entry.size_known) {
        server_reply(session, 550, "Not found.");
        return;
    }
    server_reply(session, 213, "%llu", (unsigned long long)entry.size);
}

static void server_handle_mdtm(ServerSession *session, const char *arg)
{
    FtpServer *server = session->server;
    char virtual[FTP_PATH_MAX];
    Path local;
    int is_root = 0;

    if (!server_resolve(session, arg, virtual, sizeof(virtual), &local,
                        &is_root) ||
        is_root) {
        server_reply(session, 550, "Not found.");
        return;
    }

    if (server_path_held(session, local.text, 0, 0, 0))
        return;

    PathRisk risk = PATH_RISK_NORMAL;
    FsEntry entry;
    if (path_check_read(local.text, 1, &risk) != PATH_OK ||
        server->fs->stat(server->fs->ctx, local.text, &entry) != FS_OK ||
        entry.type != FS_ENTRY_FILE || !entry.mtime_known) {
        server_reply(session, 550, "Not found.");
        return;
    }

    char stamp[20];
    format_mdtm(entry.mtime, stamp, sizeof(stamp));
    server_reply(session, 213, "%s", stamp);
}

static int server_busy_command(const char *cmd)
{
    return strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0 ||
           strcmp(cmd, "MLSD") == 0 || strcmp(cmd, "RETR") == 0 ||
           strcmp(cmd, "STOR") == 0 || strcmp(cmd, "DELE") == 0 ||
           strcmp(cmd, "MKD") == 0 || strcmp(cmd, "RMD") == 0 ||
           strcmp(cmd, "RNFR") == 0 || strcmp(cmd, "RNTO") == 0 ||
           strcmp(cmd, "PASV") == 0;
}

static int server_data_command_name(const char *cmd)
{
    return strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0 ||
           strcmp(cmd, "MLSD") == 0 || strcmp(cmd, "RETR") == 0 ||
           strcmp(cmd, "STOR") == 0;
}

static void server_handle_command(ServerSession *session, const char *line,
                                  size_t len)
{
    (void)len;
    FtpServer *server = session->server;
    int index = server_session_index(server, session);

    char cmd[16];
    const char *space = strchr(line, ' ');
    size_t cmd_len = space != NULL ? (size_t)(space - line) : strlen(line);
    if (cmd_len == 0) {
        server_reply(session, 500, "Command not understood.");
        return;
    }
    if (session->tx_backlog && strncmp(line, "ABOR", 4) != 0 &&
        strncmp(line, "QUIT", 4) != 0) {
        server_trace(server, "FTPC DROPPED %s", line);
        return;
    }
    if (cmd_len >= sizeof(cmd)) {
        server_reply(session, 502, "Command not implemented.");
        return;
    }

    for (size_t i = 0; i < cmd_len; ++i) {
        char c = line[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        cmd[i] = c;
    }
    cmd[cmd_len] = '\0';

    snprintf(server->last_command, sizeof(server->last_command), "%s", cmd);
    if (strcmp(cmd, "PASS") == 0)
        server_trace(server, "FTPC PASS ****");
    else
        server_trace(server, "FTPC %s", line);

    const char *arg = space != NULL ? space + 1 : "";
    while (*arg == ' ')
        arg++;

    if (strcmp(cmd, "ABOR") == 0) {
        server_abort_command(session);
        return;
    }

    int transfer_active =
        server->data_state == FTP_DATA_ACTIVE || server->pending_accept;
    if (server->data_state == FTP_DATA_ARMED &&
        server->data_session != index && server_data_command_name(cmd)) {
        server_reply(session, 450, "Another client is transferring.");
        return;
    }
    if (transfer_active) {
        if (server->data_session != index) {
            /* Another client owns the data slot. Control-only commands stay
             * available so its browse connection can still navigate. */
            if (server_busy_command(cmd)) {
                server_reply(session, 450, "Another client is transferring.");
                return;
            }
        } else if (strcmp(cmd, "NOOP") != 0 && strcmp(cmd, "QUIT") != 0 &&
                   strcmp(cmd, "PWD") != 0 && strcmp(cmd, "SIZE") != 0 &&
                   strcmp(cmd, "MDTM") != 0 && strcmp(cmd, "MLST") != 0 &&
                   strcmp(cmd, "ABOR") != 0) {
            server_reply(session, 450, "Transfer in progress.");
            return;
        }
    }

    /* An armed PASV connection is invalidated by a non-data command from its
     * owner. A different session must not disarm it. */
    if (server->data_state == FTP_DATA_ARMED && !server->pending_accept &&
        server->data_session == index && !server_data_command_name(cmd) &&
        strcmp(cmd, "PASV") != 0 && strcmp(cmd, "ABOR") != 0) {
        server_disarm(server);
    }

    if (!session->logged_in && strcmp(cmd, "USER") != 0 &&
        strcmp(cmd, "PASS") != 0 && strcmp(cmd, "QUIT") != 0 &&
        strcmp(cmd, "SYST") != 0 && strcmp(cmd, "NOOP") != 0 &&
        strcmp(cmd, "FEAT") != 0) {
        server_reply(session, 530, "Please login with USER and PASS.");
        return;
    }

    if (strcmp(cmd, "USER") == 0) {
        session->user_seen = 1;
        session->user_ok = ci_equal(arg, "anonymous");
        session->logged_in = 0;
        if (session->user_ok)
            server_reply(session, 331, "Please specify the password.");
        else
            server_reply(session, 530, "Login incorrect.");
        return;
    }

    if (strcmp(cmd, "PASS") == 0) {
        if (!session->user_seen) {
            server_reply(session, 503, "Login with USER first.");
            return;
        }
        if (!session->user_ok) {
            server_reply(session, 530, "Login incorrect.");
            return;
        }
        session->logged_in = 1;
        server_reply(session, 230, "Login successful.");
        return;
    }

    if (strcmp(cmd, "SYST") == 0) {
        server_reply(session, 215, "UNIX Type: L8");
        return;
    }
    if (strcmp(cmd, "NOOP") == 0) {
        server_reply(session, 200, "OK.");
        return;
    }
    if (strcmp(cmd, "OPTS") == 0) {
        if (starts_with_ci(arg, "UTF8"))
            server_reply(session, 200, "OK.");
        else
            server_reply(session, 501, "Option not understood.");
        return;
    }
    if (strcmp(cmd, "FEAT") == 0) {
        server_feat(session);
        return;
    }
    if (strcmp(cmd, "QUIT") == 0) {
        server_close_session(server, session, 1, NULL);
        return;
    }
    if (strcmp(cmd, "PWD") == 0) {
        server_reply(session, 257, "\"%s\" is the current directory.",
                     session->cwd);
        return;
    }
    if (strcmp(cmd, "CWD") == 0) {
        if (server->data_state != FTP_DATA_NONE && server->data_session == index)
            server_disarm(server);
        server_handle_cwd(session, arg);
        return;
    }
    if (strcmp(cmd, "CDUP") == 0) {
        if (server->data_state != FTP_DATA_NONE && server->data_session == index)
            server_disarm(server);
        server_handle_cdup(session);
        return;
    }
    if (strcmp(cmd, "TYPE") == 0) {
        if (ci_equal(arg, "I") || ci_equal(arg, "A"))
            server_reply(session, 200, "Type set.");
        else
            server_reply(session, 504, "Type not supported.");
        return;
    }
    if (strcmp(cmd, "MODE") == 0) {
        if (ci_equal(arg, "S"))
            server_reply(session, 200, "Mode set.");
        else
            server_reply(session, 504, "Mode not supported.");
        return;
    }
    if (strcmp(cmd, "STRU") == 0) {
        if (ci_equal(arg, "F"))
            server_reply(session, 200, "Structure set.");
        else
            server_reply(session, 504, "Structure not supported.");
        return;
    }
    if (strcmp(cmd, "PASV") == 0) {
        server_pasv(session);
        return;
    }

    if (strcmp(cmd, "LIST") == 0) {
        if (server->data_state != FTP_DATA_ARMED && !server->pending_accept) {
            server_reply(session, 425, "Use PASV first.");
            return;
        }
        server_data_command(session, SDATA_LIST, arg);
        return;
    }
    if (strcmp(cmd, "NLST") == 0) {
        if (server->data_state != FTP_DATA_ARMED && !server->pending_accept) {
            server_reply(session, 425, "Use PASV first.");
            return;
        }
        server_data_command(session, SDATA_NLST, arg);
        return;
    }
    if (strcmp(cmd, "MLSD") == 0) {
        if (server->data_state != FTP_DATA_ARMED && !server->pending_accept) {
            server_reply(session, 425, "Use PASV first.");
            return;
        }
        server_data_command(session, SDATA_MLSD, arg);
        return;
    }
    if (strcmp(cmd, "RETR") == 0) {
        if (server->data_state != FTP_DATA_ARMED && !server->pending_accept) {
            server_reply(session, 425, "Use PASV first.");
            return;
        }
        server_data_command(session, SDATA_RETR, arg);
        return;
    }
    if (strcmp(cmd, "STOR") == 0) {
        if (server->data_state != FTP_DATA_ARMED && !server->pending_accept) {
            server_reply(session, 425, "Use PASV first.");
            return;
        }
        server_data_command(session, SDATA_STOR, arg);
        return;
    }

    if (strcmp(cmd, "MLST") == 0) {
        server_mlst(session, arg);
        return;
    }
    if (strcmp(cmd, "SIZE") == 0) {
        server_handle_size(session, arg);
        return;
    }
    if (strcmp(cmd, "MDTM") == 0) {
        server_handle_mdtm(session, arg);
        return;
    }
    if (strcmp(cmd, "MKD") == 0) {
        server_handle_mkd(session, arg);
        return;
    }
    if (strcmp(cmd, "DELE") == 0) {
        server_handle_dele(session, arg);
        return;
    }
    if (strcmp(cmd, "RMD") == 0) {
        server_handle_rmd(session, arg);
        return;
    }
    if (strcmp(cmd, "RNFR") == 0) {
        server_handle_rnfr(session, arg);
        return;
    }
    if (strcmp(cmd, "RNTO") == 0) {
        server_handle_rnto(session, arg);
        return;
    }
    if (strcmp(cmd, "PORT") == 0 || strcmp(cmd, "EPRT") == 0 ||
        strcmp(cmd, "REST") == 0 || strcmp(cmd, "APPE") == 0) {
        server_reply(session, 502, "Command not implemented.");
        return;
    }

    if (server_busy_command(cmd)) {
        server_reply(session, 450, "Transfer in progress.");
        return;
    }

    server_reply(session, 502, "Command not implemented.");
}

static void server_process_control(FtpServer *server, ServerSession *session)
{
    char buf[512];
    for (;;) {
        if (session == NULL || !session->used || session->control == NULL)
            return;

        size_t got = 0;
        NetResult r = server->net->receive(server->net->ctx, session->control,
                                           buf, sizeof(buf), &got);
        if (r == NET_WOULD_BLOCK)
            return;
        if (r == NET_CLOSED) {
            server_close_session(server, session, 0, "CLIENT CLOSED");
            return;
        }
        if (r != NET_OK) {
            server_close_session(server, session, 0,
                                 "CONTROL CONNECTION LOST");
            return;
        }
        if (got == 0)
            return;

        session->control_deadline = server_now(server) + SERVER_CONTROL_IDLE_MS;
        for (size_t i = 0; i < got; ++i) {
            FtpLineResult lr = ftp_line_feed(&session->lines, buf[i]);
            if (lr == FTP_LINE_READY)
                server_handle_command(session, session->lines.line,
                                      session->lines.len);
            else if (lr == FTP_LINE_OVERFLOW)
                server_reply(session, 500, "Command line too long.");

            if (!session->used || session->control == NULL)
                return;
        }
    }
}

static void server_accept_control(FtpServer *server)
{
    NetSocket *accepted = NULL;
    NetAddress peer;
    NetResult r = server->net->accept(server->net->ctx,
                                      server->control_listener, &accepted,
                                      &peer);
    if (r != NET_OK || accepted == NULL)
        return;

    int slot = -1;
    for (int i = 0; i < SERVER_MAX_CLIENTS; ++i) {
        if (!server->sessions[i].used) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        server_trace_peer(server, "FTPD CONTROL FULL", peer);
        server->state = FTP_SERVER_REFUSING;
        server_state_event(server);
        size_t sent = 0;
        const char *busy = "450 Busy: four clients at a time.\r\n";
        NetResult sr = server->net->send(server->net->ctx, accepted, busy,
                                         strlen(busy), &sent);
        if (sr == NET_WOULD_BLOCK)
            (void)server->net->send(server->net->ctx, accepted, busy,
                                    strlen(busy), &sent);
        server->net->close(server->net->ctx, accepted);
        if (server->clients > 0)
            server->state = FTP_SERVER_SESSION;
        server_state_event(server);
        return;
    }

    server_trace_peer(server, "FTPD CONTROL", peer);
    ServerSession *session = &server->sessions[slot];
    memset(session, 0, sizeof(*session));
    session->server = server;
    session->used = 1;
    session->control = accepted;
    session->peer = peer;
    session->cwd[0] = '/';
    session->cwd[1] = '\0';
    session->control_deadline = server_now(server) + SERVER_CONTROL_IDLE_MS;
    ftp_line_init(&session->lines);
    server->last_peer = peer;
    server->clients++;
    server->state = FTP_SERVER_SESSION;
    server_emit_state_and_client(server);
    server_reply(session, 220, "vitaFTPShell ready.");
}
FtpServer *ftp_server_create(NetPort *net, FsPort *fs, ClockPort *clock,
                             FtpServerEventFn emit, void *emit_ctx)
{
    if (net == NULL || fs == NULL || clock == NULL)
        return NULL;

    FtpServer *server = calloc(1, sizeof(*server));
    if (server == NULL)
        return NULL;

    server->net = net;
    server->fs = fs;
    server->clock = clock;
    server->emit = emit;
    server->emit_ctx = emit_ctx;
    server->state = FTP_SERVER_OFF;
    server->data_state = FTP_DATA_NONE;
    server->data_session = -1;
    return server;
}

void ftp_server_destroy(FtpServer *server)
{
    if (server == NULL)
        return;
    if (server->state != FTP_SERVER_OFF)
        ftp_server_stop(server);
    free(server);
}

FtpServerResult ftp_server_start(FtpServer *server, uint16_t control_port)
{
    if (server == NULL)
        return FTP_SERVER_INVALID;
    if (server->state != FTP_SERVER_OFF)
        return FTP_SERVER_BUSY;
    if (control_port < 1024 || control_port > 65534)
        return FTP_SERVER_INVALID;

    NetLink link;
    if (server->net->link(server->net->ctx, &link) != NET_OK || !link.up ||
        !link.address_known) {
        server->state = FTP_SERVER_OFF;
        server_state_event(server);
        snprintf(server->detail, sizeof(server->detail), "NO NETWORK");
        server_error_event(server, server->detail);
        return FTP_SERVER_NO_NETWORK;
    }

    server->lan_address = link.address;
    server->control_port = control_port;
    server->data_port = (uint16_t)(control_port + 1);
    server->state = FTP_SERVER_BINDING;
    server_state_event(server);

    NetAddress bind = link.address;
    bind.port = control_port;
    if (server->net->listen(server->net->ctx, &bind,
                            &server->control_listener) != NET_OK) {
        char detail[64];
        snprintf(detail, sizeof(detail), "BIND FAILED ON %u/%u",
                 (unsigned)control_port, (unsigned)server->data_port);
        server_stop_internal(server, detail);
        return FTP_SERVER_BIND_FAILED;
    }

    bind.port = server->data_port;
    if (server->net->listen(server->net->ctx, &bind,
                            &server->data_listener) != NET_OK) {
        char detail[64];
        snprintf(detail, sizeof(detail), "BIND FAILED ON %u/%u",
                 (unsigned)control_port, (unsigned)server->data_port);
        server_stop_internal(server, detail);
        return FTP_SERVER_BIND_FAILED;
    }

    server->state = FTP_SERVER_ON;
    server->clients = 0;
    server->next_link_check = server_now(server) + SERVER_LINK_CHECK_MS;
    server_state_event(server);
    return FTP_SERVER_OK;
}

void ftp_server_stop(FtpServer *server)
{
    if (server == NULL || server->state == FTP_SERVER_OFF)
        return;
    server->state = FTP_SERVER_STOPPING;
    server_state_event(server);
    server_stop_internal(server, NULL);
}

void ftp_server_tick(FtpServer *server, unsigned wait_ms)
{
    if (server == NULL || server->state == FTP_SERVER_OFF ||
        server->state == FTP_SERVER_BINDING ||
        server->state == FTP_SERVER_STOPPING)
        return;

    uint64_t now = server_now(server);
    server->data_budget = SERVER_DATA_BYTES_PER_TICK;
    server->data_slice_start = now;

    if (now >= server->next_link_check) {
        server->next_link_check = now + SERVER_LINK_CHECK_MS;
        NetLink link;
        if (server->net->link(server->net->ctx, &link) != NET_OK ||
            !link.up) {
            server_stop_internal(server, "NETWORK LOST");
            return;
        }
    }

    for (int i = 0; i < SERVER_MAX_CLIENTS; ++i) {
        ServerSession *session = &server->sessions[i];
        if (session->used)
            server_flush(session);
    }

    if (server->clients == 0) {
        if (server->control_listener == NULL)
            return;
        NetWaitSet set;
        memset(&set, 0, sizeof(set));
        set.count = 1;
        set.items[0].socket = server->control_listener;
        set.items[0].flags = NET_WAIT_READ;
        NetReadySet ready;
        if (server->net->wait(server->net->ctx, &set, wait_ms, &ready) ==
                NET_OK &&
            ready.count > 0)
            server_accept_control(server);
        for (int i = 0; i < SERVER_MAX_CLIENTS; ++i) {
            if (server->sessions[i].used)
                server_flush(&server->sessions[i]);
        }
        return;
    }

    NetWaitSet set;
    memset(&set, 0, sizeof(set));
    for (int i = 0; i < SERVER_MAX_CLIENTS; ++i) {
        ServerSession *session = &server->sessions[i];
        if (!session->used || session->control == NULL)
            continue;
        set.items[set.count].socket = session->control;
        set.items[set.count].flags = NET_WAIT_READ;
        set.count++;
    }
    if (server->data != NULL &&
        (server->data_state == FTP_DATA_ACTIVE ||
         server->data_state == FTP_DATA_ACCEPTED)) {
        set.items[set.count].socket = server->data;
        set.items[set.count].flags = server->data_kind == SDATA_STOR
                                         ? NET_WAIT_READ
                                         : NET_WAIT_WRITE;
        set.count++;
    }
    if (server->pending_accept && server->data_listener != NULL) {
        set.items[set.count].socket = server->data_listener;
        set.items[set.count].flags = NET_WAIT_READ;
        set.count++;
    }
    if (server->control_listener != NULL) {
        set.items[set.count].socket = server->control_listener;
        set.items[set.count].flags = NET_WAIT_READ;
        set.count++;
    }

    NetReadySet ready;
    NetResult wr = server->net->wait(server->net->ctx, &set, wait_ms, &ready);
    if (wr != NET_OK) {
        server_stop_internal(server, "WAIT FAILED");
        return;
    }

    /* Control first: an ABOR must be able to stop the data pump. */
    for (unsigned i = 0; i < ready.count; ++i) {
        NetSocket *socket = ready.items[i].socket;
        if (socket == server->control_listener || socket == server->data ||
            socket == server->data_listener)
            continue;
        for (int s = 0; s < SERVER_MAX_CLIENTS; ++s) {
            ServerSession *session = &server->sessions[s];
            if (session->used && session->control == socket) {
                server_process_control(server, session);
                break;
            }
        }
    }

    for (unsigned i = 0; i < ready.count; ++i) {
        if (ready.items[i].socket == server->control_listener)
            server_accept_control(server);
    }

    if (server->pending_accept && server->data_state == FTP_DATA_ARMED)
        server_try_accept(server);

    server_pump_data(server);

    now = server_now(server);
    for (int i = 0; i < SERVER_MAX_CLIENTS; ++i) {
        ServerSession *session = &server->sessions[i];
        if (!session->used)
            continue;
        int owns_data =
            (server->data_state == FTP_DATA_ACTIVE ||
             server->data_state == FTP_DATA_ACCEPTED) &&
            server->data_session == i;
        if (owns_data)
            session->control_deadline = now + SERVER_CONTROL_IDLE_MS;
        if (now >= session->control_deadline) {
            server_reply(session, 421, "Idle timeout.");
            server_flush(session);
            server_close_session(server, session, 0, "IDLE TIMEOUT");
        }
    }

    if (server->pending_accept && now >= server->pending_deadline) {
        ServerSession *owner = server_data_owner(server);
        server_trace(server, "FTPD ACCEPT TIMEOUT");
        if (owner != NULL)
            server_reply(owner, 425, "Cannot open data connection.");
        server_disarm(server);
    }
    if (server->data_state == FTP_DATA_ACTIVE && now >= server->data_deadline)
        server_data_error(server, 426, FTP_CAUSE_NETWORK, "DATA IDLE 15S");

    for (int i = 0; i < SERVER_MAX_CLIENTS; ++i) {
        if (server->sessions[i].used)
            server_flush(&server->sessions[i]);
    }
}

FtpServerState ftp_server_state(const FtpServer *server)
{
    return server != NULL ? server->state : FTP_SERVER_OFF;
}

int ftp_server_clients(const FtpServer *server)
{
    return server != NULL ? server->clients : 0;
}

int ftp_server_busy(const FtpServer *server)
{
    return server != NULL &&
           (server->data_state == FTP_DATA_ACTIVE || server->pending_accept);
}

uint16_t ftp_server_control_port(const FtpServer *server)
{
    return server != NULL ? server->control_port : 0;
}

NetAddress ftp_server_peer(const FtpServer *server)
{
    NetAddress none = {0, 0};
    return server != NULL ? server->last_peer : none;
}

const char *ftp_server_last_command(const FtpServer *server)
{
    return server != NULL ? server->last_command : "";
}

NetAddress ftp_server_lan_address(const FtpServer *server)
{
    NetAddress none = {0, 0};
    return server != NULL ? server->lan_address : none;
}

void ftp_server_set_guard(FtpServer *server, FtpServerGuardFn guard, void *ctx)
{
    if (server == NULL)
        return;
    server->guard = guard;
    server->guard_ctx = ctx;
}

void ftp_server_set_trace(FtpServer *server, FtpServerTraceFn trace,
                          void *ctx)
{
    if (server == NULL)
        return;
    server->trace = trace;
    server->trace_ctx = ctx;
}
