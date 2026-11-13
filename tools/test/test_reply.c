/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/reply.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_single_and_bare(void)
{
    Reply r;
    CHECK(reply_parse("220 hello", 9, &r) == REPLY_OK);
    CHECK(r.code == 220);
    CHECK(strcmp(r.text, "hello") == 0);
    CHECK(r.multiline == 0);

    CHECK(reply_parse("200", 3, &r) == REPLY_OK);
    CHECK(r.code == 200);
    CHECK(r.text[0] == '\0');
}

static void test_multiline(void)
{
    const char *block = "211-First\n second\n211 End\n";
    Reply r;
    CHECK(reply_parse(block, strlen(block), &r) == REPLY_OK);
    CHECK(r.code == 211);
    CHECK(r.multiline == 1);
    CHECK(strcmp(r.text, "End") == 0);

    ReplyReader reader;
    reply_reader_init(&reader);
    CHECK(reply_reader_line(&reader, "211-First", 9, &r) == REPLY_MORE);
    CHECK(strstr(r.body, "First") != NULL);
    CHECK(reply_reader_line(&reader, " second", 7, &r) == REPLY_MORE);
    CHECK(reply_reader_line(&reader, "211 End", 7, &r) == REPLY_OK);
    CHECK(r.code == 211);
    CHECK(strcmp(r.text, "End") == 0);
    CHECK(strstr(r.body, "First") != NULL);
    CHECK(strstr(r.body, "second") != NULL);
}

static void test_malformed_and_cap(void)
{
    Reply r;
    CHECK(reply_parse("220hello", 8, &r) == REPLY_MALFORMED);
    CHECK(reply_parse("", 0, &r) == REPLY_MALFORMED);

    char line[REPLY_LINE_MAX + 2];
    line[0] = '2';
    line[1] = '2';
    line[2] = '0';
    line[3] = ' ';
    memset(line + 4, 'x', sizeof(line) - 5);
    line[sizeof(line) - 1] = '\0';
    CHECK(reply_parse(line, sizeof(line) - 1, &r) == REPLY_TOO_LONG);
}

static void test_pasv(void)
{
    NetAddress peer = {0xC0A8012Au, 21}; /* 192.168.1.42 */
    NetAddress out;

    CHECK(reply_parse_pasv("Entering Passive Mode (192,168,1,42,19,140)",
                           &peer, &out) == REPLY_PASV_OK);
    CHECK(out.addr == peer.addr);
    CHECK(out.port == 5004);

    /* The PASV host is ignored; the control peer and the reply port win. */
    NetAddress foreign = {0x0A000001u, 21};
    CHECK(reply_parse_pasv("Entering Passive Mode (192,168,1,42,19,140)",
                           &foreign, &out) == REPLY_PASV_OK);
    CHECK(out.addr == foreign.addr);
    CHECK(out.port == 5004);

    CHECK(reply_parse_pasv("Entering Passive Mode (192,168,1,42,0,0)", &peer,
                           &out) == REPLY_PASV_MALFORMED);
    CHECK(reply_parse_pasv("no address here", &peer, &out) ==
          REPLY_PASV_MALFORMED);

    CHECK(reply_parse_epsv("Entering Extended Passive Mode (|||5004|)", &peer,
                           &out) == REPLY_PASV_OK);
    CHECK(out.addr == peer.addr);
    CHECK(out.port == 5004);

    CHECK(reply_parse_epsv("Entering Extended Passive Mode (!!!5004!)", &peer,
                           &out) == REPLY_PASV_OK);
    CHECK(out.port == 5004);
}

int main(void)
{
    test_single_and_bare();
    test_multiline();
    test_malformed_and_cap();
    test_pasv();

    if (s_failures != 0) {
        fprintf(stderr, "test_reply: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_reply: OK\n");
    return 0;
}
