/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/fsop_plan.h"

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

static int step_dest_index(const FsOpPlan *plan, FsOpStepKind kind,
                           const char *dest)
{
    for (int i = 0; i < plan->step_count; ++i) {
        if (plan->steps[i].kind == (uint8_t)kind &&
            strcmp(fsop_plan_step_dest(plan, i), dest) == 0)
            return i;
    }
    return -1;
}

static int step_source_index(const FsOpPlan *plan, FsOpStepKind kind,
                             const char *source)
{
    for (int i = 0; i < plan->step_count; ++i) {
        if (plan->steps[i].kind == (uint8_t)kind &&
            strcmp(fsop_plan_step_source(plan, i), source) == 0)
            return i;
    }
    return -1;
}

static void test_folder_copy(void)
{
    facts_reset();
    fact("ux0:data/SRC", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/SRC/A.TXT", FS_ENTRY_FILE, 5, 1);
    fact("ux0:data/SRC/SUB", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/SRC/SUB/B.TXT", FS_ENTRY_FILE, 7, 1);

    const char *sources[] = {"ux0:data/SRC"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_COPY, sources, 1, "ux0:data/DST", &f,
                       FSOP_CONFLICT_OVERWRITE};

    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(s_plan.step_count == 4);
    CHECK(s_plan.item_count == 1);
    CHECK(s_plan.total_known == 1 && s_plan.total_bytes == 12);

    int dst = step_dest_index(&s_plan, FSOP_STEP_MKDIR, "ux0:data/DST/SRC");
    int sub = step_dest_index(&s_plan, FSOP_STEP_MKDIR, "ux0:data/DST/SRC/SUB");
    CHECK(dst >= 0);
    CHECK(sub >= 0);
    CHECK(dst < sub);
    CHECK(step_dest_index(&s_plan, FSOP_STEP_COPY_FILE,
                          "ux0:data/DST/SRC/A.TXT") >= 0);
    CHECK(step_dest_index(&s_plan, FSOP_STEP_COPY_FILE,
                          "ux0:data/DST/SRC/SUB/B.TXT") >= 0);
}

static void test_copy_into_self(void)
{
    facts_reset();
    fact("ux0:data/SRC", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/SRC/SUB", FS_ENTRY_DIR, 0, 1);

    const char *sources[] = {"ux0:data/SRC"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_COPY, sources, 1, "ux0:data/SRC/SUB", &f,
                       FSOP_CONFLICT_OVERWRITE};

    CHECK(fsop_plan_build(&s_plan, &req) == 0);
    CHECK(s_plan.ok == 0);
    CHECK(strstr(s_plan.reason, "ITSELF") != NULL);
}

static void test_cross_volume_move(void)
{
    facts_reset();
    fact("ux0:data/A.TXT", FS_ENTRY_FILE, 9, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);

    const char *sources[] = {"ux0:data/A.TXT"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_MOVE, sources, 1, "ur0:data", &f,
                       FSOP_CONFLICT_OVERWRITE};

    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    CHECK(s_plan.ok == 1);
    int copy = step_dest_index(&s_plan, FSOP_STEP_COPY_FILE, "ur0:data/A.TXT");
    CHECK(copy >= 0);
    CHECK(s_plan.steps[copy].move_source == 1);
    /* A completed move is never rolled back: that would delete the only copy. */
    CHECK(s_plan.rollback_count == 0);
}

static void test_same_folder(void)
{
    facts_reset();
    fact("ux0:data/A.TXT", FS_ENTRY_FILE, 3, 1);

    const char *sources[] = {"ux0:data/A.TXT"};
    FsOpFacts f = facts();
    FsOpRequest copy = {FSOP_COPY, sources, 1, "ux0:data", &f,
                        FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &copy) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(step_dest_index(&s_plan, FSOP_STEP_COPY_FILE,
                          "ux0:data/A copy.TXT") >= 0);

    FsOpRequest move = {FSOP_MOVE, sources, 1, "ux0:data", &f,
                        FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &move) == 0);
    CHECK(strstr(s_plan.reason, "ALREADY IN THIS FOLDER") != NULL);
}

static void test_batch_conflict(void)
{
    facts_reset();
    fact("ux0:data/A.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/B.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/C.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/D.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data/E.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);
    fact("ur0:data/A.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ur0:data/B.TXT", FS_ENTRY_FILE, 1, 1);

    const char *sources[] = {"ux0:data/A.TXT", "ux0:data/B.TXT",
                             "ux0:data/C.TXT", "ux0:data/D.TXT",
                             "ux0:data/E.TXT"};
    FsOpFacts f = facts();
    FsOpRequest ask = {FSOP_COPY, sources, 5, "ur0:data", &f,
                       FSOP_CONFLICT_ASK};
    CHECK(fsop_plan_build(&s_plan, &ask) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(s_plan.needs_conflict == 1);
    CHECK(s_plan.existing_count == 2);
    CHECK(s_plan.step_count == 0);

    FsOpRequest overwrite = {FSOP_COPY, sources, 5, "ur0:data", &f,
                             FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &overwrite) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(!s_plan.needs_conflict);
    CHECK(s_plan.step_count == 5);
    CHECK(s_plan.item_count == 5);
}

static void test_risk(void)
{
    facts_reset();
    fact("ur0:tai/config.txt", FS_ENTRY_FILE, 4, 1);
    fact("ux0:data", FS_ENTRY_DIR, 0, 1);

    const char *sources[] = {"ur0:tai/config.txt"};
    FsOpFacts f = facts();
    FsOpRequest marked = {FSOP_COPY, sources, 1, "ux0:data", &f,
                          FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &marked) == 1);
    CHECK(s_plan.high_risk == 1);
    CHECK(strcmp(s_plan.risk_path.text, "ur0:tai/config.txt") == 0);

    facts_reset();
    fact("ux0:data/A.TXT", FS_ENTRY_FILE, 1, 1);
    fact("ur0:tai", FS_ENTRY_DIR, 0, 1);
    const char *plain[] = {"ux0:data/A.TXT"};
    FsOpFacts f2 = facts();
    FsOpRequest dest = {FSOP_COPY, plain, 1, "ur0:tai", &f2,
                        FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &dest) == 1);
    CHECK(s_plan.high_risk == 1);
    CHECK(strcmp(s_plan.risk_path.text, "ur0:tai/A.TXT") == 0);

    facts_reset();
    fact("ur0:tai/NEW", FS_ENTRY_DIR, 0, 0);
    FsOpFacts f3 = facts();
    FsOpRequest mkdir = {FSOP_MKDIR, NULL, 0, "ur0:tai/NEW", &f3,
                         FSOP_CONFLICT_ASK};
    CHECK(fsop_plan_build(&s_plan, &mkdir) == 1);
    CHECK(s_plan.high_risk == 1);
}

static void test_merge(void)
{
    facts_reset();
    fact("ux0:data/SRC", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/SRC/A.TXT", FS_ENTRY_FILE, 2, 1);
    fact("ux0:data/SRC/SUB", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/SRC/SUB/B.TXT", FS_ENTRY_FILE, 3, 1);
    fact("ux0:data/DST", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data/DST/SRC", FS_ENTRY_DIR, 0, 1);

    const char *sources[] = {"ux0:data/SRC"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_COPY, sources, 1, "ux0:data/DST", &f,
                       FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(step_dest_index(&s_plan, FSOP_STEP_MKDIR, "ux0:data/DST/SRC") == -1);
    CHECK(step_dest_index(&s_plan, FSOP_STEP_MKDIR, "ux0:data/DST/SRC/SUB") >= 0);
    CHECK(step_dest_index(&s_plan, FSOP_STEP_DELETE_DIR, "ux0:data/DST") == -1);
}

static void test_volume_root_source(void)
{
    facts_reset();
    fact("ux0:", FS_ENTRY_DIR, 0, 1);
    fact("ux0:data", FS_ENTRY_DIR, 0, 1);
    const char *sources[] = {"ux0:"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_COPY, sources, 1, "ux0:data", &f,
                       FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &req) == 0);
    CHECK(strstr(s_plan.reason, "VOLUME ROOT") != NULL);
}

static void test_unknown_total(void)
{
    facts_reset();
    fact("ux0:data/A.TXT", FS_ENTRY_FILE, 5, 1);
    fact("ux0:data/B.TXT", FS_ENTRY_FILE, 0, 1);
    s_facts[s_fact_count - 1].size_known = 0;
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);
    const char *sources[] = {"ux0:data/A.TXT", "ux0:data/B.TXT"};
    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_COPY, sources, 2, "ur0:data", &f,
                       FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    CHECK(s_plan.total_known == 0);
    CHECK(s_plan.total_bytes == 5);
}

static void test_delete_protection(void)
{
    facts_reset();
    fact("ux0:", FS_ENTRY_DIR, 0, 1);
    fact("ur0:data", FS_ENTRY_DIR, 0, 1);
    fact("ur0:tai/boot_config.txt", FS_ENTRY_FILE, 1, 1);
    fact("ux0:data", FS_ENTRY_DIR, 0, 1);
    FsOpFacts f = facts();

    const char *root[] = {"ux0:"};
    FsOpRequest req = {FSOP_DELETE, root, 1, NULL, &f, FSOP_CONFLICT_ASK};
    CHECK(fsop_plan_build(&s_plan, &req) == 0);
    CHECK(strstr(s_plan.reason, "VOLUME ROOT") != NULL);

    const char *data[] = {"ur0:data"};
    req.sources = data;
    CHECK(fsop_plan_build(&s_plan, &req) == 0);
    CHECK(strstr(s_plan.reason, "PROTECTED PATH") != NULL);

    const char *boot[] = {"ur0:tai/boot_config.txt"};
    req.sources = boot;
    CHECK(fsop_plan_build(&s_plan, &req) == 0);
    CHECK(strstr(s_plan.reason, "BOOT CONFIG DENIED") != NULL);

    const char *app[] = {"ux0:data"};
    req.sources = app;
    CHECK(fsop_plan_build(&s_plan, &req) == 0);
    CHECK(strstr(s_plan.reason, "PROTECTED PATH") != NULL);
}

static void test_twelve_delete(void)
{
    static const char *names[] = {
        "ux0:data/F01", "ux0:data/F02", "ux0:data/F03", "ux0:data/F04",
        "ux0:data/F05", "ux0:data/F06", "ux0:data/F07", "ux0:data/F08",
        "ux0:data/F09", "ux0:data/F10", "ux0:data/F11", "ux0:data/F12"};

    facts_reset();
    for (int i = 0; i < 12; ++i)
        fact(names[i], FS_ENTRY_FILE, 1, 1);

    FsOpFacts f = facts();
    FsOpRequest req = {FSOP_DELETE, names, 12, NULL, &f, FSOP_CONFLICT_ASK};
    CHECK(fsop_plan_build(&s_plan, &req) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(s_plan.item_count == 12);
    CHECK(s_plan.step_count == 12);
    CHECK(s_plan.total_bytes == 12);
}

static void test_rename_and_mkdir(void)
{
    facts_reset();
    fact("ur0:tai/a.txt", FS_ENTRY_FILE, 1, 1);
    fact("ur0:tai/b.txt", FS_ENTRY_FILE, 1, 0);
    const char *sources[] = {"ur0:tai/a.txt"};
    FsOpFacts f = facts();
    FsOpRequest rename = {FSOP_RENAME, sources, 1, "ur0:tai/b.txt", &f,
                          FSOP_CONFLICT_OVERWRITE};
    CHECK(fsop_plan_build(&s_plan, &rename) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(s_plan.high_risk == 1);
    CHECK(step_source_index(&s_plan, FSOP_STEP_RENAME, "ur0:tai/a.txt") >= 0);

    facts_reset();
    fact("ux0:data/NEW", FS_ENTRY_DIR, 0, 0);
    FsOpFacts f2 = facts();
    FsOpRequest mkdir = {FSOP_MKDIR, NULL, 0, "ux0:data/NEW", &f2,
                         FSOP_CONFLICT_ASK};
    CHECK(fsop_plan_build(&s_plan, &mkdir) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(step_dest_index(&s_plan, FSOP_STEP_MKDIR, "ux0:data/NEW") >= 0);

    facts_reset();
    fact("ux0:data/EXISTS", FS_ENTRY_DIR, 0, 1);
    FsOpFacts f3 = facts();
    FsOpRequest again = {FSOP_MKDIR, NULL, 0, "ux0:data/EXISTS", &f3,
                         FSOP_CONFLICT_ASK};
    CHECK(fsop_plan_build(&s_plan, &again) == 1);
    CHECK(s_plan.ok == 1);
    CHECK(s_plan.step_count == 0);
}

int main(void)
{
    test_folder_copy();
    test_copy_into_self();
    test_cross_volume_move();
    test_same_folder();
    test_batch_conflict();
    test_risk();
    test_merge();
    test_volume_root_source();
    test_unknown_total();
    test_delete_protection();
    test_twelve_delete();
    test_rename_and_mkdir();

    if (s_failures != 0) {
        fprintf(stderr, "test_fsop_plan: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_fsop_plan: OK\n");
    return 0;
}
