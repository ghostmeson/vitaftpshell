/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/report.h"
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

#define REPORT "ux0:data/skipped.txt"

static XferPlan *new_plan(XferPlanDirection direction)
{
    XferPlan *plan = xfer_plan_create(direction);
    CHECK(plan != NULL);
    return plan;
}

/* The walk is closed only after every folder the plan queued is popped. */
static void finalize(XferPlan *plan)
{
    while (xfer_plan_next_dir(plan) >= 0) {
    }
    CHECK(xfer_plan_finalize(plan) == 1);
}

static void seed_report_folder(void)
{
    fake_fs_reset();
    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_add_dir("ux0:data/DEST");
}

static int read_text(const char *path, char *out, size_t cap)
{
    size_t n = fake_fs_read_file(path, out, cap - 1);
    out[n] = '\0';
    return n > 0;
}

static int holds(const char *path, const char *needle)
{
    char text[8192];
    if (!read_text(path, text, sizeof(text)))
        return 0;
    return strstr(text, needle) != NULL;
}

static XferReportRequest request(const char *direction, const char *roots)
{
    XferReportRequest req;
    memset(&req, 0, sizeof(req));
    req.direction = direction;
    req.roots = roots;
    req.complete = 1;
    req.deleted = -1;
    return req;
}

/* A skipped unsafe name is one upload row, relative to the report folder. */
static void test_upload_header_and_row(void)
{
    seed_report_folder();
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "ux0:data/SRC", "/R", XFER_ENTRY_DIR, 0,
                                  1);
    int file = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    int bad = xfer_plan_add_child(plan, root, "bad:name.txt", XFER_ENTRY_FILE,
                                  9, 1);
    CHECK(file > 0);
    CHECK(bad > 0);
    finalize(plan);
    /* A normal copy is not missing; only the unsafe name is. */
    plan->entries[file].status = XFER_STATUS_COPIED;
    plan->entries[file].moved = 5;
    CHECK(plan->entries[bad].status == XFER_STATUS_SKIPPED);

    XferReportRequest req = request("UPLOAD", "ux0:data/SRC -> /R");
    char chosen[PATH_MAX_LEN];
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, chosen,
                            sizeof(chosen)) == XFER_REPORT_OK);
    CHECK(strcmp(chosen, REPORT) == 0);

    CHECK(holds(REPORT, "vitaFTPShell transfer report\n"));
    CHECK(holds(REPORT, "direction: UPLOAD\n"));
    CHECK(holds(REPORT, "roots: ux0:data/SRC -> /R\n"));
    CHECK(holds(REPORT, "missing: 1\n"));
    CHECK(holds(REPORT, "SKIPPED SRC/bad:name.txt - UNSAFE NAME\n"));
    CHECK(!holds(REPORT, "A.TXT"));

    xfer_plan_free(plan);
}

/* With nothing missing and the job complete, the file says so (R21). */
static void test_complete_line(void)
{
    seed_report_folder();
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "ux0:data/SRC", "/R", XFER_ENTRY_DIR, 0,
                                  1);
    int file = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    finalize(plan);
    plan->entries[file].status = XFER_STATUS_COPIED;
    plan->entries[file].moved = 5;

    XferReportRequest req = request("UPLOAD", "ux0:data/SRC -> /R");
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, NULL, 0) ==
          XFER_REPORT_OK);
    CHECK(holds(REPORT, "missing: 0\n"));
    CHECK(holds(REPORT, "transfer complete - nothing is missing\n"));

    /* A cancelled job with no row missing never reads complete. */
    req.complete = 0;
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, NULL, 0) ==
          XFER_REPORT_OK);
    CHECK(holds(REPORT, "transfer stopped before it completed\n"));
    CHECK(!holds(REPORT, "transfer complete"));

    xfer_plan_free(plan);
}

/* A row inside the report folder is relative; a row outside is absolute; a
 * download skip with no local destination is REMOTE (R21). */
static void test_relative_absolute_remote(void)
{
    seed_report_folder();
    fake_fs_add_dir("ux0:data/OTHER");
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int inside = xfer_plan_add_root(plan, "/A", "ux0:data/DEST/A",
                                    XFER_ENTRY_DIR, 0, 1);
    int outside = xfer_plan_add_root(plan, "/B", "ux0:data/OTHER/B",
                                     XFER_ENTRY_DIR, 0, 1);
    finalize(plan);
    CHECK(xfer_plan_add_skip(plan, inside, "/A/bad:name.txt",
                             "ux0:data/DEST/A/bad:name.txt", XFER_ENTRY_FILE,
                             XFER_SKIP_UNSAFE_NAME) >= 0);
    CHECK(xfer_plan_add_skip(plan, outside, "/B/x.bin", "ux0:data/OTHER/B/x.bin",
                             XFER_ENTRY_FILE, XFER_SKIP_LIST_REFUSED) >= 0);
    CHECK(xfer_plan_add_skip(plan, inside, "/A/no-local.bin", NULL,
                             XFER_ENTRY_FILE, XFER_SKIP_UNSAFE_NAME) >= 0);
    /* A skip after finalization needs a new total/conflict view. */
    while (xfer_plan_next_dir(plan) >= 0) {
    }
    CHECK(xfer_plan_finalize(plan) == 1);

    XferReportRequest req = request("DOWNLOAD", "/A -> ux0:data/DEST");
    req.complete = 0;
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data/DEST", plan, &req, NULL,
                            0) == XFER_REPORT_OK);

    const char *path = "ux0:data/DEST/skipped.txt";
    CHECK(holds(path, "SKIPPED A/bad:name.txt - UNSAFE NAME\n"));
    CHECK(holds(path, "SKIPPED ux0:data/OTHER/B/x.bin - LISTING REFUSED\n"));
    CHECK(holds(path, "SKIPPED REMOTE /A/no-local.bin - UNSAFE NAME\n"));

    xfer_plan_free(plan);
}

/* A control byte in a name is escaped so one entry stays one line (R21). */
static void test_control_escape(void)
{
    seed_report_folder();
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/A", "ux0:data/DEST/A",
                                  XFER_ENTRY_DIR, 0, 1);
    finalize(plan);
    CHECK(xfer_plan_add_skip(plan, root, "/A/bad\x01.bin",
                             "ux0:data/DEST/A/bad\x01.bin", XFER_ENTRY_FILE,
                             XFER_SKIP_UNSAFE_NAME) >= 0);
    while (xfer_plan_next_dir(plan) >= 0) {
    }
    CHECK(xfer_plan_finalize(plan) == 1);

    XferReportRequest req = request("DOWNLOAD", "/A -> ux0:data/DEST");
    req.complete = 0;
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data/DEST", plan, &req, NULL,
                            0) == XFER_REPORT_OK);

    const char *path = "ux0:data/DEST/skipped.txt";
    CHECK(holds(path, "SKIPPED A/bad\\x01.bin - UNSAFE NAME\n"));
    CHECK(!holds(path, "SKIPPED A/bad\x01.bin"));

    xfer_plan_free(plan);
}

/* Every missing kind is its own row: skipped, unverified, and not attempted. */
static void test_missing_kinds(void)
{
    seed_report_folder();
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/A", "ux0:data/DEST/A",
                                  XFER_ENTRY_DIR, 0, 1);
    int skipped = xfer_plan_add_child(plan, root, "S.BIN", XFER_ENTRY_FILE, 1,
                                      1);
    int unverified = xfer_plan_add_child(plan, root, "U.BIN", XFER_ENTRY_FILE,
                                         2, 1);
    int pending = xfer_plan_add_child(plan, root, "P.BIN", XFER_ENTRY_FILE, 3,
                                      1);
    finalize(plan);
    CHECK(xfer_plan_skip_entry(plan, skipped, XFER_SKIP_UNSAFE_NAME) == 1);
    /* A transfer that stopped before verifying leaves the row unverified. */
    plan->entries[unverified].status = XFER_STATUS_UNVERIFIED;
    plan->entries[pending].status = XFER_STATUS_PENDING;

    XferReportRequest req = request("DOWNLOAD", "/A -> ux0:data/DEST");
    req.complete = 0;
    req.pending_reason = "CANCELLED";
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data/DEST", plan, &req, NULL,
                            0) == XFER_REPORT_OK);

    const char *path = "ux0:data/DEST/skipped.txt";
    CHECK(holds(path, "missing: 3\n"));
    CHECK(holds(path, "SKIPPED A/S.BIN - UNSAFE NAME\n"));
    CHECK(holds(path, "UNVERIFIED A/U.BIN - NOT VERIFIED\n"));
    CHECK(holds(path, "NOT ATTEMPTED A/P.BIN - CANCELLED\n"));

    xfer_plan_free(plan);
}

/* A partial move names the verified sources it did not delete (R14). */
static void test_partial_move_keeps_sources(void)
{
    seed_report_folder();
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;

    int root = xfer_plan_add_root(plan, "/A", "ux0:data/DEST/A",
                                  XFER_ENTRY_DIR, 0, 1);
    int one = xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    int two = xfer_plan_add_child(plan, root, "B.TXT", XFER_ENTRY_FILE, 5, 1);
    finalize(plan);
    plan->entries[root].status = XFER_STATUS_VERIFIED;
    plan->entries[one].status = XFER_STATUS_VERIFIED;
    plan->entries[one].moved = 5;
    plan->entries[two].status = XFER_STATUS_VERIFIED;
    plan->entries[two].moved = 5;

    /* The delete list runs bottom-up: B.TXT, A.TXT, A. One was removed. */
    XferReportRequest req = request("DOWNLOAD", "/A -> ux0:data/DEST");
    req.complete = 0;
    req.move = 1;
    req.deleted = 1;
    req.pending_reason = "DELETE FAILED";
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data/DEST", plan, &req, NULL,
                            0) == XFER_REPORT_OK);

    const char *path = "ux0:data/DEST/skipped.txt";
    CHECK(holds(path, "missing: 2\n"));
    CHECK(!holds(path, "NOT DELETED REMOTE /A/B.TXT"));
    CHECK(holds(path, "NOT DELETED REMOTE /A/A.TXT - DELETE FAILED\n"));
    CHECK(holds(path, "NOT DELETED REMOTE /A - DELETE FAILED\n"));

    xfer_plan_free(plan);
}

/* A pre-existing report file is replaced (R22). */
static void test_replace_file(void)
{
    seed_report_folder();
    fake_fs_seed_file(REPORT, "OLD\n", 4);

    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;
    int file = xfer_plan_add_root(plan, "ux0:data/SRC/A.TXT", "/R/A.TXT",
                                  XFER_ENTRY_FILE, 5, 1);
    finalize(plan);
    plan->entries[file].status = XFER_STATUS_COPIED;

    XferReportRequest req = request("UPLOAD", "ux0:data/SRC -> /R");
    char chosen[PATH_MAX_LEN];
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, chosen,
                            sizeof(chosen)) == XFER_REPORT_OK);
    CHECK(strcmp(chosen, REPORT) == 0);
    CHECK(holds(REPORT, "missing: 0\n"));
    CHECK(!holds(REPORT, "OLD"));

    xfer_plan_free(plan);
}

/* A directory at the name is never replaced; no report is saved (R22). */
static void test_directory_is_not_replaced(void)
{
    seed_report_folder();
    fake_fs_add_dir(REPORT);

    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;
    CHECK(xfer_plan_add_root(plan, "ux0:data/SRC/A.TXT", "/R/A.TXT",
                             XFER_ENTRY_FILE, 5, 1) >= 0);
    finalize(plan);

    XferReportRequest req = request("UPLOAD", "ux0:data/SRC -> /R");
    char chosen[PATH_MAX_LEN];
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, chosen,
                            sizeof(chosen)) == XFER_REPORT_REFUSED);
    CHECK(chosen[0] == '\0');
    CHECK(fake_fs_exists(REPORT));
    CHECK(!fake_fs_exists(REPORT ".tmp"));
    CHECK(!fake_fs_exists(REPORT ".old"));

    xfer_plan_free(plan);
}

/* A file the transfer wrote under the report name takes the free name (R22). */
static void test_free_name_for_transfer_file(void)
{
    seed_report_folder();
    fake_fs_seed_file(REPORT, "TRANSFERRED\n", 12);

    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;
    int file = xfer_plan_add_root(plan, REPORT, "/R/skipped.txt",
                                  XFER_ENTRY_FILE, 12, 1);
    finalize(plan);
    plan->entries[file].status = XFER_STATUS_COPIED;

    XferReportRequest req = request("UPLOAD", "ux0:data -> /R");
    char chosen[PATH_MAX_LEN];
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, chosen,
                            sizeof(chosen)) == XFER_REPORT_OK);
    CHECK(strcmp(chosen, "ux0:data/skipped 2.txt") == 0);
    CHECK(holds("ux0:data/skipped 2.txt", "missing: 0\n"));

    /* The transfer's own file is untouched. */
    char text[32];
    CHECK(read_text(REPORT, text, sizeof(text)));
    CHECK(strcmp(text, "TRANSFERRED\n") == 0);

    xfer_plan_free(plan);
}

/* A refusal or a write failure saves nothing and leaves no debris. */
static void test_refused_and_failed_writes(void)
{
    seed_report_folder();
    XferPlan *plan = new_plan(XFER_PLAN_DOWNLOAD);
    if (plan == NULL)
        return;
    int root = xfer_plan_add_root(plan, "/A", "ux0:data/DEST/A",
                                  XFER_ENTRY_DIR, 0, 1);
    xfer_plan_add_child(plan, root, "A.TXT", XFER_ENTRY_FILE, 5, 1);
    finalize(plan);

    XferReportRequest req = request("DOWNLOAD", "/A -> ux0:data/DEST");
    char chosen[PATH_MAX_LEN];
    chosen[0] = 'x';

    /* A read-only folder: the adapter refuses the temp write. */
    fake_fs_fail_op(FAKE_FS_OP_OPEN, FS_DENIED);
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data/DEST", plan, &req,
                            chosen, sizeof(chosen)) == XFER_REPORT_REFUSED);
    CHECK(chosen[0] == '\0');
    CHECK(!fake_fs_exists("ux0:data/DEST/skipped.txt"));

    /* A write failure: the temp is removed and the target is untouched. */
    fake_fs_fail_op(FAKE_FS_OP_WRITE, FS_IO);
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data/DEST", plan, &req,
                            chosen, sizeof(chosen)) == XFER_REPORT_FAILED);
    CHECK(!fake_fs_exists("ux0:data/DEST/skipped.txt"));
    CHECK(!fake_fs_exists("ux0:data/DEST/skipped.txt.tmp"));

    /* A sync failure is a failed report, not a half-written one. */
    fake_fs_fail_sync_at(0);
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data/DEST", plan, &req,
                            chosen, sizeof(chosen)) == XFER_REPORT_FAILED);
    CHECK(!fake_fs_exists("ux0:data/DEST/skipped.txt"));
    CHECK(!fake_fs_exists("ux0:data/DEST/skipped.txt.tmp"));

    xfer_plan_free(plan);
}

/* The temp and the aside never survive a successful save; an orphaned aside is
 * recovered or removed like the store's own sequence (KTD9). */
static void test_temp_and_aside_cleanup(void)
{
    seed_report_folder();
    XferPlan *plan = new_plan(XFER_PLAN_UPLOAD);
    if (plan == NULL)
        return;
    CHECK(xfer_plan_add_root(plan, "ux0:data/SRC/A.TXT", "/R/A.TXT",
                             XFER_ENTRY_FILE, 5, 1) >= 0);
    finalize(plan);

    XferReportRequest req = request("UPLOAD", "ux0:data/SRC -> /R");

    /* An orphaned aside with no target is restored, then replaced. */
    fake_fs_seed_file(REPORT ".old", "STALE\n", 6);
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, NULL, 0) ==
          XFER_REPORT_OK);
    CHECK(fake_fs_exists(REPORT));
    CHECK(!fake_fs_exists(REPORT ".old"));
    CHECK(!fake_fs_exists(REPORT ".tmp"));

    /* The no-replace fallback still replaces the report and removes the aside. */
    fake_fs_set_rename_replaces(0);
    fake_fs_seed_file(REPORT ".old", "STALE\n", 6);
    CHECK(xfer_report_write(fake_fs_port(), "ux0:data", plan, &req, NULL, 0) ==
          XFER_REPORT_OK);
    CHECK(holds(REPORT, "vitaFTPShell transfer report"));
    CHECK(!fake_fs_exists(REPORT ".old"));
    CHECK(!fake_fs_exists(REPORT ".tmp"));

    xfer_plan_free(plan);
}

int main(void)
{
    test_upload_header_and_row();
    test_complete_line();
    test_relative_absolute_remote();
    test_control_escape();
    test_missing_kinds();
    test_partial_move_keeps_sources();
    test_replace_file();
    test_directory_is_not_replaced();
    test_free_name_for_transfer_file();
    test_refused_and_failed_writes();
    test_temp_and_aside_cleanup();

    if (s_failures != 0) {
        fprintf(stderr, "test_report: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_report: OK\n");
    return 0;
}
