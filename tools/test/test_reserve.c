/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/reserve.h"

static int s_failures;
static Reserve s_reserve;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void reset(void)
{
    reserve_reset(&s_reserve);
}

static int conflicts(ReserveKind kind, const char *path)
{
    return reserve_conflict(&s_reserve, kind, path, NULL, 0);
}

static void test_equal(void)
{
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/a.bin"));
    CHECK(conflicts(RESERVE_WRITE, "UX0:DATA/A.BIN"));
    CHECK(conflicts(RESERVE_TREE, "ux0:data/a.bin"));
    CHECK(!conflicts(RESERVE_WRITE, "ux0:data/b.bin"));

    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_READ,
                      "ux0:data/a.bin"));
    CHECK(!conflicts(RESERVE_READ, "ux0:data/a.bin"));
    CHECK(conflicts(RESERVE_WRITE, "ux0:data/a.bin"));
}

static void test_ancestor(void)
{
    reset();
    /* A local tree delete blocks a transfer inside it. */
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_LOCAL, RESERVE_TREE,
                      "ux0:data"));
    CHECK(conflicts(RESERVE_WRITE, "ux0:data/a.bin"));
    CHECK(conflicts(RESERVE_READ, "ux0:data/a.bin"));

    /* A held child blocks a tree operation on its parent. */
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/a.bin"));
    CHECK(conflicts(RESERVE_TREE, "ux0:data"));
    CHECK(!conflicts(RESERVE_WRITE, "ux0:data/sub/b.bin"));

    /* Siblings never conflict. */
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_LOCAL, RESERVE_TREE,
                      "ux0:data/left"));
    CHECK(!conflicts(RESERVE_WRITE, "ux0:data/right/a"));
    CHECK(!conflicts(RESERVE_WRITE, "ux0:data/leftish/a"));
}

static void test_server_reads(void)
{
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/a.bin"));
    /* Listing the folder is allowed; the file itself is refused. */
    CHECK(!conflicts(RESERVE_READ, "ux0:data"));
    CHECK(conflicts(RESERVE_READ, "ux0:data/a.bin"));
    CHECK(conflicts(RESERVE_READ,
                    "ux0:data/a.bin.vftp.part"));
    CHECK(conflicts(RESERVE_WRITE, "ux0:data/a.bin"));

    /* Reading inside a held tree is refused. */
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_LOCAL, RESERVE_TREE,
                      "ux0:data/tree"));
    CHECK(conflicts(RESERVE_READ, "ux0:data/tree/a.bin"));
    CHECK(!conflicts(RESERVE_READ, "ux0:data"));
}

static void test_sidecars(void)
{
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/a.bin"));
    CHECK(conflicts(RESERVE_WRITE,
                    "ux0:data/a.bin.vftp.part"));
    CHECK(conflicts(RESERVE_WRITE,
                    "ux0:data/a.bin.vftp.old"));

    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/a.bin.vftp.part"));
    CHECK(conflicts(RESERVE_WRITE, "ux0:data/a.bin"));

    CHECK(reserve_is_sidecar("UX0:DATA/A.BIN.VFTP.PART"));
    CHECK(reserve_is_sidecar("a.vftp.old"));
    CHECK(!reserve_is_sidecar("a.bin"));
}

static void test_release(void)
{
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_LOCAL, RESERVE_WRITE,
                      "ux0:data/a.bin"));
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/b.bin"));
    reserve_release(&s_reserve, RESERVE_LANE_LOCAL);
    CHECK(!conflicts(RESERVE_WRITE, "ux0:data/a.bin"));
    CHECK(conflicts(RESERVE_WRITE, "ux0:data/b.bin"));
    reserve_release(&s_reserve, RESERVE_LANE_NETWORK);
    CHECK(s_reserve.count == 0);
}

/* Release must reclaim the arena, or a long session eventually fails every
 * add with a spurious "too large". */
static void test_release_reclaims_arena(void)
{
    reset();
    char path[PATH_MAX_LEN];
    for (int round = 0; round < 9; ++round) {
        for (int i = 0; i < 1000; ++i) {
            snprintf(path, sizeof(path), "ux0:data/round-%d-file-%04d.bin",
                     round, i);
            CHECK(reserve_add(&s_reserve, RESERVE_LANE_LOCAL, RESERVE_WRITE,
                              path));
        }
        reserve_release(&s_reserve, RESERVE_LANE_LOCAL);
    }
    CHECK(s_reserve.count == 0);
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/final.bin"));
}

static void test_blocker(void)
{
    reset();
    CHECK(reserve_add(&s_reserve, RESERVE_LANE_NETWORK, RESERVE_WRITE,
                      "ux0:data/a.bin"));
    char blocker[PATH_MAX_LEN];
    CHECK(reserve_conflict(&s_reserve, RESERVE_WRITE, "ux0:data/a.bin",
                           blocker, sizeof(blocker)));
    CHECK(strcmp(blocker, "ux0:data/a.bin") == 0);
}

int main(void)
{
    test_equal();
    test_ancestor();
    test_server_reads();
    test_sidecars();
    test_release();
    test_release_reclaims_arena();
    test_blocker();

    if (s_failures != 0) {
        fprintf(stderr, "test_reserve: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_reserve: OK\n");
    return 0;
}
