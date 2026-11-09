#include "fake_ftpd.h"

#include <stdio.h>
#include <string.h>

static void refill(FakeFtpd *server)
{
    if (server->len >= sizeof(server->pending) - 1)
        return;
    size_t got = fake_net_read_tx(server->control,
                                  server->pending + server->len,
                                  sizeof(server->pending) - 1 - server->len);
    server->len += got;
    server->pending[server->len] = '\0';
}

int fake_ftpd_next_command(FakeFtpd *server, char *out, size_t cap)
{
    if (server == NULL || server->control == NULL || out == NULL || cap == 0)
        return 0;

    for (;;) {
        char *newline = strchr(server->pending, '\n');
        if (newline != NULL) {
            size_t len = (size_t)(newline - server->pending);
            if (len > 0 && server->pending[len - 1] == '\r')
                len--;
            if (len >= cap)
                len = cap - 1;
            memcpy(out, server->pending, len);
            out[len] = '\0';
            size_t consumed = (size_t)(newline - server->pending) + 1;
            memmove(server->pending, server->pending + consumed,
                    server->len - consumed + 1);
            server->len -= consumed;
            return 1;
        }

        size_t before = server->len;
        refill(server);
        if (server->len == before)
            return 0;
    }
}

void fake_ftpd_send_raw(FakeFtpd *server, const char *data)
{
    if (server == NULL || server->control == NULL || data == NULL)
        return;
    fake_net_queue_rx(server->control, data, strlen(data));
}

void fake_ftpd_send(FakeFtpd *server, const char *line)
{
    if (server == NULL || line == NULL)
        return;
    fake_ftpd_send_raw(server, line);
    fake_ftpd_send_raw(server, "\r\n");
}

void fake_ftpd_reply(FakeFtpd *server, int code, const char *text)
{
    char line[FTP_REPLY_TEXT_MAX + 16];
    snprintf(line, sizeof(line), "%03d %s", code, text != NULL ? text : "");
    fake_ftpd_send(server, line);
}

NetSocket *fake_ftpd_data_socket(void)
{
    return fake_net_last_connected();
}

void fake_ftpd_init(FakeFtpd *server, NetSocket *control)
{
    if (server == NULL)
        return;
    memset(server, 0, sizeof(*server));
    server->control = control;
}
