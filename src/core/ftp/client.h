#ifndef VFTP_CORE_FTP_CLIENT_H
#define VFTP_CORE_FTP_CLIENT_H

#include <stdint.h>

#include "core/ftp/ftp.h"
#include "core/listing.h"
#include "core/path.h"
#include "core/reply.h"
#include "platform/clock.h"
#include "platform/fs.h"
#include "platform/net.h"

/*
 * Passive FTP client. One machine drives connect, login, browsing, and one
 * transfer at a time. It owns the client session state; the caller owns the
 * queue and the UI. Every completion is reported through one event callback.
 *
 * The machine never blocks on a file body: a tick reads or writes a bounded
 * chunk. The worker calls tick when the control or data socket is ready and at
 * least once per deadline check.
 */

typedef enum {
    FTP_CLIENT_IDLE = 0,
    FTP_CLIENT_RESOLVING,
    FTP_CLIENT_CONNECTING,
    FTP_CLIENT_GREETING,
    FTP_CLIENT_LOGGING_IN,
    FTP_CLIENT_READY,
    FTP_CLIENT_LISTING,
    FTP_CLIENT_TRANSFERRING,
    FTP_CLIENT_STALLED,
    FTP_CLIENT_LOST,
    FTP_CLIENT_FAILED
} FtpClientState;

typedef enum {
    FTP_CLIENT_OP_CONNECT = 0,
    FTP_CLIENT_OP_LIST,
    FTP_CLIENT_OP_NLST,
    FTP_CLIENT_OP_CWD,
    FTP_CLIENT_OP_DOWNLOAD,
    FTP_CLIENT_OP_UPLOAD,
    FTP_CLIENT_OP_MKDIR,
    FTP_CLIENT_OP_RENAME,
    FTP_CLIENT_OP_DELETE,
    FTP_CLIENT_OP_RMDIR,
    FTP_CLIENT_OP_QUIT
} FtpClientOperation;

typedef enum {
    FTP_CLIENT_EVENT_STATE = 0,
    FTP_CLIENT_EVENT_LISTING_BEGIN,
    FTP_CLIENT_EVENT_LISTING_ROW,
    FTP_CLIENT_EVENT_LISTING_END,
    FTP_CLIENT_EVENT_PROGRESS,
    FTP_CLIENT_EVENT_OPERATION_DONE
} FtpClientEventKind;

/*
 * Event fields are valid for the duration of the callback. `detail` is the
 * server reply text for a server-class failure, or a short mapped reason; it is
 * for the card footer, never for the session log.
 */
typedef struct {
    FtpClientEventKind kind;
    FtpClientState state;
    FtpDataState data_state;
    FtpStage stage;
    FtpCause cause;
    FtpClientOperation operation;
    int ok;
    const char *path;
    const char *detail;
    const ListingEntry *row;
    uint64_t bytes;
    uint64_t total;
    int total_known;
} FtpClientEvent;

typedef void (*FtpClientEventFn)(void *ctx, const FtpClientEvent *event);

typedef struct {
    const char *host;
    uint16_t port;
    int anonymous;
    const char *username;
    const char *password;
} FtpClientLogin;

typedef struct {
    const char *remote_path;
    const char *local_path;
    /* The caller has shown the high-risk confirmation for this destination. */
    int risk_approved;
} FtpClientTransfer;

typedef enum {
    FTP_CLIENT_OK = 0,
    FTP_CLIENT_BUSY,
    FTP_CLIENT_INVALID,
    FTP_CLIENT_NOT_CONNECTED,
    FTP_CLIENT_NEED_RISK,
    FTP_CLIENT_DENIED
} FtpClientResult;

typedef struct FtpClient FtpClient;

FtpClient *ftp_client_create(NetPort *net, FsPort *fs, ClockPort *clock,
                             FtpClientEventFn emit, void *emit_ctx);
void ftp_client_destroy(FtpClient *client);

FtpClientResult ftp_client_connect(FtpClient *client,
                                   const FtpClientLogin *login);
FtpClientResult ftp_client_list(FtpClient *client, const char *path);
FtpClientResult ftp_client_nlst(FtpClient *client, const char *path);
FtpClientResult ftp_client_cwd(FtpClient *client, const char *path);
/* A denied destination returns DENIED; an unapproved high-risk one NEED_RISK. */
FtpClientResult ftp_client_download(FtpClient *client,
                                    const FtpClientTransfer *transfer,
                                    PathRisk *out_risk);
FtpClientResult ftp_client_upload(FtpClient *client,
                                  const FtpClientTransfer *transfer);
FtpClientResult ftp_client_mkdir(FtpClient *client, const char *path);
FtpClientResult ftp_client_rename(FtpClient *client, const char *from,
                                  const char *to);
FtpClientResult ftp_client_delete(FtpClient *client, const char *path);
FtpClientResult ftp_client_rmdir(FtpClient *client, const char *path);
FtpClientResult ftp_client_quit(FtpClient *client);
/* Cancels the listing or transfer in flight; the session survives. */
FtpClientResult ftp_client_cancel(FtpClient *client);

void ftp_client_tick(FtpClient *client);

FtpClientState ftp_client_state(const FtpClient *client);
FtpDataState ftp_client_data_state(const FtpClient *client);
FtpStage ftp_client_stage(const FtpClient *client);
const char *ftp_client_cwd_text(const FtpClient *client);
const Reply *ftp_client_last_reply(const FtpClient *client);
int ftp_client_has_mlsd(const FtpClient *client);
int ftp_client_busy(const FtpClient *client);

#endif
