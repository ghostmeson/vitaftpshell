#ifndef VFTP_CORE_REPLY_H
#define VFTP_CORE_REPLY_H

#include <stddef.h>

#include "platform/net.h"

/*
 * FTP reply parsing. One reader serves the client and the server: a single
 * line, a multiline block, a bare code, and the passive-mode address replies.
 *
 * The reader is incremental because the client receives arbitrary TCP chunks;
 * reply_parse is the one-shot form for a complete buffer and for tests.
 */

#define REPLY_LINE_MAX 4096
#define REPLY_BODY_MAX 4096

typedef struct {
    int code;
    char text[REPLY_LINE_MAX];
    char body[REPLY_BODY_MAX];
    int multiline;
} Reply;

typedef enum {
    REPLY_OK = 0,
    REPLY_MORE,
    REPLY_MALFORMED,
    REPLY_TOO_LONG,
    REPLY_INCOMPLETE
} ReplyResult;

typedef struct {
    int active;
    int multiline;
    int code;
    size_t text_len;
    size_t body_len;
    char text[REPLY_LINE_MAX];
    char body[REPLY_BODY_MAX];
} ReplyReader;

void reply_reader_init(ReplyReader *reader);
/* Feed one line without its newline. Returns REPLY_MORE or REPLY_OK. */
ReplyResult reply_reader_line(ReplyReader *reader, const char *line, size_t len,
                              Reply *out);
/* Parse a complete reply block, newline separated. */
ReplyResult reply_parse(const char *text, size_t len, Reply *out);

typedef enum {
    REPLY_PASV_OK = 0,
    REPLY_PASV_REFUSED,
    REPLY_PASV_MALFORMED
} ReplyPasvResult;

/* 227 parsing; a host different from the control peer is refused. */
ReplyPasvResult reply_parse_pasv(const char *text,
                                 const NetAddress *control_peer,
                                 NetAddress *out);
/* 229 parsing; the address is the control peer's, the port from the reply. */
ReplyPasvResult reply_parse_epsv(const char *text,
                                 const NetAddress *control_peer,
                                 NetAddress *out);

#endif
