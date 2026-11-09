#ifndef VFTP_FAKE_FTPD_H
#define VFTP_FAKE_FTPD_H

#include <stddef.h>

#include "core/ftp/ftp.h"
#include "fake_net.h"

/*
 * Scripted FTP server for client tests. It reads the client's control-socket
 * output and queues replies on the same fake socket; data payloads are queued
 * by the test on the data socket the client opens after 227.
 */
typedef struct {
    NetSocket *control;
    char pending[FTP_LINE_MAX + 1];
    size_t len;
} FakeFtpd;

void fake_ftpd_init(FakeFtpd *server, NetSocket *control);
/* 1 when a command line was returned, 0 when none is available. */
int fake_ftpd_next_command(FakeFtpd *server, char *out, size_t cap);
void fake_ftpd_send(FakeFtpd *server, const char *line);
void fake_ftpd_send_raw(FakeFtpd *server, const char *data);
void fake_ftpd_reply(FakeFtpd *server, int code, const char *text);
/* The most recent socket the client connected; use it after a 227. */
NetSocket *fake_ftpd_data_socket(void);

#endif
