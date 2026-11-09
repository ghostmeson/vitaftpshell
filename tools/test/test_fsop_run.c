/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/fsop_run.h"
#include "fake_fs.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

#define FACTS_MAX 64

static FsOpFact s_facts[FACTS_MAX];
static int s_fact_count;
static FsOpPlan s_plan;

static void facts_reset(void)
{
    memset(s_facts, 0, sizeof(s_facts));
    s_fact_count = 0;
}

static void fact(const char *path, FsEntryType type, uint64_t size, int exists)
{
    FsOpFact *f = &s_facts[s_fact_count++];
    f->path = path;
    f->type = type;
    f->size = size;
    f->size_known = type == FS_ENTRY_FILE;
    f->exists = exists;
}

static FsOpFacts facts(void)
{
    FsOpFacts f;
    f.items = s_facts;
    f.count = s_fact_count;
    return f;
}

static void setup(void)
{
    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ux0:data/src");
    fake_fs_add_dir("ux0:data/dst");
    fake_fs_add_dir("ur0:");
    fake_fs_add_dir("ur0:data");
}

static int build_copy(const char *src, const char *dest_dir,
                      FsOpConflict conflict)
{
    const char *sources[] = {src};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_COPY, sources, 1, dest_dir, &f, conflict};
    return fsop_plan_build(&s_plan, &req);
}

static int build_move(const char *src, const char *dest_dir,
                      FsOpConflict conflict)
{
    const char *sources[] = {src};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_MOVE, sources, 1, dest_dir, &f, conflict};
    return fsop_plan_build(&s_plan, &req);
}

static int build_delete(const char *const *sources, int count)
{
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_DELETE, sources, count, NULL, &f,
                       FSOP_CONFLICT_ASK};
    return fsop_plan_build(&s_plan, &req);
}

static void seed(const char *path, const char *text)
{
    fake_fs_seed_file(path, text, strlen(text));
}

static int content_is(const char *path, const char *expected)
{
    char buf[64];
    size_t n = fake_fs_read_file(path, buf, sizeof(buf));
    return n == strlen(expected) && memcmp(buf, expected, n) == 0;
}

static FsOpProgress run(FsOpCancelFn cancel, void *ctx)
{
    FsOpProgress progress;
    fsop_run(fake_fs_port(), &s_plan, &progress, cancel, ctx);
    return progress;
}

static void test_copy_replace(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_set_rename_replaces(1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.files_done == 1 && p.files_total == 1);
    CHECK(content_is("ux0:data/dst/a.bin", "NEW"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.old"));
    CHECK(fake_fs_sync_count() == 2);
}

static void test_copy_aside_real(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_set_rename_replaces(0);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_APP);
    CHECK(content_is("ux0:data/dst/a.bin", "NEW"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.old"));
}

static void test_stale_aside(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin.vftp.old", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_fail_op(FAKE_FS_OP_WRITE, FS_IO);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.old"));
}

typedef struct {
    int calls;
    int cancel_on;
} CancelCtx;

static int cancel_after(void *ctx)
{
    CancelCtx *c = ctx;
    c->calls++;
    return c->calls >= c->cancel_on;
}

static void test_cancel_copy(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_set_rename_replaces(1);
    CancelCtx ctx = {0, 2};
    FsOpProgress p = run(cancel_after, &ctx);
    CHECK(p.cause == OP_CAUSE_USER);
    CHECK(p.files_done == 0);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
}

static void test_write_failure(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_fail_op(FAKE_FS_OP_WRITE, FS_IO);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
}

static void test_open_close_failures(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_fail_op(FAKE_FS_OP_OPEN, FS_IO);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));

    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ux0:data/src");
    fake_fs_add_dir("ux0:data/dst");
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_fail_op(FAKE_FS_OP_CLOSE, FS_IO);
    p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
}

static void test_sync_failures(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_set_rename_replaces(1);
    fake_fs_fail_sync_at(0);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));

    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ux0:data/src");
    fake_fs_add_dir("ux0:data/dst");
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    fake_fs_set_rename_replaces(1);
    fake_fs_fail_sync_at(1);
    FsOpProgress after = run(NULL, NULL);
    CHECK(after.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "NEW"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
}

static void test_rename_failures(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_set_rename_replaces(0);
    fake_fs_fail_rename_at(0);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));

    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ux0:data/src");
    fake_fs_add_dir("ux0:data/dst");
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    fake_fs_set_rename_replaces(0);
    fake_fs_fail_rename_at(1);
    FsOpProgress after = run(NULL, NULL);
    CHECK(after.cause == OP_CAUSE_STORAGE);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.part"));
    CHECK(!fake_fs_exists("ux0:data/dst/a.bin.vftp.old"));
}

static void test_move_success(void)
{
    setup();
    seed("ux0:data/src/a.bin", "MOVE");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 4, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);

    CHECK(build_move("ux0:data/src/a.bin", "ur0:data",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_APP);
    CHECK(!fake_fs_exists("ux0:data/src/a.bin"));
    CHECK(content_is("ur0:data/a.bin", "MOVE"));
    CHECK(!fake_fs_exists("ur0:data/a.bin.vftp.part"));
}

static void test_move_delete_failure(void)
{
    setup();
    seed("ux0:data/src/a.bin", "MOVE");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 4, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);

    CHECK(build_move("ux0:data/src/a.bin", "ur0:data",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_fail_remove_at(1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(strstr(p.message, "NOTHING CHANGED") != NULL);
    CHECK(fake_fs_exists("ux0:data/src/a.bin"));
    CHECK(!fake_fs_exists("ur0:data/a.bin"));
}

static void test_move_overwrite_delete_failure(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ur0:data/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);
    fact("ur0:data/a.bin", FS_ENTRY_FILE, 3, 1);

    CHECK(build_move("ux0:data/src/a.bin", "ur0:data",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    fake_fs_fail_remove_at(1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(strstr(p.message, "DESTINATION KEPT") != NULL);
    CHECK(fake_fs_exists("ux0:data/src/a.bin"));
    CHECK(content_is("ur0:data/a.bin", "NEW"));
}

static void test_move_incomplete(void)
{
    setup();
    seed("ux0:data/src/a.bin", "A");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 1, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);

    const char *sources[] = {"ux0:data/src/a.bin"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_MOVE, sources, 1, "ur0:data", &f,
                       FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    fake_fs_fail_remove_from(1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(strstr(p.message, "MOVE INCOMPLETE") != NULL);
    CHECK(strstr(p.message, "ux0:data/src/a.bin") != NULL);
    CHECK(strstr(p.message, "ur0:data/a.bin") != NULL);
    CHECK(fake_fs_exists("ux0:data/src/a.bin"));
    CHECK(fake_fs_exists("ur0:data/a.bin"));
}

static void test_move_batch_partial(void)
{
    setup();
    seed("ux0:data/src/a.bin", "A");
    seed("ux0:data/src/b.bin", "B");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/src/b.bin", FS_ENTRY_FILE, 1, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);

    const char *sources[] = {"ux0:data/src/a.bin", "ux0:data/src/b.bin"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_MOVE, sources, 2, "ur0:data", &f,
                       FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    /* First source remove succeeds, second fails; its destination is removed. */
    fake_fs_fail_remove_at(3);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(strstr(p.message, "1 OF 2 MOVED") != NULL);
    CHECK(!fake_fs_exists("ux0:data/src/a.bin"));
    CHECK(content_is("ur0:data/a.bin", "A"));
    CHECK(fake_fs_exists("ux0:data/src/b.bin"));
    CHECK(!fake_fs_exists("ur0:data/b.bin"));
}

static void test_skip(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/dst/a.bin", FS_ENTRY_FILE, 3, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_SKIP) == 1);
    CHECK(s_plan.step_count == 0);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_APP);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
}

static void test_merge(void)
{
    setup();
    fake_fs_add_dir("ux0:data/src/sub");
    seed("ux0:data/src/a.txt", "A");
    seed("ux0:data/src/sub/b.txt", "B");
    fake_fs_add_dir("ux0:data/dst/src");
    seed("ux0:data/dst/src/keep.txt", "KEEP");

    facts_reset();
    fact("ux0:data/src", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/src/a.txt", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/src/sub", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/src/sub/b.txt", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/dst/src", FS_ENTRY_DIR, 0, 1);

    CHECK(build_copy("ux0:data/src", "ux0:data/dst",
                     FSOP_CONFLICT_OVERWRITE) == 1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_APP);
    CHECK(content_is("ux0:data/dst/src/a.txt", "A"));
    CHECK(content_is("ux0:data/dst/src/sub/b.txt", "B"));
    CHECK(content_is("ux0:data/dst/src/keep.txt", "KEEP"));
}

typedef struct {
    int done_at;
    FsOpProgress *progress;
} ProgressCancel;

static int cancel_when_done(void *ctx)
{
    ProgressCancel *c = ctx;
    return c->progress->files_done >= c->done_at;
}

static void test_delete_cancel_two(void)
{
    static const char *names[] = {
        "ux0:data/f1", "ux0:data/f2", "ux0:data/f3",
        "ux0:data/f4", "ux0:data/f5"};

    setup();
    for (int i = 0; i < 5; ++i)
        seed(names[i], "X");
    facts_reset();
    for (int i = 0; i < 5; ++i)
        fact(names[i], FS_ENTRY_FILE, 1, 1);
    CHECK(build_delete(names, 5) == 1);

    FsOpProgress progress;
    memset(&progress, 0, sizeof(progress));
    ProgressCancel ctx = {2, &progress};
    FsOpRunResult r = fsop_run(fake_fs_port(), &s_plan, &progress,
                               cancel_when_done, &ctx);
    CHECK(r == FSOP_RUN_PARTIAL);
    CHECK(progress.files_done == 2);
    CHECK(progress.files_total == 5);
    CHECK(strcmp(progress.message, "DELETED 2 OF 5") == 0);
    CHECK(fake_fs_exists("ux0:data/f3"));
    CHECK(fake_fs_exists("ux0:data/f4"));
    CHECK(fake_fs_exists("ux0:data/f5"));
}

static void test_aside_cleanup_failure(void)
{
    setup();
    seed("ux0:data/OLD.TXT", "OLD");
    seed("ux0:data/NEW.TXT", "NEW");
    facts_reset();
    fact("ux0:data/OLD.TXT", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/NEW.TXT", FS_ENTRY_FILE, 3, 1);
    const char *sources[] = {"ux0:data/OLD.TXT"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_RENAME, sources, 1, "ux0:data/NEW.TXT", &f,
                       FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    fake_fs_set_rename_replaces(0);
    fake_fs_fail_remove_at(0);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_STORAGE);
    CHECK(strstr(p.message, "ASIDE CLEANUP FAILED") != NULL);
    CHECK(fake_fs_exists("ux0:data/NEW.TXT.vftp.old"));
}

static void test_mkdir_rename(void)
{
    setup();
    facts_reset();
    fact("ux0:data/NEW", FS_ENTRY_DIR, 0, 0);
    FsOpFacts f = facts();
    FsOpRequest mkdir = {FSOP_MKDIR, NULL, 0, "ux0:data/NEW", &f,
                         FSOP_CONFLICT_ASK};
    CHECK(fsop_plan_build(&s_plan, &mkdir) == 1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_APP);
    CHECK(fake_fs_exists("ux0:data/NEW"));

    seed("ux0:data/OLD.TXT", "R");
    facts_reset();
    fact("ux0:data/OLD.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/NEW.TXT", FS_ENTRY_FILE, 1, 0);
    const char *sources[] = {"ux0:data/OLD.TXT"};
    FsOpFacts f2 = facts();
    FsOpRequest rename = {FSOP_RENAME, sources, 1, "ux0:data/NEW.TXT", &f2,
                          FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &rename) == 1);
    p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_APP);
    CHECK(!fake_fs_exists("ux0:data/OLD.TXT"));
    CHECK(content_is("ux0:data/NEW.TXT", "R"));
}

static void test_conflict_undecided(void)
{
    setup();
    seed("ux0:data/src/a.bin", "NEW");
    seed("ux0:data/dst/a.bin", "OLD");
    facts_reset();
    fact("ux0:data/src/a.bin", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/dst", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/dst/a.bin", FS_ENTRY_FILE, 3, 1);

    CHECK(build_copy("ux0:data/src/a.bin", "ux0:data/dst",
                     FSOP_CONFLICT_ASK) == 1);
    CHECK(s_plan.needs_conflict == 1);
    FsOpProgress p = run(NULL, NULL);
    CHECK(p.cause == OP_CAUSE_APP);
    CHECK(strstr(p.message, "CONFLICT") != NULL);
    CHECK(content_is("ux0:data/dst/a.bin", "OLD"));
}

int main(void)
{
    test_copy_replace();
    test_copy_aside_real();
    test_stale_aside();
    test_cancel_copy();
    test_write_failure();
    test_open_close_failures();
    test_sync_failures();
    test_rename_failures();
    test_move_success();
    test_move_delete_failure();
    test_move_overwrite_delete_failure();
    test_move_incomplete();
    test_move_batch_partial();
    test_skip();
    test_merge();
    test_delete_cancel_two();
    test_aside_cleanup_failure();
    test_mkdir_rename();
    test_conflict_undecided();

    if (s_failures != 0) {
        fprintf(stderr, "test_fsop_run: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_fsop_run: OK\n");
    return 0;
}
