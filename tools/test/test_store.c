/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/settings.h"
#include "core/site.h"
#include "core/store.h"
#include "fake_fs.h"
#include "platform/fs.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void setup_fs(void)
{
    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ux0:data/vitaFTPShell");
}

static Site make_site(const char *name, const char *host, const char *pass)
{
    Site site;
    memset(&site, 0, sizeof(site));
    strncpy(site.name, name, sizeof(site.name) - 1);
    strncpy(site.host, host, sizeof(site.host) - 1);
    site.port = 2121;
    site.anonymous = 0;
    strncpy(site.username, "user", sizeof(site.username) - 1);
    strncpy(site.password, pass, sizeof(site.password) - 1);
    return site;
}

static void test_round_trip(void)
{
    setup_fs();
    FsPort *fs = fake_fs_port();

    SiteBook book;
    site_book_init(&book);
    Site site = make_site("my site", "example.test", "a\tb%c\nd");
    CHECK(site_book_add(&book, &site) == 1);

    CHECK(store_save_sites(fs, STORE_DIR, &book) == STORE_OK);
    CHECK(fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg"));
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg.tmp"));
    CHECK(fake_fs_sync_count() > 0);

    SiteBook loaded;
    site_book_init(&loaded);
    CHECK(store_load_sites(fs, STORE_DIR, &loaded) == STORE_OK);
    CHECK(loaded.count == 2);
    CHECK(loaded.parsed == 1);
    CHECK(loaded.partial == 0);

    const Site *first = site_book_at(&loaded, 0);
    const Site *second = site_book_at(&loaded, 1);
    CHECK(first != NULL && strcmp(first->name, "ftp.gnu.org") == 0);
    CHECK(second != NULL);
    if (second != NULL) {
        CHECK(strcmp(second->name, "my site") == 0);
        CHECK(strcmp(second->host, "example.test") == 0);
        CHECK(strcmp(second->username, "user") == 0);
        CHECK(strcmp(second->password, "a\tb%c\nd") == 0);
        CHECK(second->port == 2121);
        CHECK(second->anonymous == 0);
    }
}

static void test_bad_header(void)
{
    setup_fs();
    FsPort *fs = fake_fs_port();
    const char *bad = "vitaFTPShell-sites 99\nname=x\thost=h\tport=21\tanon=1\n";
    fake_fs_seed_file("ux0:data/vitaFTPShell/sites.cfg", bad, strlen(bad));

    SiteBook book;
    site_book_init(&book);
    CHECK(store_load_sites(fs, STORE_DIR, &book) == STORE_BAD_VERSION);
    CHECK(book.count == 0);
    CHECK(book.parsed == 0);
}

static void test_partial(void)
{
    setup_fs();
    FsPort *fs = fake_fs_port();
    const char *text = "vitaFTPShell-sites 1\n"
                       "name=one\thost=h\tport=21\tanon=1\tuser=\tpass=\n"
                       "name=two\thost=h\n";
    fake_fs_seed_file("ux0:data/vitaFTPShell/sites.cfg", text, strlen(text));

    SiteBook book;
    site_book_init(&book);
    CHECK(store_load_sites(fs, STORE_DIR, &book) == STORE_PARTIAL);
    CHECK(book.count == 1);
    CHECK(book.parsed == 1);
    CHECK(book.partial == 1);
    CHECK(strcmp(book.items[0].name, "one") == 0);
}

static void test_rename_failure_keeps_original(void)
{
    setup_fs();
    FsPort *fs = fake_fs_port();

    SiteBook book;
    site_book_init(&book);
    Site first = make_site("one", "host", "p1");
    CHECK(site_book_add(&book, &first) == 1);
    CHECK(store_save_sites(fs, STORE_DIR, &book) == STORE_OK);

    char before[4096];
    size_t before_len = fake_fs_read_file(
        "ux0:data/vitaFTPShell/sites.cfg", before, sizeof(before));
    CHECK(before_len > 0);

    Site second = make_site("two", "host", "p2");
    CHECK(site_book_update(&book, 1, &second) == 1);
    fake_fs_fail_op(FAKE_FS_OP_RENAME, FS_IO);
    CHECK(store_save_sites(fs, STORE_DIR, &book) == STORE_IO);

    char after[4096];
    size_t after_len = fake_fs_read_file("ux0:data/vitaFTPShell/sites.cfg",
                                         after, sizeof(after));
    CHECK(after_len == before_len);
    CHECK(after_len > 0 && memcmp(before, after, before_len) == 0);
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg.tmp"));
}

static void test_replace_fallback(void)
{
    setup_fs();
    FsPort *fs = fake_fs_port();

    SiteBook book;
    site_book_init(&book);
    Site first = make_site("one", "host", "p1");
    CHECK(site_book_add(&book, &first) == 1);
    CHECK(store_save_sites(fs, STORE_DIR, &book) == STORE_OK);

    fake_fs_set_rename_replaces(0);
    Site second = make_site("two", "host", "p2");
    CHECK(site_book_update(&book, 1, &second) == 1);
    CHECK(store_save_sites(fs, STORE_DIR, &book) == STORE_OK);

    CHECK(fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg"));
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg.tmp"));
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg.old"));

    SiteBook loaded;
    site_book_init(&loaded);
    CHECK(store_load_sites(fs, STORE_DIR, &loaded) == STORE_OK);
    CHECK(loaded.count == 2);
    CHECK(strcmp(loaded.items[1].name, "two") == 0);
}

static void test_stale_aside(void)
{
    setup_fs();
    FsPort *fs = fake_fs_port();

    SiteBook book;
    site_book_init(&book);
    Site first = make_site("one", "host", "p1");
    CHECK(site_book_add(&book, &first) == 1);
    CHECK(store_save_sites(fs, STORE_DIR, &book) == STORE_OK);

    /* Simulate a crash after the target was renamed aside. */
    CHECK(fs->rename(fs->ctx, "ux0:data/vitaFTPShell/sites.cfg",
                     "ux0:data/vitaFTPShell/sites.cfg.old") == FS_OK);
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg"));

    SiteBook loaded;
    site_book_init(&loaded);
    CHECK(store_load_sites(fs, STORE_DIR, &loaded) == STORE_OK);
    CHECK(loaded.count == 2);
    CHECK(fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg"));
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg.old"));

    /* A stale aside beside a live target is cleared by the next save. */
    fake_fs_seed_file("ux0:data/vitaFTPShell/sites.cfg.old", "stale", 5);
    CHECK(store_save_sites(fs, STORE_DIR, &book) == STORE_OK);
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/sites.cfg.old"));
}

static void test_settings(void)
{
    setup_fs();
    FsPort *fs = fake_fs_port();

    Settings settings;
    settings_init(&settings);
    strncpy(settings.last_local, "ur0:data", sizeof(settings.last_local) - 1);
    strncpy(settings.last_site, "my site", sizeof(settings.last_site) - 1);
    settings.server_port = 2121;

    CHECK(store_save_settings(fs, STORE_DIR, &settings) == STORE_OK);
    CHECK(!fake_fs_exists("ux0:data/vitaFTPShell/settings.cfg.tmp"));

    Settings loaded;
    settings_init(&loaded);
    CHECK(store_load_settings(fs, STORE_DIR, &loaded) == STORE_OK);
    CHECK(strcmp(loaded.last_local, "ur0:data") == 0);
    CHECK(strcmp(loaded.last_site, "my site") == 0);
    CHECK(loaded.server_port == 2121);

    /* A bad header leaves a defined default, not the caller's sentinel. */
    const char *bad = "vitaFTPShell-settings 99\nlast_local=ur0:data\n";
    fake_fs_seed_file("ux0:data/vitaFTPShell/settings.cfg", bad, strlen(bad));
    Settings sentinel;
    memset(&sentinel, 0x41, sizeof(sentinel));
    CHECK(store_load_settings(fs, STORE_DIR, &sentinel) == STORE_BAD_VERSION);
    CHECK(strcmp(sentinel.last_local, "ux0:data") == 0);
    CHECK(sentinel.server_port == 1337);

    /* A deep last_local must still serialize. */
    Settings deep;
    settings_init(&deep);
    memset(deep.last_local, ' ', sizeof(deep.last_local) - 1);
    deep.last_local[sizeof(deep.last_local) - 1] = '\0';
    CHECK(store_save_settings(fs, STORE_DIR, &deep) == STORE_OK);
    CHECK(store_load_settings(fs, STORE_DIR, &loaded) == STORE_OK);
    CHECK(strcmp(loaded.last_local, deep.last_local) == 0);

    /* An overlong line is a partial load, not a silent success. */
    char long_file[8192];
    const char *header = "vitaFTPShell-settings 1\n";
    size_t at = strlen(header);
    memcpy(long_file, header, at);
    memset(long_file + at, 'x', 5000);
    at += 5000;
    long_file[at++] = '\n';
    fake_fs_seed_file("ux0:data/vitaFTPShell/settings.cfg", long_file, at);
    CHECK(store_load_settings(fs, STORE_DIR, &loaded) == STORE_PARTIAL);
}

int main(void)
{
    test_round_trip();
    test_bad_header();
    test_partial();
    test_rename_failure_keeps_original();
    test_replace_fallback();
    test_stale_aside();
    test_settings();

    if (s_failures != 0) {
        fprintf(stderr, "test_store: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_store: OK\n");
    return 0;
}
