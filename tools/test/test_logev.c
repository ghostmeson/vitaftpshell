#include <stdio.h>
#include <string.h>

#include "core/logev.h"
#include "core/site.h"
#include "fake_log.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_no_passwords(void)
{
    fake_log_reset();
    LogPort *log = fake_log_port();

    SiteBook book;
    site_book_init(&book);
    Site site;
    memset(&site, 0, sizeof(site));
    strncpy(site.name, "site", sizeof(site.name) - 1);
    strncpy(site.host, "host", sizeof(site.host) - 1);
    site.port = 21;
    site.anonymous = 0;
    strncpy(site.password, "hunter2-secret", sizeof(site.password) - 1);
    CHECK(site_book_add(&book, &site) == 1);

    for (int event = 0; event < LOGEV_EVENT_COUNT; ++event) {
        LogRecord record;
        logev_init(&record, (LogEvent)event);
        record.site = book.items[1].name;
        record.host = book.items[1].host;
        record.path = "ux0:data/file";
        record.stage = "data";
        record.cls = "network";
        record.bytes = 42;
        record.total = 100;
        record.port = 1337;
        record.clients = 1;
        CHECK(logev_emit(log, (uint64_t)event * 10, &record) == LOG_OK);
    }

    for (unsigned i = 0; i < fake_log_count(); ++i) {
        const char *line = fake_log_line(i);
        CHECK(line != NULL);
        if (line != NULL)
            CHECK(strstr(line, "hunter2-secret") == NULL);
    }
}

static void test_reply_text_is_not_logged(void)
{
    /* There is deliberately no reply field; a 530 echo has nowhere to go. */
    LogRecord record;
    logev_init(&record, LOGEV_LOGIN);
    record.site = "site";
    record.host = "host";
    record.stage = "login";
    record.cls = "server";

    char line[LOGEV_LINE_MAX];
    CHECK(logev_format(line, sizeof(line), 1234, &record) > 0);
    CHECK(strstr(line, "1234") == line);
    CHECK(strstr(line, "login") != NULL);
    CHECK(strstr(line, "reply") == NULL);
}

static void test_rotation(void)
{
    fake_log_reset();
    fake_log_set_capacity(90);
    LogPort *log = fake_log_port();

    LogRecord record;
    logev_init(&record, LOGEV_LIST);
    record.path = "ux0:data";
    record.site = "site";

    for (int i = 0; i < 12; ++i)
        CHECK(logev_emit(log, (uint64_t)i, &record) == LOG_OK);

    CHECK(fake_log_rotations() > 0);
    CHECK(fake_log_count() > 0);
    CHECK(logev_should_rotate(LOGEV_CAP, 1) == 1);
    CHECK(logev_should_rotate(0, 1) == 0);
}

int main(void)
{
    test_no_passwords();
    test_reply_text_is_not_logged();
    test_rotation();

    if (s_failures != 0) {
        fprintf(stderr, "test_logev: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_logev: OK\n");
    return 0;
}
