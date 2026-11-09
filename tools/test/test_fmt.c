/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/fmt.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

int main(void)
{
    char buf[32];

    fmt_size(buf, sizeof(buf), 999, 1);
    CHECK(strcmp(buf, "999 B") == 0);

    fmt_size(buf, sizeof(buf), 1229, 1);
    CHECK(strcmp(buf, "1.2 KB") == 0);

    fmt_size(buf, sizeof(buf), 124518400, 1);
    CHECK(strcmp(buf, "118.7 MB") == 0);

    fmt_size(buf, sizeof(buf), 0, 0);
    CHECK(strcmp(buf, "--") == 0);

    fmt_rate(buf, sizeof(buf), 1229, 1);
    CHECK(strcmp(buf, "1.2 KB/S") == 0);

    fmt_percent(buf, sizeof(buf), 1, 2, 1);
    CHECK(strcmp(buf, "50%") == 0);
    fmt_percent(buf, sizeof(buf), 1, 2, 0);
    CHECK(strcmp(buf, "--") == 0);
    fmt_percent(buf, sizeof(buf), 2, 2, 1);
    CHECK(strcmp(buf, "100%") == 0);

    fmt_count(buf, sizeof(buf), 1, "ITEM", "ITEMS");
    CHECK(strcmp(buf, "1 ITEM") == 0);
    fmt_count(buf, sizeof(buf), 3, "ITEM", "ITEMS");
    CHECK(strcmp(buf, "3 ITEMS") == 0);

    if (s_failures != 0) {
        fprintf(stderr, "test_fmt: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_fmt: OK\n");
    return 0;
}
