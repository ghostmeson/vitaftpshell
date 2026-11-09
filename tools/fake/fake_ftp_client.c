#include "fake_ftp_client.h"

#include <stdio.h>
#include <string.h>

static void refill(FakeFtpClient *client)
{
    if (client->len >= sizeof(client->pending) - 1)
        return;
    size_t got = fake_net_read_tx(client->control,
                                  client->pending + client->len,
                                  sizeof(client->pending) - 1 - client->len);
    client->len += got;
    client->pending[client->len] = '\0';
}

static int next_line(FakeFtpClient *client, char *out, size_t cap)
{
    for (;;) {
        char *newline = strchr(client->pending, '\n');
        if (newline != NULL) {
            size_t len = (size_t)(newline - client->pending);
            if (len > 0 && client->pending[len - 1] == '\r')
                len--;
            if (len >= cap)
                len = cap - 1;
            memcpy(out, client->pending, len);
            out[len] = '\0';
            size_t consumed = (size_t)(newline - client->pending) + 1;
            memmove(client->pending, client->pending + consumed,
                    client->len - consumed + 1);
            client->len -= consumed;
            return 1;
        }

        size_t before = client->len;
        refill(client);
        if (client->len == before)
            return 0;
    }
}

void fake_ftp_client_send_raw(FakeFtpClient *client, const char *data)
{
    if (client == NULL || client->control == NULL || data == NULL)
        return;
    fake_net_queue_rx(client->control, data, strlen(data));
}

void fake_ftp_client_send(FakeFtpClient *client, const char *line)
{
    if (client == NULL || line == NULL)
        return;
    fake_ftp_client_send_raw(client, line);
    fake_ftp_client_send_raw(client, "\r\n");
}

int fake_ftp_client_read_line(FakeFtpClient *client, char *out, size_t cap)
{
    if (client == NULL)
        return 0;
    return next_line(client, out, cap);
}

int fake_ftp_client_read_reply(FakeFtpClient *client, int *code, char *text,
                               size_t cap)
{
    if (client == NULL || code == NULL)
        return 0;

    char line[FTP_LINE_MAX + 1];
    for (;;) {
        if (!next_line(client, line, sizeof(line)))
            return 0;

        if (!client->assembling) {
            if (strlen(line) < 4 || line[0] < '0' || line[0] > '9' ||
                line[1] < '0' || line[1] > '9' || line[2] < '0' ||
                line[2] > '9')
                continue;

            int value = (line[0] - '0') * 100 + (line[1] - '0') * 10 +
                        (line[2] - '0');
            if (line[3] == '-') {
                client->assembling = 1;
                client->code = value;
                client->body_len = 0;
                if (cap > 0)
                    text[0] = '\0';
                continue;
            }

            *code = value;
            if (cap > 0) {
                const char *body = line[3] == ' ' ? line + 4 : line + 3;
                strncpy(text, body, cap - 1);
                text[cap - 1] = '\0';
            }
            return 1;
        }

        if (strlen(line) >= 3 && line[0] - '0' == client->code / 100 &&
            line[1] - '0' == (client->code / 10) % 10 &&
            line[2] - '0' == client->code % 10 && line[3] == ' ') {
            if (client->body_len > 0 && cap > 0) {
                size_t n = client->body_len < cap - 1 ? client->body_len
                                                      : cap - 1;
                memcpy(text, client->body, n);
                text[n] = '\0';
            } else if (cap > 0) {
                strncpy(text, line + 4, cap - 1);
                text[cap - 1] = '\0';
            }
            *code = client->code;
            client->assembling = 0;
            return 1;
        }

        size_t n = strlen(line);
        if (client->body_len + n + 1 < sizeof(client->body)) {
            memcpy(client->body + client->body_len, line, n);
            client->body_len += n;
            client->body[client->body_len++] = '\n';
            client->body[client->body_len] = '\0';
        }
    }
}

void fake_ftp_client_init(FakeFtpClient *client, NetSocket *control)
{
    if (client == NULL)
        return;
    memset(client, 0, sizeof(*client));
    client->control = control;
}
