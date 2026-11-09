/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_CORE_FTP_SERVER_H
#define VFTP_CORE_FTP_SERVER_H

#include <stdint.h>

#include "core/ftp/ftp.h"
#include "core/path.h"
#include "platform/clock.h"
#include "platform/fs.h"
#include "platform/net.h"

/*
 * Anonymous passive FTP server. OFF is the resting state. The machine binds
 * the control port and control+1 on the LAN address, accepts one client, and
 * runs one data command at a time. Every path resolves through the shared path
 * table; a denial answers exactly as a missing path and creates nothing.
 */

typedef enum {
    FTP_SERVER_OFF = 0,
    FTP_SERVER_BINDING,
    FTP_SERVER_ON,
    FTP_SERVER_SESSION,
    FTP_SERVER_REFUSING,
    FTP_SERVER_STOPPING
} FtpServerState;

typedef enum {
    FTP_SERVER_EVENT_STATE = 0,
    FTP_SERVER_EVENT_CLIENT,
    FTP_SERVER_EVENT_PROGRESS,
    FTP_SERVER_EVENT_TRANSFER_DONE,
    FTP_SERVER_EVENT_ERROR
} FtpServerEventKind;

typedef struct {
    FtpServerEventKind kind;
    FtpServerState state;
    int clients;
    FtpDataState data_state;
    FtpStage stage;
    FtpCause cause;
    const char *path;
    const char *detail;
    uint64_t bytes;
    uint64_t total;
    int total_known;
    int ok;
} FtpServerEvent;

typedef void (*FtpServerEventFn)(void *ctx, const FtpServerEvent *event);

typedef enum {
    FTP_SERVER_OK = 0,
    FTP_SERVER_BUSY,
    FTP_SERVER_NO_NETWORK,
    FTP_SERVER_BIND_FAILED,
    FTP_SERVER_INVALID
} FtpServerResult;

typedef struct FtpServer FtpServer;

FtpServer *ftp_server_create(NetPort *net, FsPort *fs, ClockPort *clock,
                             FtpServerEventFn emit, void *emit_ctx);
void ftp_server_destroy(FtpServer *server);

FtpServerResult ftp_server_start(FtpServer *server, uint16_t control_port);
void ftp_server_stop(FtpServer *server);
void ftp_server_tick(FtpServer *server);

FtpServerState ftp_server_state(const FtpServer *server);
int ftp_server_clients(const FtpServer *server);
int ftp_server_busy(const FtpServer *server);
uint16_t ftp_server_control_port(const FtpServer *server);
uint16_t ftp_server_data_port(const FtpServer *server);

#endif
