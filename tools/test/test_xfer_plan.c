/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/xfer_plan.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static XferPlan *new_plan(XferPlanDirection direction)
{
    XferPlan *plan = xfer_plan_create(direction);
    CHECK(plan != NULL);
    return plan;
}

/* The walk is complete only after every queued folder is popped (R2). */
static void drain_dirs(XferPlan *plan)
{
    while (xfer_plan_next_dir(plan) >= 0) {
    }
}

static void test_three_level_tree(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "ux0:data/L", "/remote/R",
                                  XFER_ENTRY_DIR, 0, 1);
    int sub = xfer_plan_add_child(plan, root, "SUB", XFER_ENTRY_DIR, 0, 1);
    int file = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    int deep = xfer_plan_add_child(plan, sub, "B.TXT", XFER_ENTRY_FILE, 7, 1);

    CHECK(root == 0);
    CHECK(sub == 1);
    CHECK(file == 2);
    CHECK(deep == 3);
    CHECK(plan->entry_count == 4);

    CHECK(xfer_plan_parent(plan, root) == -1);
    CHECK(xfer_plan_parent(plan, sub) == root);
    CHECK(xfer_plan_parent(plan, file) == root);
    CHECK(xfer_plan_parent(plan, deep) == sub);

    CHECK(strcmp(xfer_plan_source(plan, deep), "ux0:data/L/SUB/B.TXT") == 0);
    CHECK(strcmp(xfer_plan_dest(plan, deep), "/remote/R/SUB/B.TXT") == 0);

    /* The worklist expands the tree without recursion (KTD2). */
    CHECK(xfer_plan_next_dir(plan) == root);
    CHECK(xfer_plan_next_dir(plan) == sub);
    CHECK(xfer_plan_next_dir(plan) == -1);

    xfer_plan_note_dest_exists(plan, file);
    CHECK(xfer_plan_finalize(plan) == 1);

    CHECK(plan->entry_count == 4);
    CHECK(plan->root_count == 1);
    CHECK(plan->dir_count == 2);
    CHECK(plan->file_count == 2);
    CHECK(plan->skipped_count == 0);
    CHECK(plan->total_bytes == 12);
    CHECK(plan->bytes_known == 1);
    CHECK(plan->conflicts.count == 1);
    CHECK(plan->conflicts.named == 1);
    CHECK(strcmp(plan->conflicts.names[0].text, "A.TXT") == 0);

    xfer_plan_free(plan);
}

static void test_capacity(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int added = 0;
    for (int i = 0; i < XFER_PLAN_ENTRY_MAX; ++i) {
        char source[32];
        char dest[32];
        snprintf(source, sizeof(source), "/remote/F%d", i);
        snprintf(dest, sizeof(dest), "ux0:data/F%d", i);
        if (xfer_plan_add_root(plan, source, dest, XFER_ENTRY_FILE, 0, 1) == i)
            added++;
    }

    CHECK(added == XFER_PLAN_ENTRY_MAX);
    CHECK(plan->ok == 1);
    CHECK(plan->entry_count == XFER_PLAN_ENTRY_MAX);
    CHECK(xfer_plan_finalize(plan) == 1);

    /* The 4,097th entry refuses the whole plan; it is never truncated. */
    CHECK(xfer_plan_add_root(plan, "/remote/OVER", "ux0:data/OVER",
                             XFER_ENTRY_FILE, 0, 1) == -1);
    CHECK(plan->ok == 0);
    CHECK(strcmp(plan->reason, "PLAN TOO LARGE") == 0);
    CHECK(plan->entry_count == 0);
    CHECK(plan->arena_used == 0);
    CHECK(xfer_plan_next_dir(plan) == -1);

    xfer_plan_free(plan);
}

static void test_empty_folder(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/remote/EMPTY", "ux0:data/EMPTY",
                                  XFER_ENTRY_DIR, 0, 1);
    CHECK(root == 0);
    CHECK(xfer_plan_next_dir(plan) == root);
    CHECK(xfer_plan_next_dir(plan) == -1);
    CHECK(xfer_plan_finalize(plan) == 1);

    CHECK(plan->entry_count == 1);
    CHECK(plan->dir_count == 1);
    CHECK(plan->file_count == 0);
    CHECK(plan->total_bytes == 0);
    CHECK(plan->bytes_known == 1);

    /* An empty folder verifies as an existing folder (R13). */
    xfer_plan_verify_folder_end(plan, root);
    CHECK(plan->entries[root].status == XFER_STATUS_VERIFIED);
    CHECK(xfer_plan_move_ready(plan) == 1);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_DELETE) == 1);
    CHECK(xfer_plan_list_at(plan, XFER_LIST_DELETE, 0) == root);

    xfer_plan_free(plan);
}

static void test_multi_root(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;

    int first = xfer_plan_add_root(plan, "ux0:data/A.BIN", "/remote/A.BIN",
                                   XFER_ENTRY_FILE, 10, 1);
    int second = xfer_plan_add_root(plan, "ux0:data/B.BIN", "/remote/B.BIN",
                                    XFER_ENTRY_FILE, 20, 1);
    int unknown = xfer_plan_add_root(plan, "ux0:data/C.BIN", "/remote/C.BIN",
                                     XFER_ENTRY_FILE, 0, 0);

    CHECK(first == 0);
    CHECK(second == 1);
    CHECK(unknown == 2);
    CHECK(strcmp(xfer_plan_source(plan, first), "ux0:data/A.BIN") == 0);
    CHECK(strcmp(xfer_plan_source(plan, second), "ux0:data/B.BIN") == 0);
    CHECK(strcmp(xfer_plan_source(plan, unknown), "ux0:data/C.BIN") == 0);

    CHECK(xfer_plan_finalize(plan) == 1);
    CHECK(plan->root_count == 3);
    CHECK(plan->file_count == 3);
    CHECK(plan->dir_count == 0);
    CHECK(plan->total_bytes == 30);
    CHECK(plan->bytes_known == 0);
    CHECK(plan->conflicts.count == 0);

    xfer_plan_free(plan);
}

static void test_unsafe_name_subtree(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/remote/ROOT", "ux0:data/ROOT",
                                  XFER_ENTRY_DIR, 0, 1);
    int bad = xfer_plan_add_child(plan, root, "bad\nname", XFER_ENTRY_DIR, 0,
                                  1);
    CHECK(bad == 1);
    CHECK(plan->entry_count == 2);
    CHECK(plan->entries[bad].status == XFER_STATUS_SKIPPED);
    CHECK(plan->entries[bad].skip_reason == XFER_SKIP_UNSAFE_NAME);
    CHECK(plan->entries[bad].dest_off == XFER_PLAN_NO_PATH);
    CHECK(xfer_plan_subtree_verified(plan, root) == 0);

    /* The unsafe subtree is one missing entry, never queued for expansion. */
    CHECK(xfer_plan_next_dir(plan) == root);
    CHECK(xfer_plan_next_dir(plan) == -1);
    CHECK(xfer_plan_finalize(plan) == 1);
    CHECK(plan->skipped_count == 1);

    char row[XFER_REPORT_ROW_MAX];
    CHECK(xfer_plan_report_row(plan, bad, "ux0:data", row, sizeof(row)) > 0);
    CHECK(strcmp(row, "REMOTE /remote/ROOT/bad\\x0Aname") == 0);

    xfer_plan_free(plan);
}

static void test_read_only_subtree(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "ux0:data/SRC", "/remote",
                                  XFER_ENTRY_DIR, 0, 1);
    int ro = xfer_plan_add_skip(plan, root, "ux0:data/SRC/RO",
                                "/remote/RO", XFER_ENTRY_DIR,
                                XFER_SKIP_READ_ONLY);
    CHECK(ro == 1);
    CHECK(plan->entries[ro].status == XFER_STATUS_SKIPPED);
    CHECK(plan->entries[ro].skip_reason == XFER_SKIP_READ_ONLY);
    CHECK(strcmp(xfer_skip_reason_text(XFER_SKIP_READ_ONLY), "READ ONLY") == 0);

    CHECK(xfer_plan_next_dir(plan) == root);
    CHECK(xfer_plan_next_dir(plan) == -1);
    CHECK(xfer_plan_finalize(plan) == 1);
    CHECK(plan->skipped_count == 1);
    CHECK(plan->dir_count == 1);

    char row[XFER_REPORT_ROW_MAX];
    CHECK(xfer_plan_report_row(plan, ro, "ux0:data", row, sizeof(row)) > 0);
    CHECK(strcmp(row, "SRC/RO") == 0);

    xfer_plan_free(plan);
}

static void test_verify_exact(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/remote/ROOT", "ux0:data/ROOT",
                                  XFER_ENTRY_DIR, 0, 1);
    int file = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    drain_dirs(plan);
    CHECK(xfer_plan_finalize(plan) == 1);

    CHECK(xfer_plan_mark_moved(plan, file, 5) == 1);
    CHECK(plan->entries[file].status == XFER_STATUS_COPIED);
    CHECK(plan->entries[file].moved == 5);
    CHECK(xfer_plan_verify_row(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1) ==
          XFER_VERIFY_VERIFIED);
    CHECK(plan->entries[file].status == XFER_STATUS_VERIFIED);

    xfer_plan_verify_folder_end(plan, root);
    CHECK(plan->entries[root].status == XFER_STATUS_VERIFIED);
    CHECK(xfer_plan_move_ready(plan) == 1);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_VERIFIED) == 2);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_UNVERIFIED) == 0);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_MISSING) == 0);

    /* Delete order walks bottom-up: the file before its folder. */
    CHECK(xfer_plan_list_count(plan, XFER_LIST_DELETE) == 2);
    CHECK(xfer_plan_list_at(plan, XFER_LIST_DELETE, 0) == file);
    CHECK(xfer_plan_list_at(plan, XFER_LIST_DELETE, 1) == root);

    xfer_plan_free(plan);
}

static void test_verify_unverified(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/remote/ROOT", "ux0:data/ROOT",
                                  XFER_ENTRY_DIR, 0, 1);
    int short_file = xfer_plan_add_child(plan, root, "SHORT.BIN",
                                         XFER_ENTRY_FILE, 10, 1);
    int unknown_file = xfer_plan_add_child(plan, root, "UNKNOWN.BIN",
                                           XFER_ENTRY_FILE, 10, 1);
    int case_file = xfer_plan_add_child(plan, root, "CASE.BIN",
                                        XFER_ENTRY_FILE, 7, 1);
    int missing_file = xfer_plan_add_child(plan, root, "MISSING.BIN",
                                           XFER_ENTRY_FILE, 1, 1);
    drain_dirs(plan);
    CHECK(xfer_plan_finalize(plan) == 1);

    xfer_plan_mark_moved(plan, short_file, 10);
    xfer_plan_mark_moved(plan, unknown_file, 10);
    xfer_plan_mark_moved(plan, case_file, 7);
    xfer_plan_mark_moved(plan, missing_file, 1);

    CHECK(xfer_plan_verify_row(plan, root, "SHORT.BIN", XFER_ENTRY_FILE, 9,
                               1) == XFER_VERIFY_UNVERIFIED);
    CHECK(xfer_plan_verify_row(plan, root, "UNKNOWN.BIN", XFER_ENTRY_FILE, 0,
                               0) == XFER_VERIFY_UNVERIFIED);
    CHECK(xfer_plan_verify_row(plan, root, "case.bin", XFER_ENTRY_FILE, 7,
                               1) == XFER_VERIFY_UNVERIFIED);

    /* A name that never appears is unverified when the folder listing ends. */
    xfer_plan_verify_folder_end(plan, root);

    CHECK(plan->entries[short_file].status == XFER_STATUS_UNVERIFIED);
    CHECK(plan->entries[unknown_file].status == XFER_STATUS_UNVERIFIED);
    CHECK(plan->entries[case_file].status == XFER_STATUS_UNVERIFIED);
    CHECK(plan->entries[missing_file].status == XFER_STATUS_UNVERIFIED);
    /* The root listing was read, so the folder exists; its subtree is not
     * complete, and the delete guard still keeps it. */
    CHECK(plan->entries[root].status == XFER_STATUS_VERIFIED);
    CHECK(xfer_plan_subtree_verified(plan, root) == 0);
    CHECK(xfer_plan_move_ready(plan) == 0);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_UNVERIFIED) == 4);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_MISSING) == 4);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_DELETE) == 0);

    xfer_plan_free(plan);

    /* A listing that cannot be read leaves the whole move unverified. */
    plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;
    root = xfer_plan_add_root(plan, "/remote/ROOT", "ux0:data/ROOT",
                              XFER_ENTRY_DIR, 0, 1);
    int failed_file =
        xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    drain_dirs(plan);
    CHECK(xfer_plan_finalize(plan) == 1);
    xfer_plan_mark_moved(plan, failed_file, 5);
    xfer_plan_verify_folder_failed(plan, root);
    CHECK(plan->entries[root].status == XFER_STATUS_UNVERIFIED);
    CHECK(plan->entries[failed_file].status == XFER_STATUS_UNVERIFIED);
    CHECK(xfer_plan_move_ready(plan) == 0);

    xfer_plan_free(plan);
}

static void test_verify_extra_ignored(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/remote/ROOT", "ux0:data/ROOT",
                                  XFER_ENTRY_DIR, 0, 1);
    int file = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    drain_dirs(plan);
    CHECK(xfer_plan_finalize(plan) == 1);
    xfer_plan_mark_moved(plan, file, 5);

    CHECK(xfer_plan_verify_row(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1) ==
          XFER_VERIFY_VERIFIED);
    CHECK(xfer_plan_verify_row(plan, root, "EXTRA.TXT", XFER_ENTRY_FILE, 99,
                               1) == XFER_VERIFY_IGNORED);
    CHECK(xfer_plan_verify_row(plan, root, ".", XFER_ENTRY_DIR, 0, 0) ==
          XFER_VERIFY_IGNORED);
    CHECK(xfer_plan_verify_row(plan, root, "..", XFER_ENTRY_DIR, 0, 0) ==
          XFER_VERIFY_IGNORED);

    CHECK(plan->entry_count == 2);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_UNVERIFIED) == 0);
    CHECK(plan->entries[file].status == XFER_STATUS_VERIFIED);

    xfer_plan_verify_folder_end(plan, root);
    CHECK(xfer_plan_move_ready(plan) == 1);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_VERIFIED) == 2);

    xfer_plan_free(plan);
}

static void test_report_rows(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "ux0:data/ROOT", "/remote/ROOT",
                                  XFER_ENTRY_DIR, 0, 1);
    int file = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    int escaped = xfer_plan_add_skip(plan, root, "ux0:data/ROOT/BAD\x01.TXT",
                                     NULL, XFER_ENTRY_FILE,
                                     XFER_SKIP_UNSAFE_NAME);
    int outside = xfer_plan_add_skip(plan, -1, "ux0:data/OTHER/F.TXT",
                                     "/remote/F.TXT", XFER_ENTRY_FILE,
                                     XFER_SKIP_READ_ONLY);
    drain_dirs(plan);
    CHECK(xfer_plan_finalize(plan) == 1);

    char row[XFER_REPORT_ROW_MAX];
    CHECK(xfer_plan_report_row(plan, file, "ux0:data", row, sizeof(row)) > 0);
    CHECK(strcmp(row, "ROOT/A.TXT") == 0);

    CHECK(xfer_plan_report_row(plan, escaped, "ux0:data", row, sizeof(row)) >
          0);
    CHECK(strcmp(row, "ROOT/BAD\\x01.TXT") == 0);

    /* A path outside the report root keeps its full local path (R21). */
    CHECK(xfer_plan_report_row(plan, outside, "ux0:data/ROOT", row,
                               sizeof(row)) > 0);
    CHECK(strcmp(row, "ux0:data/OTHER/F.TXT") == 0);

    /* A short buffer refuses rather than writing a truncated row. */
    char small[8];
    CHECK(xfer_plan_report_row(plan, file, "ux0:data", small, sizeof(small)) ==
          -1);
    CHECK(small[0] == '\0');

    xfer_plan_free(plan);
}

static void test_conflict_names_cap(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;

    for (int i = 0; i < XFER_PLAN_CONFLICT_NAMES + 1; ++i) {
        char source[32];
        char dest[32];
        snprintf(source, sizeof(source), "ux0:data/F%d.BIN", i);
        snprintf(dest, sizeof(dest), "/remote/F%d.BIN", i);
        int index = xfer_plan_add_root(plan, source, dest, XFER_ENTRY_FILE, 1,
                                       1);
        CHECK(index == i);
        xfer_plan_note_dest_exists(plan, index);
    }

    CHECK(xfer_plan_finalize(plan) == 1);
    CHECK(plan->conflicts.count == XFER_PLAN_CONFLICT_NAMES + 1);
    CHECK(plan->conflicts.named == XFER_PLAN_CONFLICT_NAMES);
    CHECK(strcmp(plan->conflicts.names[0].text, "F0.BIN") == 0);
    CHECK(strcmp(plan->conflicts.names[XFER_PLAN_CONFLICT_NAMES - 1].text,
                 "F15.BIN") == 0);

    xfer_plan_free(plan);
}

/* U4: a destination that turns out to be a file skips the planned folder and
 * every child already under it; the worklist never lists it afterwards. */
static void test_skip_entry_subtree(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/R", "ux0:data/R", XFER_ENTRY_DIR, 0,
                                  1);
    int sub = xfer_plan_add_child(plan, root, "B", XFER_ENTRY_DIR, 0, 1);
    int deep = xfer_plan_add_child(plan, sub, "C.TXT", XFER_ENTRY_FILE, 5, 1);
    int file = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    CHECK(xfer_plan_next_dir(plan) == root);
    CHECK(xfer_plan_next_dir(plan) == sub);

    CHECK(xfer_plan_skip_entry(plan, sub, XFER_SKIP_DEST_IS_FILE) == 1);
    CHECK(plan->entries[sub].status == XFER_STATUS_SKIPPED);
    CHECK(plan->entries[sub].skip_reason == XFER_SKIP_DEST_IS_FILE);
    CHECK(plan->entries[deep].status == XFER_STATUS_SKIPPED);
    CHECK(plan->entries[root].status == XFER_STATUS_PENDING);
    CHECK(plan->entries[file].status == XFER_STATUS_PENDING);
    CHECK(xfer_plan_next_dir(plan) == XFER_PLAN_NO_INDEX);

    CHECK(xfer_plan_finalize(plan) == 1);
    CHECK(plan->skipped_count == 2);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_MISSING) == 2);
    CHECK(xfer_plan_next_dir(plan) == XFER_PLAN_NO_INDEX);

    xfer_plan_free(plan);
}

/* A root file's own parent listing is the one that proves it exists; when that
 * listing fails, the root itself is the unverified entry (R13). */
static void test_verify_root_file_failure(void)
{
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;
    int file = xfer_plan_add_root(plan, "/F.BIN", "ux0:data/F.BIN",
                                  XFER_ENTRY_FILE, 5, 1);
    CHECK(xfer_plan_finalize(plan) == 1);
    CHECK(xfer_plan_mark_moved(plan, file, 5) == 1);

    xfer_plan_verify_folder_failed(plan, XFER_PLAN_NO_INDEX);
    CHECK(plan->entries[file].status == XFER_STATUS_UNVERIFIED);
    CHECK(xfer_plan_move_ready(plan) == 0);
    CHECK(xfer_plan_list_count(plan, XFER_LIST_MISSING) == 1);
    xfer_plan_free(plan);

    XferPlan *ok = new_plan(XFER_PLAN_DOWNLOAD);
    if (ok == NULL)
        return;
    int good = xfer_plan_add_root(ok, "/F.BIN", "ux0:data/F.BIN",
                                  XFER_ENTRY_FILE, 5, 1);
    CHECK(xfer_plan_finalize(ok) == 1);
    CHECK(xfer_plan_mark_moved(ok, good, 5) == 1);
    CHECK(xfer_plan_verify_row(ok, XFER_PLAN_NO_INDEX, "F.BIN",
                               XFER_ENTRY_FILE, 5, 1) ==
          XFER_VERIFY_VERIFIED);
    xfer_plan_verify_folder_end(ok, XFER_PLAN_NO_INDEX);
    CHECK(xfer_plan_move_ready(ok) == 1);
    xfer_plan_free(ok);
}

int main(void)
{
    test_three_level_tree();
    test_capacity();
    test_empty_folder();
    test_multi_root();
    test_unsafe_name_subtree();
    test_read_only_subtree();
    test_verify_exact();
    test_verify_unverified();
    test_verify_extra_ignored();
    test_report_rows();
    test_conflict_names_cap();
    test_skip_entry_subtree();
    test_verify_root_file_failure();

    if (s_failures != 0) {
        fprintf(stderr, "test_xfer_plan: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_xfer_plan: OK\n");
    return 0;
}
