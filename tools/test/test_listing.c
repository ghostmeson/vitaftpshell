#include <stdio.h>
#include <string.h>

#include "core/listing.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_mlsd(void)
{
    ListingEntry e;
    CHECK(listing_parse_mlsd(
              "type=dir;size=0;modify=20260101120000; name with spaces",
              &e) == LISTING_OK);
    CHECK(e.type == LISTING_DIR);
    CHECK(strcmp(e.name, "name with spaces") == 0);
    CHECK(e.size_known == 1 && e.size == 0);
    CHECK(e.mtime_known == 1 && e.mtime == 20260101120000ULL);

    CHECK(listing_parse_mlsd("type=file; name", &e) == LISTING_OK);
    CHECK(e.type == LISTING_FILE);
    CHECK(e.size_known == 0);

    CHECK(listing_parse_mlsd("type=cdir; name .", &e) == LISTING_SKIP);
    CHECK(listing_parse_mlsd("type=pdir; name ..", &e) == LISTING_SKIP);
    CHECK(listing_parse_mlsd("size=1; name", &e) == LISTING_MALFORMED);

    CHECK(listing_parse_mlsd("type=file;size=18446744073709551617; x", &e) ==
          LISTING_OK);
    CHECK(e.size_known == 0);
    CHECK(listing_parse_mlsd("type=file;size=1234L; x", &e) == LISTING_OK);
    CHECK(e.size_known == 0);
}

static void test_unix(void)
{
    ListingEntry e;
    CHECK(listing_parse_unix("-rw-r--r-- 1 u g 1234 Nov 08 12:00 FILE NAME",
                             &e) == LISTING_OK);
    CHECK(e.type == LISTING_FILE);
    CHECK(strcmp(e.name, "FILE NAME") == 0);
    CHECK(e.size_known == 1 && e.size == 1234);

    CHECK(listing_parse_unix("drwxr-xr-x 2 u g 4096 Nov 08 2025 FOLDER", &e) ==
          LISTING_OK);
    CHECK(e.type == LISTING_DIR);
    CHECK(strcmp(e.name, "FOLDER") == 0);

    CHECK(listing_parse_unix("total 48", &e) == LISTING_SKIP);
    CHECK(listing_parse_unix("-rw-r--r-- 1 u g 5 Nov 08 12:00 .", &e) ==
          LISTING_SKIP);
    CHECK(listing_parse_unix("drwxr-xr-x 2 u g 5 Nov 08 12:00 ..", &e) ==
          LISTING_SKIP);
}

static void test_dos(void)
{
    ListingEntry e;
    CHECK(listing_parse_dos("11-08-26  12:00PM       <DIR>          FOLDER",
                            &e) == LISTING_OK);
    CHECK(e.type == LISTING_DIR);
    CHECK(strcmp(e.name, "FOLDER") == 0);
    CHECK(e.size_known == 0);

    CHECK(listing_parse_dos(
              "11-08-26  12:00PM             1234 FILE NAME.TXT",
              &e) == LISTING_OK);
    CHECK(e.type == LISTING_FILE);
    CHECK(strcmp(e.name, "FILE NAME.TXT") == 0);
    CHECK(e.size_known == 1 && e.size == 1234);
}

static void test_sort(void)
{
    ListingEntry entries[3];
    memset(entries, 0, sizeof(entries));

    strcpy(entries[0].name, "z.txt");
    entries[0].type = LISTING_FILE;
    strcpy(entries[1].name, "adir");
    entries[1].type = LISTING_DIR;
    strcpy(entries[2].name, "a.txt");
    entries[2].type = LISTING_FILE;

    listing_sort(entries, 3);
    CHECK(entries[0].type == LISTING_DIR);
    CHECK(strcmp(entries[0].name, "adir") == 0);
    CHECK(strcmp(entries[1].name, "a.txt") == 0);
    CHECK(strcmp(entries[2].name, "z.txt") == 0);
}

static void test_overlong_name(void)
{
    char line[512];
    ListingEntry e;

    int n = snprintf(line, sizeof(line),
                     "drwxr-xr-x 2 u g 5 Nov 08 12:00 ");
    memset(line + n, 'a', 300);
    line[n + 300] = '\0';
    CHECK(listing_parse_unix(line, &e) == LISTING_MALFORMED);

    n = snprintf(line, sizeof(line), "type=file;size=5; ");
    memset(line + n, 'a', 300);
    line[n + 300] = '\0';
    CHECK(listing_parse_mlsd(line, &e) == LISTING_MALFORMED);

    n = snprintf(line, sizeof(line), "11-08-26  12:00PM             5 ");
    memset(line + n, 'a', 300);
    line[n + 300] = '\0';
    CHECK(listing_parse_dos(line, &e) == LISTING_MALFORMED);
}

int main(void)
{
    test_mlsd();
    test_unix();
    test_dos();
    test_sort();
    test_overlong_name();

    if (s_failures != 0) {
        fprintf(stderr, "test_listing: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_listing: OK\n");
    return 0;
}
