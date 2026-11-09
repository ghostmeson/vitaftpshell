/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/fsop_plan.h"

#include <stdio.h>
#include <string.h>

#define NO_PATH UINT32_MAX

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int ci_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return 0;
    while (*a != '\0' && *b != '\0') {
        if (lower_ascii(*a) != lower_ascii(*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int ci_prefix(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL)
        return 0;
    while (*prefix != '\0') {
        if (*text == '\0' || lower_ascii(*text) != lower_ascii(*prefix))
            return 0;
        text++;
        prefix++;
    }
    return 1;
}

static const char *last_segment(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == ':')
            name = p + 1;
    }
    return name;
}

static const char *parent_path(const char *path, Path *out)
{
    if (path_parent(path, out) != PATH_OK)
        return NULL;
    return out->text;
}

static int same_folder(const char *path, const char *folder)
{
    Path parent;
    const char *p = parent_path(path, &parent);
    return p != NULL && ci_equal(p, folder);
}

/* True when `path` is `root` or below it, compared case-insensitively. */
static int inside_or_same(const char *path, const char *root)
{
    if (!ci_prefix(path, root))
        return 0;
    char next = path[strlen(root)];
    return next == '\0' || next == '/' || next == ':';
}

static void fail(FsOpPlan *plan, const char *reason)
{
    plan->ok = 0;
    snprintf(plan->reason, sizeof(plan->reason), "%s",
             reason != NULL ? reason : "OPERATION REFUSED");
}

static void mark_risk(FsOpPlan *plan, const char *path)
{
    if (plan->high_risk)
        return;
    plan->high_risk = 1;
    snprintf(plan->risk_path.text, sizeof(plan->risk_path.text), "%s", path);
}

static const FsOpFact *fact_find(const FsOpFacts *facts, const char *path)
{
    if (facts == NULL)
        return NULL;
    for (int i = 0; i < facts->count; ++i) {
        if (ci_equal(facts->items[i].path, path))
            return &facts->items[i];
    }
    return NULL;
}

static int fact_exists(const FsOpFacts *facts, const char *path,
                       const FsOpFact **out)
{
    const FsOpFact *fact = fact_find(facts, path);
    if (out != NULL)
        *out = fact;
    return fact != NULL && fact->exists;
}

static const char *path_reason(PathResult result, const char *path)
{
    if (result == PATH_PROTECTED) {
        if (path_is_volume_root(path))
            return "VOLUME ROOT";
        return "PROTECTED PATH";
    }
    if (result == PATH_DENIED) {
        const char *name = last_segment(path);
        if (ci_equal(name, "boot_config.txt"))
            return "BOOT CONFIG DENIED";
        return "DENIED PATH";
    }
    return "INVALID PATH";
}

static uint32_t store_path(FsOpPlan *plan, const char *path)
{
    if (path == NULL)
        return NO_PATH;
    size_t len = strlen(path) + 1;
    if (plan->arena_used + len > sizeof(plan->arena)) {
        fail(plan, "PLAN TOO LARGE");
        return NO_PATH;
    }
    uint32_t offset = (uint32_t)plan->arena_used;
    memcpy(plan->arena + plan->arena_used, path, len);
    plan->arena_used += len;
    return offset;
}

static const char *get_path(const FsOpPlan *plan, uint32_t offset)
{
    if (offset == NO_PATH || offset >= plan->arena_used)
        return "";
    return plan->arena + offset;
}

static int add_step(FsOpPlan *plan, FsOpStepKind kind, const char *source,
                    const char *dest, int move_source)
{
    if (plan->step_count >= FSOP_STEP_MAX) {
        fail(plan, "PLAN TOO LARGE");
        return 0;
    }
    uint32_t source_off = store_path(plan, source);
    if (source != NULL && source_off == NO_PATH)
        return 0;
    uint32_t dest_off = store_path(plan, dest);
    if (dest != NULL && dest_off == NO_PATH)
        return 0;

    FsOpStep *step = &plan->steps[plan->step_count++];
    memset(step, 0, sizeof(*step));
    step->kind = (uint8_t)kind;
    step->move_source = (uint8_t)(move_source != 0);
    step->rollback_index = -1;
    step->source_off = source_off;
    step->dest_off = dest_off;
    return 1;
}

static int add_rollback(FsOpPlan *plan, FsOpRollbackKind kind, const char *from,
                        const char *to)
{
    if (plan->rollback_count >= FSOP_ROLLBACK_MAX) {
        fail(plan, "PLAN TOO LARGE");
        return 0;
    }
    uint32_t from_off = store_path(plan, from);
    if (from != NULL && from_off == NO_PATH)
        return 0;
    uint32_t to_off = store_path(plan, to);
    if (to != NULL && to_off == NO_PATH)
        return 0;

    int index = plan->rollback_count;
    FsOpRollback *rollback = &plan->rollback[plan->rollback_count++];
    memset(rollback, 0, sizeof(*rollback));
    rollback->kind = (uint8_t)kind;
    rollback->from_off = from_off;
    rollback->to_off = to_off;
    if (plan->step_count > 0)
        plan->steps[plan->step_count - 1].rollback_index = (int16_t)index;
    return 1;
}

static int join_dest(const char *dir, const char *name, Path *out)
{
    return path_join(dir, name, out) == PATH_OK;
}

static int choose_copy_name(FsOpPlan *plan, const FsOpFacts *facts,
                            const char *source, const char *dest_dir,
                            Path *out)
{
    const char *name = last_segment(source);
    const FsOpFact *source_fact = fact_find(facts, source);
    const char *dot = NULL;
    if (source_fact != NULL && source_fact->type == FS_ENTRY_FILE) {
        for (const char *p = name; *p != '\0'; ++p) {
            if (*p == '.' && p != name)
                dot = p;
        }
    }
    size_t stem_len = dot != NULL ? (size_t)(dot - name) : strlen(name);
    const char *suffix = dot != NULL ? dot : "";
    char candidate[PATH_NAME_MAX + 16];

    for (int n = 0; n < 100; ++n) {
        if (n == 0)
            snprintf(candidate, sizeof(candidate), "%.*s copy%s", (int)stem_len,
                     name, suffix);
        else
            snprintf(candidate, sizeof(candidate), "%.*s copy %d%s",
                     (int)stem_len, name, n + 1, suffix);

        if (!join_dest(dest_dir, candidate, out)) {
            fail(plan, "NAME TOO LONG");
            return 0;
        }
        if (!fact_exists(facts, out->text, NULL))
            return 1;
    }

    fail(plan, "NO FREE COPY NAME");
    return 0;
}

static void plan_file_copy(FsOpPlan *plan, const char *source,
                           const char *dest, const FsOpFact *source_fact,
                           const FsOpFacts *facts, FsOpConflict conflict,
                           int move_source);

static void plan_tree(FsOpPlan *plan, const char *source, const char *dest,
                      const FsOpFacts *facts, FsOpConflict conflict, int move);

static void plan_file_move(FsOpPlan *plan, const char *source, const char *dest,
                           const FsOpFact *source_fact, const FsOpFacts *facts,
                           FsOpConflict conflict)
{
    const FsOpFact *dest_fact = NULL;
    int dest_exists = fact_exists(facts, dest, &dest_fact);

    if (dest_exists) {
        if (dest_fact->type == FS_ENTRY_DIR) {
            fail(plan, "DESTINATION IS A FOLDER");
            return;
        }
        if (conflict == FSOP_CONFLICT_ASK) {
            plan->needs_conflict = 1;
            plan->existing_count++;
            return;
        }
        if (conflict == FSOP_CONFLICT_SKIP)
            return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    PathResult pr = path_check_rename(source, dest, 0, &risk);
    if (pr != PATH_OK) {
        fail(plan, path_reason(pr, dest));
        return;
    }
    if (risk == PATH_RISK_HIGH)
        mark_risk(plan, dest);

    int same_volume = 1;
    Volume from_volume;
    Volume to_volume;
    if (!path_volume(source, &from_volume) || !path_volume(dest, &to_volume))
        same_volume = 1;
    else
        same_volume = from_volume == to_volume;

    if (same_volume) {
        if (!add_step(plan, FSOP_STEP_RENAME, source, dest, 0))
            return;
        if (!add_rollback(plan, FSOP_ROLLBACK_RENAME_BACK, dest, source))
            return;
    } else {
        plan_file_copy(plan, source, dest, source_fact, facts, conflict, 1);
    }
}

static void plan_file_copy(FsOpPlan *plan, const char *source,
                           const char *dest, const FsOpFact *source_fact,
                           const FsOpFacts *facts, FsOpConflict conflict,
                           int move_source)
{
    const FsOpFact *dest_fact = NULL;
    int dest_exists = fact_exists(facts, dest, &dest_fact);

    if (dest_exists) {
        if (dest_fact->type == FS_ENTRY_DIR) {
            fail(plan, "DESTINATION IS A FOLDER");
            return;
        }
        if (conflict == FSOP_CONFLICT_ASK) {
            plan->needs_conflict = 1;
            plan->existing_count++;
            return;
        }
        if (conflict == FSOP_CONFLICT_SKIP)
            return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    PathResult pr = path_check_read(source, 0, &risk);
    if (pr != PATH_OK) {
        fail(plan, path_reason(pr, source));
        return;
    }
    if (risk == PATH_RISK_HIGH)
        mark_risk(plan, source);

    pr = path_check_write(dest, 0, &risk);
    if (pr != PATH_OK) {
        fail(plan, path_reason(pr, dest));
        return;
    }
    if (risk == PATH_RISK_HIGH)
        mark_risk(plan, dest);

    if (!add_step(plan, FSOP_STEP_COPY_FILE, source, dest, move_source))
        return;
    plan->steps[plan->step_count - 1].dest_existed =
        (uint8_t)(dest_exists != 0);
    if (!move_source && !dest_exists &&
        !add_rollback(plan, FSOP_ROLLBACK_REMOVE_FILE, dest, NULL))
        return;

    plan->file_count++;
    if (source_fact != NULL && source_fact->size_known) {
        plan->total_bytes += source_fact->size;
    } else {
        plan->all_sizes_known = 0;
    }
}

static void plan_children(FsOpPlan *plan, const char *source_dir,
                          const char *dest_dir, const FsOpFacts *facts,
                          FsOpConflict conflict, int move)
{
    if (facts == NULL)
        return;

    for (int i = 0; i < facts->count; ++i) {
        const FsOpFact *child = &facts->items[i];
        if (!child->exists || ci_equal(child->path, source_dir))
            continue;

        Path parent;
        if (path_parent(child->path, &parent) != PATH_OK)
            continue;
        if (!ci_equal(parent.text, source_dir))
            continue;

        const char *name = last_segment(child->path);
        Path child_dest;
        if (!join_dest(dest_dir, name, &child_dest)) {
            fail(plan, "NAME TOO LONG");
            return;
        }
        plan_tree(plan, child->path, child_dest.text, facts, conflict, move);
        if (!plan->ok)
            return;
    }
}

static void plan_tree(FsOpPlan *plan, const char *source, const char *dest,
                      const FsOpFacts *facts, FsOpConflict conflict, int move)
{
    const FsOpFact *source_fact = NULL;
    if (!fact_exists(facts, source, &source_fact)) {
        fail(plan, "SOURCE NOT FOUND");
        return;
    }

    if (source_fact->type != FS_ENTRY_DIR) {
        if (move)
            plan_file_move(plan, source, dest, source_fact, facts, conflict);
        else
            plan_file_copy(plan, source, dest, source_fact, facts, conflict, 0);
        return;
    }

    const FsOpFact *dest_fact = NULL;
    int dest_exists = fact_exists(facts, dest, &dest_fact);
    if (dest_exists && dest_fact->type != FS_ENTRY_DIR) {
        fail(plan, "DESTINATION IS A FILE");
        return;
    }

    if (!dest_exists) {
        PathRisk risk = PATH_RISK_NORMAL;
        PathResult pr = path_check_write(dest, 0, &risk);
        if (pr != PATH_OK) {
            fail(plan, path_reason(pr, dest));
            return;
        }
        if (risk == PATH_RISK_HIGH)
            mark_risk(plan, dest);
        if (!add_step(plan, FSOP_STEP_MKDIR, NULL, dest, 0))
            return;
        if (!move && !add_rollback(plan, FSOP_ROLLBACK_REMOVE_DIR, dest, NULL))
            return;
    }

    plan_children(plan, source, dest, facts, conflict, move);
    if (!plan->ok)
        return;

    if (move) {
        PathRisk risk = PATH_RISK_NORMAL;
        PathResult pr = path_check_delete(source, 0, &risk);
        if (pr != PATH_OK) {
            fail(plan, path_reason(pr, source));
            return;
        }
        if (risk == PATH_RISK_HIGH)
            mark_risk(plan, source);
        if (!add_step(plan, FSOP_STEP_DELETE_DIR, source, NULL, 0))
            return;
    }
}

static void plan_copy_or_move(FsOpPlan *plan, const FsOpRequest *request)
{
    if (request->sources == NULL || request->source_count <= 0) {
        fail(plan, "NOTHING SELECTED");
        return;
    }
    if (request->destination == NULL) {
        fail(plan, "NO DESTINATION");
        return;
    }

    Path destination;
    if (path_canonicalize(request->destination, &destination) != PATH_OK) {
        fail(plan, "INVALID DESTINATION");
        return;
    }

    plan->item_count = request->source_count;

    for (int i = 0; i < request->source_count; ++i) {
        Path source;
        if (request->sources[i] == NULL ||
            path_canonicalize(request->sources[i], &source) != PATH_OK) {
            fail(plan, "INVALID SOURCE");
            return;
        }

        if (path_is_volume_root(source.text)) {
            fail(plan, "VOLUME ROOT");
            return;
        }

        const FsOpFact *source_fact = NULL;
        if (!fact_exists(request->facts, source.text, &source_fact)) {
            fail(plan, "SOURCE NOT FOUND");
            return;
        }

        Path dest;
        if (request->kind == FSOP_MOVE &&
            same_folder(source.text, destination.text)) {
            fail(plan, "ALREADY IN THIS FOLDER");
            return;
        }

        if (request->kind == FSOP_COPY &&
            same_folder(source.text, destination.text)) {
            if (!choose_copy_name(plan, request->facts, source.text,
                                  destination.text, &dest))
                return;
        } else {
            if (!join_dest(destination.text, last_segment(source.text), &dest)) {
                fail(plan, "NAME TOO LONG");
                return;
            }
        }

        if (source_fact->type == FS_ENTRY_DIR &&
            inside_or_same(dest.text, source.text)) {
            fail(plan, request->kind == FSOP_COPY ? "CANNOT COPY INTO ITSELF"
                                                  : "CANNOT MOVE INTO ITSELF");
            return;
        }

        if (request->kind == FSOP_COPY) {
            plan_tree(plan, source.text, dest.text, request->facts,
                      request->conflict, 0);
        } else {
            int same_volume = 1;
            Volume from_volume;
            Volume to_volume;
            if (!path_volume(source.text, &from_volume) ||
                !path_volume(dest.text, &to_volume))
                same_volume = 1;
            else
                same_volume = from_volume == to_volume;

            const FsOpFact *dest_fact = NULL;
            int dest_exists = fact_exists(request->facts, dest.text, &dest_fact);

            if (same_volume && !dest_exists) {
                PathRisk risk = PATH_RISK_NORMAL;
                PathResult pr =
                    path_check_rename(source.text, dest.text, 0, &risk);
                if (pr != PATH_OK) {
                    fail(plan, path_reason(pr, dest.text));
                    return;
                }
                if (risk == PATH_RISK_HIGH)
                    mark_risk(plan, dest.text);
                if (!add_step(plan, FSOP_STEP_RENAME, source.text, dest.text, 0))
                    return;
                if (!add_rollback(plan, FSOP_ROLLBACK_RENAME_BACK, dest.text,
                                  source.text))
                    return;
            } else {
                plan_tree(plan, source.text, dest.text, request->facts,
                          request->conflict, 1);
            }
        }
        if (!plan->ok)
            return;
    }
}

static void plan_delete_tree(FsOpPlan *plan, const char *path,
                             const FsOpFacts *facts)
{
    const FsOpFact *fact = NULL;
    if (!fact_exists(facts, path, &fact)) {
        fail(plan, "SOURCE NOT FOUND");
        return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    PathResult pr = path_check_delete(path, 0, &risk);
    if (pr != PATH_OK) {
        fail(plan, path_reason(pr, path));
        return;
    }
    if (risk == PATH_RISK_HIGH)
        mark_risk(plan, path);

    if (fact->type == FS_ENTRY_DIR) {
        if (facts != NULL) {
            for (int i = 0; i < facts->count; ++i) {
                const FsOpFact *child = &facts->items[i];
                if (!child->exists)
                    continue;
                Path parent;
                if (path_parent(child->path, &parent) != PATH_OK)
                    continue;
                if (!ci_equal(parent.text, path))
                    continue;
                plan_delete_tree(plan, child->path, facts);
                if (!plan->ok)
                    return;
            }
        }
        if (!add_step(plan, FSOP_STEP_DELETE_DIR, path, NULL, 0))
            return;
    } else {
        plan->file_count++;
        if (fact->size_known)
            plan->total_bytes += fact->size;
        else
            plan->all_sizes_known = 0;
        if (!add_step(plan, FSOP_STEP_DELETE_FILE, path, NULL, 0))
            return;
    }
}

static void plan_delete(FsOpPlan *plan, const FsOpRequest *request)
{
    if (request->sources == NULL || request->source_count <= 0) {
        fail(plan, "NOTHING SELECTED");
        return;
    }
    plan->item_count = request->source_count;

    for (int i = 0; i < request->source_count; ++i) {
        Path source;
        if (request->sources[i] == NULL ||
            path_canonicalize(request->sources[i], &source) != PATH_OK) {
            fail(plan, "INVALID SOURCE");
            return;
        }
        plan_delete_tree(plan, source.text, request->facts);
        if (!plan->ok)
            return;
    }
}

static void plan_mkdir(FsOpPlan *plan, const FsOpRequest *request)
{
    if (request->destination == NULL) {
        fail(plan, "NO DESTINATION");
        return;
    }
    Path dest;
    if (path_canonicalize(request->destination, &dest) != PATH_OK) {
        fail(plan, "INVALID DESTINATION");
        return;
    }
    plan->item_count = 1;

    const FsOpFact *fact = NULL;
    if (fact_exists(request->facts, dest.text, &fact)) {
        if (fact->type == FS_ENTRY_DIR) {
            plan->ok = 1;
            return;
        }
        fail(plan, "DESTINATION IS A FILE");
        return;
    }

    PathRisk risk = PATH_RISK_NORMAL;
    PathResult pr = path_check_write(dest.text, 0, &risk);
    if (pr != PATH_OK) {
        fail(plan, path_reason(pr, dest.text));
        return;
    }
    if (risk == PATH_RISK_HIGH)
        mark_risk(plan, dest.text);
    if (!add_step(plan, FSOP_STEP_MKDIR, NULL, dest.text, 0))
        return;
    add_rollback(plan, FSOP_ROLLBACK_REMOVE_DIR, dest.text, NULL);
}

static void plan_rename(FsOpPlan *plan, const FsOpRequest *request)
{
    if (request->source_count != 1 || request->sources == NULL ||
        request->sources[0] == NULL || request->destination == NULL) {
        fail(plan, "RENAME NEEDS ONE SOURCE AND ONE TARGET");
        return;
    }

    Path source;
    Path dest;
    if (path_canonicalize(request->sources[0], &source) != PATH_OK ||
        path_canonicalize(request->destination, &dest) != PATH_OK) {
        fail(plan, "INVALID PATH");
        return;
    }
    plan->item_count = 1;

    if (ci_equal(source.text, dest.text)) {
        plan->ok = 1;
        return;
    }

    const FsOpFact *source_fact = NULL;
    if (!fact_exists(request->facts, source.text, &source_fact)) {
        fail(plan, "SOURCE NOT FOUND");
        return;
    }

    const FsOpFact *dest_fact = NULL;
    int dest_exists = fact_exists(request->facts, dest.text, &dest_fact);
    if (dest_exists) {
        if (dest_fact->type == FS_ENTRY_DIR) {
            fail(plan, "DESTINATION IS A FOLDER");
            return;
        }
        if (request->conflict == FSOP_CONFLICT_ASK) {
            plan->needs_conflict = 1;
            plan->existing_count = 1;
            return;
        }
        if (request->conflict == FSOP_CONFLICT_SKIP) {
            plan->ok = 1;
            return;
        }
    }

    PathRisk risk = PATH_RISK_NORMAL;
    PathResult pr = path_check_rename(source.text, dest.text, 0, &risk);
    if (pr != PATH_OK) {
        fail(plan, path_reason(pr, dest.text));
        return;
    }
    if (risk == PATH_RISK_HIGH)
        mark_risk(plan, dest.text);

    if (!add_step(plan, FSOP_STEP_RENAME, source.text, dest.text, 0))
        return;
    add_rollback(plan, FSOP_ROLLBACK_RENAME_BACK, dest.text, source.text);
}

int fsop_plan_build(FsOpPlan *plan, const FsOpRequest *request)
{
    if (plan == NULL || request == NULL)
        return 0;

    memset(plan, 0, sizeof(*plan));
    plan->kind = request->kind;
    plan->ok = 1;
    plan->all_sizes_known = 1;

    switch (request->kind) {
    case FSOP_COPY:
    case FSOP_MOVE:
        plan_copy_or_move(plan, request);
        break;
    case FSOP_DELETE:
        plan_delete(plan, request);
        break;
    case FSOP_MKDIR:
        plan_mkdir(plan, request);
        break;
    case FSOP_RENAME:
        plan_rename(plan, request);
        break;
    default:
        fail(plan, "UNKNOWN OPERATION");
        break;
    }

    if (!plan->ok)
        return 0;
    plan->total_known = plan->file_count > 0 && plan->all_sizes_known;
    if (plan->needs_conflict) {
        /* Do not leave a partial plan that a caller could execute before the
         * one batch decision is made. The request still carries the items. */
        plan->step_count = 0;
        plan->rollback_count = 0;
        plan->total_bytes = 0;
        plan->total_known = 0;
        return 1;
    }
    if (plan->step_count == 0 && plan->kind != FSOP_MKDIR &&
        plan->kind != FSOP_RENAME) {
        /* A SKIP decision can legitimately leave no work. */
        plan->ok = 1;
    }
    return 1;
}

const char *fsop_plan_step_source(const FsOpPlan *plan, int step)
{
    if (plan == NULL || step < 0 || step >= plan->step_count)
        return "";
    return get_path(plan, plan->steps[step].source_off);
}

const char *fsop_plan_step_dest(const FsOpPlan *plan, int step)
{
    if (plan == NULL || step < 0 || step >= plan->step_count)
        return "";
    return get_path(plan, plan->steps[step].dest_off);
}

const char *fsop_plan_rollback_from(const FsOpPlan *plan, int rollback)
{
    if (plan == NULL || rollback < 0 || rollback >= plan->rollback_count)
        return "";
    return get_path(plan, plan->rollback[rollback].from_off);
}

const char *fsop_plan_rollback_to(const FsOpPlan *plan, int rollback)
{
    if (plan == NULL || rollback < 0 || rollback >= plan->rollback_count)
        return "";
    return get_path(plan, plan->rollback[rollback].to_off);
}
