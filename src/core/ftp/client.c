/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/ftp/client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLIENT_RESOLVE_MS 3000
#define CLIENT_CONNECT_MS 3000
#define CLIENT_GREETING_LOGIN_MS 2000
#define CLIENT_FIRST_LIST_MS 2000
#define CLIENT_COMMAND_MS 5000
#define CLIENT_DATA_IDLE_MS 15000
#define CLIENT_ABORT_MS 5000
#define CLIENT_ABORT_GRACE_MS 250
#define CLIENT_TOTAL_BUDGET_MS 10000

#define CLIENT_SEND_MAX (FTP_PATH_MAX + 64)
#define CLIENT_DATA_BUF 4096

typedef enum {
    CSTEP_NONE = 0,
    CSTEP_GREETING,
    CSTEP_USER,
    CSTEP_PASS,
    CSTEP_TYPE,
    CSTEP_PWD,
    CSTEP_FEAT,
    CSTEP_PASV,
    CSTEP_DATA_CMD,
    CSTEP_CWD,
    CSTEP_MKD,
    CSTEP_RNFR,
    CSTEP_RNTO,
    CSTEP_DELE,
    CSTEP_RMD,
    CSTEP_QUIT
} ClientStep;

typedef enum {
    CLIST_MLSD = 0,
    CLIST_UNIX,
    CLIST_NLST
} ClientListKind;

typedef enum {
    CDATA_LIST = 0,
    CDATA_RETR,
    CDATA_STOR
} ClientDataKind;

struct FtpClient {
    NetPort *net;
    FsPort *fs;
    ClockPort *clock;
    FtpClientEventFn emit;
    void *emit_ctx;

    FtpClientState state;
    FtpStage stage;
    FtpDataState data_state;
    FtpCause cause;
    FtpClientOperation op;
    ClientStep step;
    ClientListKind list_kind;
    ClientDataKind data_kind;

    NetSocket *control;
    NetSocket *data;
    NetAddress resolved;
    NetAddress control_peer;
    NetAddress data_addr;

    FtpLineReader lines;
    ReplyReader replies;
    Reply reply;

    char send_buf[CLIENT_SEND_MAX];
    size_t send_len;
    size_t send_off;

    char host[128];
    uint16_t port;
    int anonymous;
    char username[64];
    char password[128];

    char cwd[FTP_PATH_MAX];
    char list_path[FTP_PATH_MAX];
    char pending_path[FTP_PATH_MAX];
    char rename_from[FTP_PATH_MAX];
    char rename_to[FTP_PATH_MAX];

    int has_mlsd;
    int list_fallback;
    int first_listing;
    int listing_open;
    int listing_rows;

    uint64_t session_start;
    uint64_t budget_deadline;
    int budget_active;
    uint64_t stage_deadline;
    uint64_t data_deadline;

    FsFile *file;
    int file_eof;
    FtpTempWrite temp;
    uint64_t transferred;
    uint64_t total;
    int total_known;

    char data_buf[CLIENT_DATA_BUF];
    size_t data_len;
    size_t data_off;

    FtpLineReader list_lines;

    int final_seen;
    int abor_drain;
    int abor_grace;
    uint64_t abor_deadline;
    uint64_t abor_grace_deadline;
    int op_active;
    int op_reported;

    char last_detail[FTP_REPLY_TEXT_MAX];
};

static uint64_t min_deadline(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (cap == 0)
        return;
    if (src == NULL)
        src = "";
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static uint64_t now_ms(FtpClient *client)
{
    return client->clock->now_ms(client->clock->ctx);
}

static void event_init(FtpClientEvent *event, FtpClientEventKind kind)
{
    memset(event, 0, sizeof(*event));
    event->kind = kind;
}

static void client_emit(FtpClient *client, const FtpClientEvent *event)
{
    if (client->emit != NULL)
        client->emit(client->emit_ctx, event);
}

static void client_state_event(FtpClient *client)
{
    FtpClientEvent event;
    event_init(&event, FTP_CLIENT_EVENT_STATE);
    event.state = client->state;
    event.data_state = client->data_state;
    event.stage = client->stage;
    event.cause = client->cause;
    event.operation = client->op;
    client_emit(client, &event);
}

static void client_set_state(FtpClient *client, FtpClientState state)
{
    if (client->state == state)
        return;
    client->state = state;
    client_state_event(client);
}

static void client_set_stage(FtpClient *client, FtpStage stage)
{
    client->stage = stage;
}

static void client_emit_progress(FtpClient *client)
{
    if (client->data_kind == CDATA_LIST)
        return;

    FtpClientEvent event;
    event_init(&event, FTP_CLIENT_EVENT_PROGRESS);
    event.state = client->state;
    event.data_state = client->data_state;
    event.stage = client->stage;
    event.operation = client->op;
    event.bytes = client->transferred;
    event.total = client->total;
    event.total_known = client->total_known;
    client_emit(client, &event);
}

static void client_emit_listing_begin(FtpClient *client)
{
    FtpClientEvent event;
    event_init(&event, FTP_CLIENT_EVENT_LISTING_BEGIN);
    event.state = client->state;
    event.stage = client->stage;
    event.operation = client->op;
    event.path = client->list_path;
    client_emit(client, &event);
}

static void client_emit_listing_row(FtpClient *client, const ListingEntry *row)
{
    FtpClientEvent event;
    event_init(&event, FTP_CLIENT_EVENT_LISTING_ROW);
    event.state = client->state;
    event.stage = client->stage;
    event.operation = client->op;
    event.path = client->list_path;
    event.row = row;
    client_emit(client, &event);
}

static void client_emit_listing_end(FtpClient *client, int ok)
{
    if (!client->listing_open)
        return;
    client->listing_open = 0;

    FtpClientEvent event;
    event_init(&event, FTP_CLIENT_EVENT_LISTING_END);
    event.state = client->state;
    event.stage = client->stage;
    event.operation = client->op;
    event.path = client->list_path;
    event.ok = ok;
    client_emit(client, &event);
}

static void client_emit_op_done(FtpClient *client, int ok, FtpCause cause,
                                FtpStage stage, const char *detail)
{
    if (!client->op_active || client->op_reported)
        return;
    client->op_reported = 1;

    FtpClientEvent event;
    event_init(&event, FTP_CLIENT_EVENT_OPERATION_DONE);
    event.state = client->state;
    event.data_state = client->data_state;
    event.stage = stage;
    event.cause = cause;
    event.operation = client->op;
    event.ok = ok;
    event.detail = detail;
    if (client->op == FTP_CLIENT_OP_LIST || client->op == FTP_CLIENT_OP_NLST)
        event.path = client->list_path;
    else if (client->op == FTP_CLIENT_OP_RENAME)
        event.path = client->rename_from;
    else if (client->op == FTP_CLIENT_OP_CONNECT)
        event.path = client->cwd;
    else
        event.path = client->pending_path;
    client_emit(client, &event);
}

static void client_close_data(FtpClient *client)
{
    if (client->data != NULL) {
        client->net->close(client->net->ctx, client->data);
        client->data = NULL;
    }
    client->data_state = FTP_DATA_NONE;
    client->data_len = 0;
    client->data_off = 0;
}

static void client_close_control(FtpClient *client)
{
    if (client->control != NULL) {
        client->net->close(client->net->ctx, client->control);
        client->control = NULL;
    }
}

static void client_abort_local(FtpClient *client)
{
    if (client->temp.open)
        ftp_temp_abort(client->fs, &client->temp);
    if (client->file != NULL) {
        client->fs->close(client->fs->ctx, client->file);
        client->file = NULL;
    }
    client->file_eof = 0;
}

static void client_clear_op(FtpClient *client)
{
    client->op_active = 0;
    client->op_reported = 0;
    client->step = CSTEP_NONE;
    client->stage_deadline = 0;
    client->data_deadline = 0;
    client->budget_active = 0;
    client->final_seen = 0;
    client->first_listing = 0;
    if (client->state == FTP_CLIENT_READY || client->state == FTP_CLIENT_IDLE)
        client->stage = FTP_STAGE_NONE;
}

static void client_enter_terminal(FtpClient *client, FtpClientState state,
                                  FtpCause cause, FtpStage stage,
                                  const char *detail)
{
    client_abort_local(client);
    client_close_data(client);
    client_close_control(client);
    client->cause = cause;
    client->stage = stage;
    client->budget_active = 0;
    if (detail != NULL)
        copy_str(client->last_detail, sizeof(client->last_detail), detail);
    client_set_state(client, state);
    client_emit_listing_end(client, 0);
    client_emit_op_done(client, 0, cause, stage,
                        detail != NULL ? detail : "FAILED");
    client_clear_op(client);
}

static void client_lost(FtpClient *client, const char *detail)
{
    if (client->state == FTP_CLIENT_LOST ||
        client->state == FTP_CLIENT_FAILED ||
        client->state == FTP_CLIENT_STALLED)
        return;
    client_enter_terminal(client, FTP_CLIENT_LOST, FTP_CAUSE_NETWORK,
                          client->stage, detail != NULL ? detail : "CONNECTION LOST");
}

static void client_stall(FtpClient *client, FtpStage stage, const char *detail)
{
    if (client->state == FTP_CLIENT_LOST ||
        client->state == FTP_CLIENT_FAILED ||
        client->state == FTP_CLIENT_STALLED)
        return;
    client_enter_terminal(client, FTP_CLIENT_STALLED, FTP_CAUSE_NETWORK, stage,
                          detail != NULL ? detail : "TIMEOUT");
}

static void client_command_fail(FtpClient *client, FtpCause cause,
                                FtpStage stage, const char *detail)
{
    client_abort_local(client);
    client_close_data(client);
    client->cause = cause;
    client->stage = stage;
    client_set_state(client, FTP_CLIENT_READY);
    client_emit_listing_end(client, 0);
    client_emit_op_done(client, 0, cause, stage,
                        detail != NULL ? detail : "FAILED");
    client_clear_op(client);
}

static int client_flush(FtpClient *client)
{
    if (client->control == NULL)
        return 0;

    while (client->send_off < client->send_len) {
        size_t sent = 0;
        NetResult r = client->net->send(
            client->net->ctx, client->control, client->send_buf + client->send_off,
            client->send_len - client->send_off, &sent);
        if (r == NET_WOULD_BLOCK)
            return 1;
        if (r != NET_OK || sent == 0)
            return 0;
        client->send_off += sent;
    }

    client->send_len = 0;
    client->send_off = 0;
    return 1;
}

static int client_send(FtpClient *client, const char *fmt, ...)
{
    if (client->control == NULL)
        return 0;
    if (client->send_off < client->send_len)
        return 0;

    char line[CLIENT_SEND_MAX];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(line) - 2)
        return 0;

    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = '\0';

    memcpy(client->send_buf, line, (size_t)n);
    client->send_len = (size_t)n;
    client->send_off = 0;

    if (!client_flush(client)) {
        client_lost(client, "CONTROL WRITE FAILED");
        return 0;
    }
    return 1;
}

static void client_fail_server(FtpClient *client, FtpStage stage,
                               const char *detail)
{
    if (client->state == FTP_CLIENT_GREETING ||
        client->state == FTP_CLIENT_LOGGING_IN ||
        (client->state == FTP_CLIENT_LISTING && client->first_listing)) {
        client_enter_terminal(client, FTP_CLIENT_FAILED, FTP_CAUSE_SERVER,
                              stage, detail);
        return;
    }
    client_command_fail(client, FTP_CAUSE_SERVER, stage, detail);
}

static void client_handle_abor_reply(FtpClient *client, const Reply *reply)
{
    uint64_t now = now_ms(client);

    if (reply->code == 426) {
        /* The 426 is the transfer's reply; keep waiting for the ABOR pair's
         * terminal reply. During the grace it is the tolerated other order. */
        if (client->abor_drain == 0) {
            client->abor_grace = 1;
            client->abor_grace_deadline = now + CLIENT_ABORT_GRACE_MS;
        }
        return;
    }

    if (reply->code == 225 || reply->code == 226) {
        client->abor_drain = 0;
        client->abor_grace = 1;
        client->abor_grace_deadline = now + CLIENT_ABORT_GRACE_MS;
        return;
    }

    /* A definitive refusal or an unexpected reply ends the drain; the next
     * reply can belong to a command again. */
    client->abor_drain = 0;
    client->abor_grace = 0;
}

static void client_begin_abort(FtpClient *client, FtpCause cause,
                               FtpStage stage, const char *detail)
{
    client_abort_local(client);
    client_close_data(client);
    client_emit_listing_end(client, 0);
    client->cause = cause;
    client->stage = stage;
    client_emit_op_done(client, 0, cause, stage,
                        detail != NULL ? detail : "ABORTED");
    client_clear_op(client);

    if (client->control == NULL) {
        client->state = FTP_CLIENT_LOST;
        client_state_event(client);
        return;
    }

    client_set_state(client, FTP_CLIENT_READY);
    if (!client_send(client, "ABOR")) {
        client_lost(client, "CONTROL WRITE FAILED");
        return;
    }
    client->abor_drain = 1;
    client->abor_grace = 0;
    client->abor_grace_deadline = 0;
    client->abor_deadline = now_ms(client) + CLIENT_ABORT_MS;
}

static int client_start_op(FtpClient *client, FtpClientOperation op)
{
    if (client->op_active || client->abor_drain > 0 || client->abor_grace)
        return 0;
    client->op = op;
    client->op_active = 1;
    client->op_reported = 0;
    client->cause = FTP_CAUSE_APP;
    return 1;
}

static void client_parse_pwd(FtpClient *client, const Reply *reply)
{
    char parsed[FTP_PATH_MAX];
    parsed[0] = '\0';

    const char *open = strchr(reply->text, '"');
    if (open != NULL) {
        const char *close = strchr(open + 1, '"');
        size_t n = close != NULL ? (size_t)(close - open - 1)
                                 : strlen(open + 1);
        if (n >= sizeof(parsed))
            n = sizeof(parsed) - 1;
        memcpy(parsed, open + 1, n);
        parsed[n] = '\0';
    } else if (reply->text[0] == '/') {
        copy_str(parsed, sizeof(parsed), reply->text);
    } else {
        copy_str(parsed, sizeof(parsed), "/");
    }

    if (!ftp_remote_normalize("/", parsed, client->cwd, sizeof(client->cwd)))
        copy_str(client->cwd, sizeof(client->cwd), "/");
}

static void client_emit_listing_line(FtpClient *client, const char *line,
                                     size_t len)
{
    if (len == 0)
        return;

    ListingEntry entry;
    ListingResult result;

    if (client->list_kind == CLIST_MLSD) {
        result = listing_parse_mlsd(line, &entry);
    } else if (client->list_kind == CLIST_NLST) {
        memset(&entry, 0, sizeof(entry));
        if (strcmp(line, ".") == 0 || strcmp(line, "..") == 0)
            return;
        if (len >= sizeof(entry.name))
            return; /* a truncated name would name a different file */
        memcpy(entry.name, line, len);
        entry.name[len] = '\0';
        entry.type = LISTING_FILE;
        entry.size_known = 0;
        result = LISTING_OK;
    } else {
        result = listing_parse_unix(line, &entry);
        if (result == LISTING_MALFORMED)
            result = listing_parse_dos(line, &entry);
    }

    if (result != LISTING_OK)
        return;

    client->listing_rows++;
    client_emit_listing_row(client, &entry);
}

static void client_feed_data_bytes(FtpClient *client, const char *bytes,
                                   size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        FtpLineResult r = ftp_line_feed(&client->list_lines, bytes[i]);
        if (r == FTP_LINE_READY)
            client_emit_listing_line(client, client->list_lines.line,
                                     client->list_lines.len);
    }
}

static void client_send_data_command(FtpClient *client)
{
    const char *verb = "LIST";
    if (client->data_kind == CDATA_RETR)
        verb = "RETR";
    else if (client->data_kind == CDATA_STOR)
        verb = "STOR";
    else if (client->list_kind == CLIST_MLSD)
        verb = "MLSD";
    else if (client->list_kind == CLIST_NLST)
        verb = "NLST";

    const char *path = client->data_kind == CDATA_LIST
                           ? client->list_path
                           : client->pending_path;
    client_send(client, "%s %s", verb, path);
    client->step = CSTEP_DATA_CMD;
}

static void client_start_pasv(FtpClient *client)
{
    client_close_data(client);
    client->data_state = FTP_DATA_ARMED;
    client->final_seen = 0;
    if (!client_send(client, "PASV")) {
        client_lost(client, "CONTROL WRITE FAILED");
        return;
    }
    client->step = CSTEP_PASV;
}

static void client_begin_listing(FtpClient *client, int first)
{
    client->first_listing = first;
    client->listing_open = 1;
    client->listing_rows = 0;
    ftp_line_init(&client->list_lines);
    if (first)
        client_set_stage(client, FTP_STAGE_FIRST_LISTING);
    else
        client_set_stage(client, FTP_STAGE_COMMAND);
    client_set_state(client, FTP_CLIENT_LISTING);
    client_emit_listing_begin(client);
    client_start_pasv(client);
}

static void client_start_first_listing(FtpClient *client)
{
    client->list_kind = client->has_mlsd ? CLIST_MLSD : CLIST_UNIX;
    client->list_fallback = client->has_mlsd;
    copy_str(client->list_path, sizeof(client->list_path), client->cwd);
    client->stage_deadline =
        min_deadline(now_ms(client) + CLIENT_FIRST_LIST_MS,
                     client->budget_deadline);
    client_begin_listing(client, 1);
}

static void client_complete_data(FtpClient *client, int ok, FtpCause cause,
                                 FtpStage stage, const char *detail)
{
    client_close_data(client);

    if (client->data_kind == CDATA_LIST) {
        if (client->first_listing) {
            if (ok) {
                client->budget_active = 0;
                client_emit_listing_end(client, 1);
                client_set_state(client, FTP_CLIENT_READY);
                client->cause = FTP_CAUSE_APP;
                client_emit_op_done(client, 1, FTP_CAUSE_APP, stage, NULL);
                client_clear_op(client);
            } else {
                client_enter_terminal(client, FTP_CLIENT_FAILED, cause, stage,
                                      detail);
            }
            return;
        }

        client_emit_listing_end(client, ok);
        if (ok) {
            client_set_state(client, FTP_CLIENT_READY);
            client->cause = FTP_CAUSE_APP;
            client_emit_op_done(client, 1, FTP_CAUSE_APP, stage, NULL);
            client_clear_op(client);
        } else {
            client_command_fail(client, cause, stage, detail);
        }
        return;
    }

    if (ok) {
        if (client->data_kind == CDATA_RETR) {
            FsResult r = ftp_temp_finish(client->fs, &client->temp);
            if (r != FS_OK) {
                client_command_fail(client, FTP_CAUSE_STORAGE,
                                    FTP_STAGE_TRANSFER,
                                    "DESTINATION COMMIT FAILED");
                return;
            }
        } else if (client->file != NULL) {
            client->fs->close(client->fs->ctx, client->file);
            client->file = NULL;
            client->file_eof = 0;
        }
        client_set_state(client, FTP_CLIENT_READY);
        client_emit_op_done(client, 1, FTP_CAUSE_APP, stage, NULL);
        client_clear_op(client);
        return;
    }

    client_abort_local(client);
    client_command_fail(client, cause, stage, detail);
}

static void client_data_failure(FtpClient *client, FtpCause cause,
                                const char *detail)
{
    FtpStage stage =
        client->data_kind == CDATA_LIST ? FTP_STAGE_DATA : FTP_STAGE_TRANSFER;
    client_begin_abort(client, cause, stage, detail);
}

static void client_protocol_error(FtpClient *client, const char *detail)
{
    if (client->step == CSTEP_DATA_CMD &&
        client->data_state != FTP_DATA_NONE)
        client_data_failure(client, FTP_CAUSE_SERVER, detail);
    else
        client_fail_server(client, client->stage, detail);
}

static int client_write_all(FsPort *fs, FsFile *file, const char *data,
                            size_t len)
{
    size_t total = 0;
    while (total < len) {
        size_t written = 0;
        FsResult r = fs->write(fs->ctx, file, data + total, len - total,
                               &written);
        if (r != FS_OK || written == 0)
            return 0;
        total += written;
    }
    return 1;
}

static void client_pump_receive(FtpClient *client)
{
    char buf[2048];
    size_t got = 0;
    NetResult r = client->net->receive(client->net->ctx, client->data, buf,
                                       sizeof(buf), &got);
    if (r == NET_WOULD_BLOCK)
        return;
    if (r == NET_CLOSED || (r == NET_OK && got == 0)) {
        client_close_data(client);
        client->data_state = FTP_DATA_DRAINING;
        if (client->final_seen)
            client_complete_data(client, 1, FTP_CAUSE_APP,
                                 client->stage, NULL);
        return;
    }
    if (r != NET_OK) {
        client_data_failure(client, FTP_CAUSE_NETWORK, "DATA CONNECTION LOST");
        return;
    }

    client->data_deadline = now_ms(client) + CLIENT_DATA_IDLE_MS;
    client->transferred += got;

    if (client->first_listing && client->stage_deadline != 0) {
        /* The first listing budget ends when rows actually start arriving;
         * after this the 15 s data-idle deadline governs. */
        client->stage_deadline = 0;
        client->budget_active = 0;
        client_set_stage(client, FTP_STAGE_DATA);
    }

    if (client->data_kind == CDATA_LIST) {
        client_feed_data_bytes(client, buf, got);
    } else if (client->data_kind == CDATA_RETR) {
        if (client->temp.file == NULL ||
            !client_write_all(client->fs, client->temp.file, buf, got)) {
            client_data_failure(client, FTP_CAUSE_STORAGE,
                                "DESTINATION WRITE FAILED");
            return;
        }
    }

    client_emit_progress(client);
}

static void client_pump_send(FtpClient *client)
{
    if (client->data_off < client->data_len) {
        size_t sent = 0;
        NetResult r = client->net->send(
            client->net->ctx, client->data, client->data_buf + client->data_off,
            client->data_len - client->data_off, &sent);
        if (r == NET_WOULD_BLOCK)
            return;
        if (r != NET_OK || sent == 0) {
            client_data_failure(client, FTP_CAUSE_NETWORK,
                                "DATA CONNECTION LOST");
            return;
        }
        client->data_off += sent;
        client->transferred += sent;
        client->data_deadline = now_ms(client) + CLIENT_DATA_IDLE_MS;
        client_emit_progress(client);
        return;
    }

    if (client->file_eof) {
        client_close_data(client);
        client->data_state = FTP_DATA_DRAINING;
        return;
    }

    if (client->file == NULL) {
        client_data_failure(client, FTP_CAUSE_STORAGE, "SOURCE FILE MISSING");
        return;
    }

    size_t got = 0;
    FsResult r = client->fs->read(client->fs->ctx, client->file,
                                  client->data_buf, sizeof(client->data_buf),
                                  &got);
    if (r == FS_END) {
        client->file_eof = 1;
        client_close_data(client);
        client->data_state = FTP_DATA_DRAINING;
        return;
    }
    if (r != FS_OK) {
        client_data_failure(client, FTP_CAUSE_STORAGE, "SOURCE READ FAILED");
        return;
    }
    if (got == 0) {
        client->file_eof = 1;
        client_close_data(client);
        client->data_state = FTP_DATA_DRAINING;
        return;
    }

    client->data_len = got;
    client->data_off = 0;
    client->data_deadline = now_ms(client) + CLIENT_DATA_IDLE_MS;
    client_pump_send(client);
}

static void client_pump_data(FtpClient *client)
{
    if (client->data_state != FTP_DATA_ACTIVE || client->data == NULL)
        return;
    if (client->data_kind == CDATA_STOR)
        client_pump_send(client);
    else
        client_pump_receive(client);
}

static void client_handle_reply(FtpClient *client, const Reply *reply)
{
    if (client->abor_drain > 0) {
        client_handle_abor_reply(client, reply);
        return;
    }

    if (client->abor_grace) {
        if (reply->code == 426 || reply->code == 225 || reply->code == 226) {
            client->abor_grace_deadline =
                now_ms(client) + CLIENT_ABORT_GRACE_MS;
            return;
        }
        if (reply->code == 421) {
            client_lost(client, "SERVER CLOSED THE CONTROL CHANNEL");
            return;
        }
        client->abor_grace = 0;
        return;
    }

    if (client->step == CSTEP_NONE) {
        /* A late reply after a completed operation must not fail a healthy
         * session; 421 is the server saying it is closing. */
        if (reply->code == 421)
            client_lost(client, "SERVER CLOSED THE CONTROL CHANNEL");
        return;
    }

    switch (client->step) {
    case CSTEP_GREETING:
        if (reply->code == 120)
            return;
        if (reply->code == 220) {
            client_set_stage(client, FTP_STAGE_LOGIN);
            client_set_state(client, FTP_CLIENT_LOGGING_IN);
            client->step = CSTEP_USER;
            if (client->anonymous)
                client_send(client, "USER anonymous");
            else
                client_send(client, "USER %s", client->username);
            return;
        }
        client_fail_server(client, FTP_STAGE_GREETING, reply->text);
        return;

    case CSTEP_USER:
        if (reply->code == 230) {
            client->step = CSTEP_TYPE;
            client_send(client, "TYPE I");
            return;
        }
        if (reply->code == 331) {
            client->step = CSTEP_PASS;
            if (client->anonymous)
                client_send(client, "PASS anonymous@");
            else
                client_send(client, "PASS %s", client->password);
            return;
        }
        client_fail_server(client, FTP_STAGE_LOGIN, reply->text);
        return;

    case CSTEP_PASS:
        if (reply->code == 230) {
            client->step = CSTEP_TYPE;
            client_send(client, "TYPE I");
            return;
        }
        client_fail_server(client, FTP_STAGE_LOGIN, reply->text);
        return;

    case CSTEP_TYPE:
        if (reply->code >= 200 && reply->code < 300) {
            client->step = CSTEP_PWD;
            client_send(client, "PWD");
            return;
        }
        client_fail_server(client, FTP_STAGE_LOGIN, reply->text);
        return;

    case CSTEP_PWD:
        if (reply->code == 257)
            client_parse_pwd(client, reply);
        client->step = CSTEP_FEAT;
        client_send(client, "FEAT");
        return;

    case CSTEP_FEAT:
        client->has_mlsd =
            reply->code == 211 &&
            (ftp_text_contains(reply->body, "MLST") ||
             ftp_text_contains(reply->text, "MLST"));
        client_start_first_listing(client);
        return;

    case CSTEP_PASV: {
        if (reply->code != 227) {
            client_fail_server(client, FTP_STAGE_DATA, reply->text);
            return;
        }
        ReplyPasvResult pr =
            reply_parse_pasv(reply->text, &client->control_peer,
                             &client->data_addr);
        if (pr != REPLY_PASV_OK) {
            client_fail_server(client, FTP_STAGE_DATA, "PASSIVE REFUSED");
            return;
        }
        client->data_state = FTP_DATA_ARMED;
        NetResult nr = client->net->connect(client->net->ctx,
                                            &client->data_addr,
                                            CLIENT_CONNECT_MS, &client->data);
        if (nr != NET_OK) {
            client->data = NULL;
            client->data_state = FTP_DATA_NONE;
            client_command_fail(client, FTP_CAUSE_NETWORK, FTP_STAGE_DATA,
                                "DATA CONNECT FAILED");
            return;
        }
        client->data_state = FTP_DATA_ACCEPTED;
        client_send_data_command(client);
        return;
    }

    case CSTEP_DATA_CMD:
        if (reply->code == 125 || reply->code == 150) {
            client->data_state = FTP_DATA_ACTIVE;
            client->data_deadline = now_ms(client) + CLIENT_DATA_IDLE_MS;
            client->final_seen = 0;
            if (!client->first_listing) {
                client->stage_deadline = 0;
                client_set_stage(client, client->data_kind == CDATA_LIST
                                               ? FTP_STAGE_DATA
                                               : FTP_STAGE_TRANSFER);
            }
            client_emit_progress(client);
            return;
        }

        if (reply->code == 226 || reply->code == 250) {
            if (client->data_kind == CDATA_STOR) {
                if (client->data_state == FTP_DATA_DRAINING) {
                    client_complete_data(client, 1, FTP_CAUSE_APP,
                                         FTP_STAGE_TRANSFER, NULL);
                } else {
                    client_complete_data(client, 0, FTP_CAUSE_SERVER,
                                         FTP_STAGE_TRANSFER,
                                         "SERVER CLOSED BEFORE DATA ENDED");
                }
                return;
            }
            client->final_seen = 1;
            if (client->data_state != FTP_DATA_ACTIVE)
                client_complete_data(client, 1, FTP_CAUSE_APP,
                                     client->stage, NULL);
            return;
        }

        if (reply->code >= 400) {
            int can_fallback =
                client->data_kind == CDATA_LIST &&
                client->list_kind == CLIST_MLSD && client->list_fallback &&
                client->listing_rows == 0;
            client_close_data(client);
            client->data_state = FTP_DATA_NONE;

            if (can_fallback) {
                client->list_fallback = 0;
                client->list_kind = CLIST_UNIX;
                client->listing_rows = 0;
                ftp_line_init(&client->list_lines);
                client_start_pasv(client);
                return;
            }

            if (client->first_listing) {
                client_emit_listing_end(client, 0);
                client_enter_terminal(client, FTP_CLIENT_FAILED,
                                      FTP_CAUSE_SERVER, client->stage,
                                      reply->text);
                return;
            }

            client_command_fail(client, FTP_CAUSE_SERVER, client->stage,
                                reply->text);
            return;
        }

        /* Any other 1xx/2xx keeps waiting for the final reply. */
        return;

    case CSTEP_CWD:
        /* Handled by client_handle_cwd_reply so the success path can update
         * the directory; this case exists only to keep the switch exhaustive. */
        client_fail_server(client, FTP_STAGE_COMMAND, "UNEXPECTED CWD REPLY");
        return;

    case CSTEP_MKD:
    case CSTEP_DELE:
    case CSTEP_RMD:
        if (reply->code >= 200 && reply->code < 300) {
            client_set_state(client, FTP_CLIENT_READY);
            client_emit_op_done(client, 1, FTP_CAUSE_APP, FTP_STAGE_COMMAND,
                                NULL);
            client_clear_op(client);
            return;
        }
        client_fail_server(client, FTP_STAGE_COMMAND, reply->text);
        return;

    case CSTEP_RNFR:
        if (reply->code == 350 || (reply->code >= 200 && reply->code < 300)) {
            client_send(client, "RNTO %s", client->rename_to);
            client->step = CSTEP_RNTO;
            return;
        }
        client_fail_server(client, FTP_STAGE_COMMAND, reply->text);
        return;

    case CSTEP_RNTO:
        if (reply->code >= 200 && reply->code < 300) {
            client_set_state(client, FTP_CLIENT_READY);
            client_emit_op_done(client, 1, FTP_CAUSE_APP, FTP_STAGE_COMMAND,
                                NULL);
            client_clear_op(client);
            return;
        }
        client_fail_server(client, FTP_STAGE_COMMAND, reply->text);
        return;

    case CSTEP_QUIT:
        if (reply->code >= 200 && reply->code < 300) {
            client_close_control(client);
            client_set_state(client, FTP_CLIENT_IDLE);
            client_emit_op_done(client, 1, FTP_CAUSE_APP, FTP_STAGE_COMMAND,
                                NULL);
            client_clear_op(client);
        } else {
            client_command_fail(client, FTP_CAUSE_SERVER, FTP_STAGE_COMMAND,
                                reply->text);
        }
        return;

    default:
        client_fail_server(client, client->stage, "UNEXPECTED REPLY");
        return;
    }
}

/*
 * CWD success needs its own path because the generic command handler cannot
 * express "update the directory". Keep it separate from the failure helper.
 */
static void client_cwd_success(FtpClient *client)
{
    copy_str(client->cwd, sizeof(client->cwd), client->pending_path);
    client_set_state(client, FTP_CLIENT_READY);
    client_emit_op_done(client, 1, FTP_CAUSE_APP, FTP_STAGE_COMMAND, NULL);
    client_clear_op(client);
}

static void client_handle_cwd_reply(FtpClient *client, const Reply *reply)
{
    if (reply->code >= 200 && reply->code < 300) {
        client_cwd_success(client);
        return;
    }
    client_fail_server(client, FTP_STAGE_COMMAND, reply->text);
}

static void client_handle_line(FtpClient *client, const char *line, size_t len)
{
    Reply reply;
    ReplyResult r = reply_reader_line(&client->replies, line, len, &reply);
    if (r == REPLY_OK) {
        client->reply = reply;
        if (client->step == CSTEP_CWD) {
            if (client->abor_drain == 0)
                client_handle_cwd_reply(client, &reply);
            else
                client_handle_abor_reply(client, &reply);
            return;
        }
        client_handle_reply(client, &reply);
        return;
    }
    if (r == REPLY_MALFORMED || r == REPLY_TOO_LONG)
        client_protocol_error(client, "MALFORMED REPLY");
}

static void client_receive_control(FtpClient *client)
{
    char buf[512];

    for (;;) {
        if (client->control == NULL)
            return;

        size_t got = 0;
        NetResult r = client->net->receive(client->net->ctx, client->control,
                                           buf, sizeof(buf), &got);
        if (r == NET_WOULD_BLOCK)
            return;
        if (r == NET_CLOSED) {
            client_lost(client, "CONTROL CONNECTION LOST");
            return;
        }
        if (r != NET_OK) {
            client_lost(client, "CONTROL CONNECTION LOST");
            return;
        }
        if (got == 0)
            return;

        for (size_t i = 0; i < got; ++i) {
            FtpLineResult lr = ftp_line_feed(&client->lines, buf[i]);
            if (lr == FTP_LINE_READY)
                client_handle_line(client, client->lines.line,
                                   client->lines.len);
            else if (lr == FTP_LINE_OVERFLOW)
                client_protocol_error(client, "REPLY LINE TOO LONG");

            if (client->state == FTP_CLIENT_LOST ||
                client->state == FTP_CLIENT_FAILED ||
                client->state == FTP_CLIENT_STALLED ||
                client->state == FTP_CLIENT_IDLE)
                return;
        }
    }
}

static void client_reset_session(FtpClient *client)
{
    client_abort_local(client);
    client_close_data(client);
    client_close_control(client);
    client->step = CSTEP_NONE;
    client->state = FTP_CLIENT_IDLE;
    client->stage = FTP_STAGE_NONE;
    client->data_state = FTP_DATA_NONE;
    client->cause = FTP_CAUSE_APP;
    client->op = FTP_CLIENT_OP_CONNECT;
    client->op_active = 0;
    client->op_reported = 0;
    client->budget_active = 0;
    client->budget_deadline = 0;
    client->stage_deadline = 0;
    client->data_deadline = 0;
    client->abor_drain = 0;
    client->abor_grace = 0;
    client->abor_grace_deadline = 0;
    client->final_seen = 0;
    client->listing_open = 0;
    client->listing_rows = 0;
    client->has_mlsd = 0;
    client->list_fallback = 0;
    client->first_listing = 0;
    client->transferred = 0;
    client->total = 0;
    client->total_known = 0;
    client->file_eof = 0;
    ftp_line_init(&client->lines);
    ftp_line_init(&client->list_lines);
    reply_reader_init(&client->replies);
    memset(&client->reply, 0, sizeof(client->reply));
    client->last_detail[0] = '\0';
    copy_str(client->cwd, sizeof(client->cwd), "/");
}

FtpClient *ftp_client_create(NetPort *net, FsPort *fs, ClockPort *clock,
                             FtpClientEventFn emit, void *emit_ctx)
{
    if (net == NULL || fs == NULL || clock == NULL)
        return NULL;

    FtpClient *client = calloc(1, sizeof(*client));
    if (client == NULL)
        return NULL;

    client->net = net;
    client->fs = fs;
    client->clock = clock;
    client->emit = emit;
    client->emit_ctx = emit_ctx;
    client_reset_session(client);
    return client;
}

void ftp_client_destroy(FtpClient *client)
{
    if (client == NULL)
        return;
    client_abort_local(client);
    client_close_data(client);
    client_close_control(client);
    free(client);
}

FtpClientResult ftp_client_connect(FtpClient *client,
                                   const FtpClientLogin *login)
{
    if (client == NULL || login == NULL || login->host == NULL ||
        login->host[0] == '\0' || login->port == 0)
        return FTP_CLIENT_INVALID;
    if (strlen(login->host) >= sizeof(client->host) ||
        (login->username != NULL &&
         strlen(login->username) >= sizeof(client->username)) ||
        (login->password != NULL &&
         strlen(login->password) >= sizeof(client->password)))
        return FTP_CLIENT_INVALID;

    if (client->op_active || client->abor_drain > 0)
        return FTP_CLIENT_BUSY;
    if (client->state != FTP_CLIENT_IDLE &&
        client->state != FTP_CLIENT_LOST &&
        client->state != FTP_CLIENT_FAILED &&
        client->state != FTP_CLIENT_STALLED)
        return FTP_CLIENT_BUSY;

    client_reset_session(client);
    copy_str(client->host, sizeof(client->host), login->host);
    client->port = login->port;
    client->anonymous = login->anonymous;
    copy_str(client->username, sizeof(client->username), login->username);
    copy_str(client->password, sizeof(client->password), login->password);
    client->op = FTP_CLIENT_OP_CONNECT;
    client->op_active = 1;
    client->op_reported = 0;
    client->session_start = now_ms(client);
    client->budget_deadline = client->session_start + CLIENT_TOTAL_BUDGET_MS;
    client->budget_active = 1;
    client->stage = FTP_STAGE_RESOLVE;
    client->stage_deadline =
        min_deadline(client->session_start + CLIENT_RESOLVE_MS,
                     client->budget_deadline);
    client_set_state(client, FTP_CLIENT_RESOLVING);

    NetAddress address;
    NetResult r = client->net->resolve(client->net->ctx, client->host,
                                       CLIENT_RESOLVE_MS, &address);
    if (r != NET_OK) {
        client_enter_terminal(client,
                              r == NET_TIMEOUT ? FTP_CLIENT_STALLED
                                               : FTP_CLIENT_FAILED,
                              FTP_CAUSE_NETWORK, FTP_STAGE_RESOLVE,
                              r == NET_TIMEOUT ? "RESOLVE TIMEOUT"
                                               : "RESOLVE FAILED");
        return FTP_CLIENT_OK;
    }

    address.port = client->port;
    client->resolved = address;
    client->stage = FTP_STAGE_CONNECT;
    client->stage_deadline =
        min_deadline(now_ms(client) + CLIENT_CONNECT_MS,
                     client->budget_deadline);
    client_set_state(client, FTP_CLIENT_CONNECTING);

    r = client->net->connect(client->net->ctx, &address, CLIENT_CONNECT_MS,
                             &client->control);
    if (r != NET_OK) {
        client->control = NULL;
        client_enter_terminal(client,
                              r == NET_TIMEOUT ? FTP_CLIENT_STALLED
                                               : FTP_CLIENT_FAILED,
                              FTP_CAUSE_NETWORK, FTP_STAGE_CONNECT,
                              r == NET_TIMEOUT ? "CONNECT TIMEOUT"
                                               : "CONNECT FAILED");
        return FTP_CLIENT_OK;
    }

    client->control_peer = address;
    client->stage = FTP_STAGE_GREETING;
    client->stage_deadline =
        min_deadline(now_ms(client) + CLIENT_GREETING_LOGIN_MS,
                     client->budget_deadline);
    client->step = CSTEP_GREETING;
    client_set_state(client, FTP_CLIENT_GREETING);
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_list(FtpClient *client, const char *path)
{
    if (client == NULL)
        return FTP_CLIENT_INVALID;
    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, FTP_CLIENT_OP_LIST))
        return FTP_CLIENT_BUSY;
    if (!ftp_remote_normalize(client->cwd, path, client->list_path,
                              sizeof(client->list_path)) ||
        !ftp_remote_path_is_safe(client->list_path)) {
        client_clear_op(client);
        return FTP_CLIENT_INVALID;
    }

    client->list_kind = client->has_mlsd ? CLIST_MLSD : CLIST_UNIX;
    client->list_fallback = client->has_mlsd;
    client->stage = FTP_STAGE_COMMAND;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client_begin_listing(client, 0);
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_nlst(FtpClient *client, const char *path)
{
    if (client == NULL)
        return FTP_CLIENT_INVALID;
    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, FTP_CLIENT_OP_NLST))
        return FTP_CLIENT_BUSY;
    if (!ftp_remote_normalize(client->cwd, path, client->list_path,
                              sizeof(client->list_path)) ||
        !ftp_remote_path_is_safe(client->list_path)) {
        client_clear_op(client);
        return FTP_CLIENT_INVALID;
    }

    client->list_kind = CLIST_NLST;
    client->list_fallback = 0;
    client->stage = FTP_STAGE_COMMAND;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client_begin_listing(client, 0);
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_cwd(FtpClient *client, const char *path)
{
    if (client == NULL)
        return FTP_CLIENT_INVALID;
    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, FTP_CLIENT_OP_CWD))
        return FTP_CLIENT_BUSY;
    if (!ftp_remote_normalize(client->cwd, path, client->pending_path,
                              sizeof(client->pending_path)) ||
        !ftp_remote_path_is_safe(client->pending_path)) {
        client_clear_op(client);
        return FTP_CLIENT_INVALID;
    }

    client->stage = FTP_STAGE_COMMAND;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client->step = CSTEP_CWD;
    client_send(client, "CWD %s", client->pending_path);
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_download(FtpClient *client,
                                    const FtpClientTransfer *transfer,
                                    PathRisk *out_risk)
{
    if (client == NULL || transfer == NULL || transfer->remote_path == NULL ||
        transfer->local_path == NULL)
        return FTP_CLIENT_INVALID;

    if (out_risk != NULL)
        *out_risk = PATH_RISK_NORMAL;

    PathRisk risk = PATH_RISK_NORMAL;
    PathResult pr = path_check_write(transfer->local_path, 0, &risk);
    if (pr != PATH_OK)
        return FTP_CLIENT_DENIED;
    if (out_risk != NULL)
        *out_risk = risk;
    if (risk == PATH_RISK_HIGH && !transfer->risk_approved)
        return FTP_CLIENT_NEED_RISK;

    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, FTP_CLIENT_OP_DOWNLOAD))
        return FTP_CLIENT_BUSY;

    if (!ftp_remote_normalize(client->cwd, transfer->remote_path,
                              client->pending_path,
                              sizeof(client->pending_path)) ||
        !ftp_remote_path_is_safe(client->pending_path)) {
        client_clear_op(client);
        return FTP_CLIENT_INVALID;
    }

    FsResult tr = ftp_temp_open(client->fs, transfer->local_path,
                                &client->temp);
    if (tr != FS_OK) {
        client->cause = FTP_CAUSE_STORAGE;
        client_set_state(client, FTP_CLIENT_READY);
        client_emit_op_done(client, 0, FTP_CAUSE_STORAGE,
                            FTP_STAGE_TRANSFER, "DESTINATION OPEN FAILED");
        client_clear_op(client);
        return FTP_CLIENT_OK;
    }

    client->data_kind = CDATA_RETR;
    client->transferred = 0;
    client->total = 0;
    client->total_known = 0;
    client->stage = FTP_STAGE_TRANSFER;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client_set_state(client, FTP_CLIENT_TRANSFERRING);
    client_emit_progress(client);
    client_start_pasv(client);
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_upload(FtpClient *client,
                                  const FtpClientTransfer *transfer)
{
    if (client == NULL || transfer == NULL || transfer->remote_path == NULL ||
        transfer->local_path == NULL)
        return FTP_CLIENT_INVALID;

    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, FTP_CLIENT_OP_UPLOAD))
        return FTP_CLIENT_BUSY;

    if (!ftp_remote_normalize(client->cwd, transfer->remote_path,
                              client->pending_path,
                              sizeof(client->pending_path)) ||
        !ftp_remote_path_is_safe(client->pending_path)) {
        client_clear_op(client);
        return FTP_CLIENT_INVALID;
    }

    FsResult r = client->fs->open(client->fs->ctx, transfer->local_path,
                                  FS_OPEN_READ, &client->file);
    if (r != FS_OK) {
        client->cause = FTP_CAUSE_STORAGE;
        client_set_state(client, FTP_CLIENT_READY);
        client_emit_op_done(client, 0, FTP_CAUSE_STORAGE,
                            FTP_STAGE_TRANSFER, "SOURCE OPEN FAILED");
        client_clear_op(client);
        return FTP_CLIENT_OK;
    }

    FsEntry entry;
    if (client->fs->stat(client->fs->ctx, transfer->local_path, &entry) ==
            FS_OK &&
        entry.type == FS_ENTRY_FILE && entry.size_known) {
        client->total = entry.size;
        client->total_known = 1;
    } else {
        client->total = 0;
        client->total_known = 0;
    }

    client->data_kind = CDATA_STOR;
    client->transferred = 0;
    client->file_eof = 0;
    client->data_len = 0;
    client->data_off = 0;
    client->stage = FTP_STAGE_TRANSFER;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client_set_state(client, FTP_CLIENT_TRANSFERRING);
    client_emit_progress(client);
    client_start_pasv(client);
    return FTP_CLIENT_OK;
}

static FtpClientResult client_simple_command(FtpClient *client,
                                             FtpClientOperation op,
                                             const char *path,
                                             ClientStep step,
                                             const char *verb)
{
    if (client == NULL)
        return FTP_CLIENT_INVALID;
    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, op))
        return FTP_CLIENT_BUSY;
    if (!ftp_remote_normalize(client->cwd, path, client->pending_path,
                              sizeof(client->pending_path)) ||
        !ftp_remote_path_is_safe(client->pending_path)) {
        client_clear_op(client);
        return FTP_CLIENT_INVALID;
    }

    client->stage = FTP_STAGE_COMMAND;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client->step = step;
    client_send(client, "%s %s", verb, client->pending_path);
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_mkdir(FtpClient *client, const char *path)
{
    return client_simple_command(client, FTP_CLIENT_OP_MKDIR, path, CSTEP_MKD,
                                 "MKD");
}

FtpClientResult ftp_client_delete(FtpClient *client, const char *path)
{
    return client_simple_command(client, FTP_CLIENT_OP_DELETE, path,
                                 CSTEP_DELE, "DELE");
}

FtpClientResult ftp_client_rmdir(FtpClient *client, const char *path)
{
    return client_simple_command(client, FTP_CLIENT_OP_RMDIR, path, CSTEP_RMD,
                                 "RMD");
}

FtpClientResult ftp_client_rename(FtpClient *client, const char *from,
                                  const char *to)
{
    if (client == NULL)
        return FTP_CLIENT_INVALID;
    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, FTP_CLIENT_OP_RENAME))
        return FTP_CLIENT_BUSY;
    if (!ftp_remote_normalize(client->cwd, from, client->rename_from,
                              sizeof(client->rename_from)) ||
        !ftp_remote_normalize(client->cwd, to, client->rename_to,
                              sizeof(client->rename_to)) ||
        !ftp_remote_path_is_safe(client->rename_from) ||
        !ftp_remote_path_is_safe(client->rename_to)) {
        client_clear_op(client);
        return FTP_CLIENT_INVALID;
    }

    client->stage = FTP_STAGE_COMMAND;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client->step = CSTEP_RNFR;
    client_send(client, "RNFR %s", client->rename_from);
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_quit(FtpClient *client)
{
    if (client == NULL)
        return FTP_CLIENT_INVALID;
    if (client->state != FTP_CLIENT_READY)
        return FTP_CLIENT_NOT_CONNECTED;
    if (!client_start_op(client, FTP_CLIENT_OP_QUIT))
        return FTP_CLIENT_BUSY;

    client->stage = FTP_STAGE_COMMAND;
    client->stage_deadline = now_ms(client) + CLIENT_COMMAND_MS;
    client->step = CSTEP_QUIT;
    client_send(client, "QUIT");
    return FTP_CLIENT_OK;
}

FtpClientResult ftp_client_cancel(FtpClient *client)
{
    if (client == NULL)
        return FTP_CLIENT_INVALID;
    if (client->state != FTP_CLIENT_LISTING &&
        client->state != FTP_CLIENT_TRANSFERRING)
        return FTP_CLIENT_INVALID;
    if (client->data_state == FTP_DATA_NONE)
        return FTP_CLIENT_INVALID;

    client_begin_abort(client, FTP_CAUSE_USER, client->stage, "CANCELLED");
    return FTP_CLIENT_OK;
}

void ftp_client_tick(FtpClient *client)
{
    if (client == NULL)
        return;
    if (client->state == FTP_CLIENT_IDLE ||
        client->state == FTP_CLIENT_LOST ||
        client->state == FTP_CLIENT_FAILED ||
        client->state == FTP_CLIENT_STALLED)
        return;

    uint64_t now = now_ms(client);

    if (client->send_off < client->send_len) {
        if (!client_flush(client)) {
            client_lost(client, "CONTROL WRITE FAILED");
            return;
        }
    }

    NetWaitSet set;
    memset(&set, 0, sizeof(set));
    if (client->control != NULL) {
        set.items[set.count].socket = client->control;
        set.items[set.count].flags = NET_WAIT_READ;
        set.count++;
    }
    if (client->data != NULL &&
        (client->data_state == FTP_DATA_ACTIVE ||
         client->data_state == FTP_DATA_ACCEPTED ||
         client->data_state == FTP_DATA_DRAINING)) {
        set.items[set.count].socket = client->data;
        set.items[set.count].flags =
            client->data_kind == CDATA_STOR &&
                    client->data_state == FTP_DATA_ACTIVE
                ? NET_WAIT_WRITE
                : NET_WAIT_READ;
        set.count++;
    }

    if (set.count > 0) {
        NetReadySet ready;
        NetResult wr =
            client->net->wait(client->net->ctx, &set, 50, &ready);
        if (wr != NET_OK) {
            client_lost(client, "WAIT FAILED");
            return;
        }

        /* Data first: a final 226 can be readable in the same tick as the
         * last payload bytes, and completing before them would truncate. */
        for (unsigned i = 0; i < ready.count; ++i) {
            if (ready.items[i].socket == client->data)
                client_pump_data(client);
        }
        for (unsigned i = 0; i < ready.count; ++i) {
            if (ready.items[i].socket == client->control)
                client_receive_control(client);
        }
    }

    if (client->state == FTP_CLIENT_IDLE ||
        client->state == FTP_CLIENT_LOST ||
        client->state == FTP_CLIENT_FAILED ||
        client->state == FTP_CLIENT_STALLED)
        return;

    now = now_ms(client);

    if (client->abor_grace && now >= client->abor_grace_deadline)
        client->abor_grace = 0;

    if (client->abor_drain > 0 && now >= client->abor_deadline) {
        client_lost(client, "ABORT TIMEOUT");
        return;
    }

    if (client->budget_active && now >= client->budget_deadline) {
        client_stall(client, client->stage, "CONNECT BUDGET EXPIRED");
        return;
    }

    if (client->stage_deadline != 0 && now >= client->stage_deadline) {
        if (client->stage == FTP_STAGE_DATA ||
            client->stage == FTP_STAGE_TRANSFER) {
            if (client->data_state == FTP_DATA_ACTIVE) {
                client_data_failure(client, FTP_CAUSE_NETWORK, "DATA TIMEOUT");
                return;
            }
        }
        if (client->state == FTP_CLIENT_GREETING ||
            client->state == FTP_CLIENT_LOGGING_IN ||
            client->state == FTP_CLIENT_LISTING) {
            client_stall(client, client->stage, "STAGE TIMEOUT");
            return;
        }
        client_lost(client, "COMMAND TIMEOUT");
        return;
    }

    if ((client->data_state == FTP_DATA_ACTIVE ||
         client->data_state == FTP_DATA_DRAINING) &&
        client->data_deadline != 0 && now >= client->data_deadline) {
        client_data_failure(client, FTP_CAUSE_NETWORK, "DATA IDLE 15S");
        return;
    }

    if (client->send_off < client->send_len &&
        !client_flush(client)) {
        client_lost(client, "CONTROL WRITE FAILED");
        return;
    }
}

FtpClientState ftp_client_state(const FtpClient *client)
{
    return client != NULL ? client->state : FTP_CLIENT_IDLE;
}

FtpDataState ftp_client_data_state(const FtpClient *client)
{
    return client != NULL ? client->data_state : FTP_DATA_NONE;
}

FtpStage ftp_client_stage(const FtpClient *client)
{
    return client != NULL ? client->stage : FTP_STAGE_NONE;
}

const char *ftp_client_cwd_text(const FtpClient *client)
{
    return client != NULL ? client->cwd : "";
}

const Reply *ftp_client_last_reply(const FtpClient *client)
{
    return client != NULL ? &client->reply : NULL;
}

int ftp_client_has_mlsd(const FtpClient *client)
{
    return client != NULL ? client->has_mlsd : 0;
}

int ftp_client_busy(const FtpClient *client)
{
    return client != NULL &&
           (client->op_active || client->abor_drain > 0 || client->abor_grace);
}
