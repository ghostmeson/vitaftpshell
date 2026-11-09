#ifndef VFTP_FAKE_FTP_CLIENT_H
#define VFTP_FAKE_FTP_CLIENT_H

#include <stddef.h>

#include "core/ftp/ftp.h"
#include "fake_net.h"

/*
 * Scripted FTP client for server tests. It queues commands on the server's
 * accepted control socket and reads the server's replies from that socket's
 * transmit buffer. A multiline reply is assembled before it is returned.
 */
typedef struct {
    NetSocket *control;
    char pending[FTP_LINE_MAX + 1];
    size_t len;

    int assembling;
    int code;
    char body[FTP_LINE_MAX];
    size_t body_len;
} FakeFtpClient;

void fake_ftp_client_init(FakeFtpClient *client, NetSocket *control);
void fake_ftp_client_send(FakeFtpClient *client, const char *line);
void fake_ftp_client_send_raw(FakeFtpClient *client, const char *data);

/* 1 when a complete reply is returned; 0 when none is available yet. */
int fake_ftp_client_read_reply(FakeFtpClient *client, int *code, char *text,
                               size_t cap);
/* Read a raw line without interpreting a reply. 1 when one was returned. */
int fake_ftp_client_read_line(FakeFtpClient *client, char *out, size_t cap);

#endif
