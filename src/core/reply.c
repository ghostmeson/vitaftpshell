#include "core/reply.h"

#include <ctype.h>
#include <string.h>

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int parse_code(const char *line, size_t len, int *code)
{
    if (len < 3 || !is_digit(line[0]) || !is_digit(line[1]) ||
        !is_digit(line[2]))
        return 0;
    *code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
    return 1;
}

static void copy_text(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int append_body(char *dst, size_t cap, size_t *used, const char *src,
                       size_t len)
{
    if (*used > 0) {
        if (*used + 1 >= cap)
            return 0;
        dst[(*used)++] = '\n';
    }
    if (*used + len >= cap)
        return 0;
    memcpy(dst + *used, src, len);
    *used += len;
    dst[*used] = '\0';
    return 1;
}

void reply_reader_init(ReplyReader *reader)
{
    if (reader == NULL)
        return;
    memset(reader, 0, sizeof(*reader));
}

ReplyResult reply_reader_line(ReplyReader *reader, const char *line, size_t len,
                              Reply *out)
{
    if (reader == NULL || out == NULL || line == NULL)
        return REPLY_MALFORMED;
    if (len > REPLY_LINE_MAX - 1)
        return REPLY_TOO_LONG;

    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
        len--;

    if (!reader->active) {
        int code = 0;
        if (!parse_code(line, len, &code))
            return REPLY_MALFORMED;

        reader->active = 1;
        reader->code = code;
        reader->multiline = 0;
        reader->text_len = 0;
        reader->body_len = 0;
        reader->text[0] = '\0';
        reader->body[0] = '\0';

        if (len == 3) {
            out->code = code;
            out->text[0] = '\0';
            out->body[0] = '\0';
            out->multiline = 0;
            reader->active = 0;
            return REPLY_OK;
        }

        if (line[3] == ' ') {
            copy_text(reader->text, sizeof(reader->text), line + 4, len - 4);
            reader->text_len = strlen(reader->text);
            out->code = code;
            copy_text(out->text, sizeof(out->text), reader->text,
                      reader->text_len);
            out->body[0] = '\0';
            out->multiline = 0;
            reader->active = 0;
            return REPLY_OK;
        }

        if (line[3] == '-') {
            reader->multiline = 1;
            copy_text(reader->text, sizeof(reader->text), line + 4, len - 4);
            reader->text_len = strlen(reader->text);
            out->code = code;
            copy_text(out->text, sizeof(out->text), reader->text,
                      reader->text_len);
            out->body[0] = '\0';
            out->multiline = 1;
            return REPLY_MORE;
        }

        reader->active = 0;
        return REPLY_MALFORMED;
    }

    /* Continuation of a multiline reply. */
    int code = 0;
    int same_code = parse_code(line, len, &code) && code == reader->code;

    if (same_code && len == 3) {
        reader->active = 0;
        out->code = reader->code;
        copy_text(out->text, sizeof(out->text), reader->text,
                  reader->text_len);
        copy_text(out->body, sizeof(out->body), reader->body, reader->body_len);
        out->multiline = 1;
        return REPLY_OK;
    }

    if (same_code && line[3] == ' ') {
        copy_text(reader->text, sizeof(reader->text), line + 4, len - 4);
        reader->text_len = strlen(reader->text);
        reader->active = 0;
        out->code = reader->code;
        copy_text(out->text, sizeof(out->text), reader->text,
                  reader->text_len);
        copy_text(out->body, sizeof(out->body), reader->body, reader->body_len);
        out->multiline = 1;
        return REPLY_OK;
    }

    if (same_code && line[3] == '-') {
        if (!append_body(reader->body, sizeof(reader->body), &reader->body_len,
                         line + 4, len - 4))
            return REPLY_TOO_LONG;
        out->code = reader->code;
        copy_text(out->text, sizeof(out->text), reader->text,
                  reader->text_len);
        copy_text(out->body, sizeof(out->body), reader->body, reader->body_len);
        out->multiline = 1;
        return REPLY_MORE;
    }

    /* RFC 959 allows an intermediate line without the code prefix. */
    if (same_code)
        return REPLY_MALFORMED;

    if (!append_body(reader->body, sizeof(reader->body), &reader->body_len,
                     line, len))
        return REPLY_TOO_LONG;
    out->code = reader->code;
    copy_text(out->text, sizeof(out->text), reader->text, reader->text_len);
    copy_text(out->body, sizeof(out->body), reader->body, reader->body_len);
    out->multiline = 1;
    return REPLY_MORE;
}

ReplyResult reply_parse(const char *text, size_t len, Reply *out)
{
    if (text == NULL || out == NULL)
        return REPLY_MALFORMED;

    ReplyReader reader;
    reply_reader_init(&reader);

    size_t start = 0;
    int saw_line = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i != len && text[i] != '\n')
            continue;

        size_t line_len = i - start;
        if (i == len && line_len == 0)
            break;
        saw_line = 1;

        ReplyResult result = reply_reader_line(&reader, text + start, line_len,
                                               out);
        if (result == REPLY_OK)
            return REPLY_OK;
        if (result != REPLY_MORE)
            return result;
        start = i + 1;
    }

    return saw_line ? REPLY_INCOMPLETE : REPLY_MALFORMED;
}

static int read_number(const char **cursor, int *value)
{
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t')
        p++;
    if (!is_digit(*p))
        return 0;

    int v = 0;
    while (is_digit(*p)) {
        v = v * 10 + (*p - '0');
        if (v > 255)
            return 0;
        p++;
    }
    *cursor = p;
    *value = v;
    return 1;
}

ReplyPasvResult reply_parse_pasv(const char *text,
                                 const NetAddress *control_peer,
                                 NetAddress *out)
{
    if (text == NULL || out == NULL)
        return REPLY_PASV_MALFORMED;

    const char *p = strchr(text, '(');
    if (p != NULL) {
        p++;
    } else {
        p = text;
        while (*p != '\0' && !is_digit(*p))
            p++;
    }

    int h[4];
    int port_hi = 0;
    int port_lo = 0;
    for (int i = 0; i < 4; ++i) {
        if (!read_number(&p, &h[i]))
            return REPLY_PASV_MALFORMED;
        if (*p != ',')
            return REPLY_PASV_MALFORMED;
        p++;
    }
    if (!read_number(&p, &port_hi) || *p != ',')
        return REPLY_PASV_MALFORMED;
    p++;
    if (!read_number(&p, &port_lo))
        return REPLY_PASV_MALFORMED;

    int port = port_hi * 256 + port_lo;
    if (port <= 0)
        return REPLY_PASV_MALFORMED;

    NetAddress addr;
    addr.addr = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) |
                ((uint32_t)h[2] << 8) | (uint32_t)h[3];
    addr.port = (uint16_t)port;

    if (control_peer != NULL && addr.addr != control_peer->addr)
        return REPLY_PASV_REFUSED;

    *out = addr;
    return REPLY_PASV_OK;
}

ReplyPasvResult reply_parse_epsv(const char *text,
                                 const NetAddress *control_peer,
                                 NetAddress *out)
{
    if (text == NULL || out == NULL || control_peer == NULL)
        return REPLY_PASV_MALFORMED;

    const char *p = strchr(text, '(');
    if (p == NULL)
        return REPLY_PASV_MALFORMED;
    p++;
    if (*p != '|')
        return REPLY_PASV_MALFORMED;

    int bars = 0;
    while (*p != '\0' && bars < 3) {
        if (*p == '|')
            bars++;
        p++;
    }
    if (bars != 3)
        return REPLY_PASV_MALFORMED;

    int port = 0;
    int digits = 0;
    while (is_digit(*p)) {
        port = port * 10 + (*p - '0');
        if (port > 65535)
            return REPLY_PASV_MALFORMED;
        p++;
        digits++;
    }
    if (digits == 0 || *p != '|' || port <= 0)
        return REPLY_PASV_MALFORMED;

    NetAddress addr = *control_peer;
    addr.port = (uint16_t)port;
    *out = addr;
    return REPLY_PASV_OK;
}
