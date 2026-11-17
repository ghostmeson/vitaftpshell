/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/xfer_plan.h"

#include <stdlib.h>

/*
 * No string.h, no stdio, and no port header: this module is pure policy. The
 * small text helpers below keep the dependency list at stdlib/stdint/stddef
 * and core/path.h, which is what the layering guard expects of a core file.
 * The plan itself is heap-owned.
 */

static void zero_bytes(void *memory, size_t count)
{
    unsigned char *bytes = memory;
    for (size_t i = 0; i < count; ++i)
        bytes[i] = 0;
}

static size_t text_len(const char *text)
{
    size_t len = 0;
    if (text == NULL)
        return 0;
    while (text[len] != '\0')
        len++;
    return len;
}

static void copy_bytes(char *to, const char *from, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        to[i] = from[i];
}

static int text_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return 0;
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i])
        i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int copy_text(char *to, size_t cap, const char *from)
{
    if (cap == 0)
        return 0;
    size_t len = text_len(from);
    if (len >= cap)
        len = cap - 1;
    copy_bytes(to, from, len);
    to[len] = '\0';
    return 1;
}

static const char *leaf_name(const char *path)
{
    const char *name = path != NULL ? path : "";
    for (const char *p = name; *p != '\0'; ++p) {
        if (*p == '/' || *p == ':')
            name = p + 1;
    }
    return name;
}

/* Local paths join without a separator after a colon; remote paths keep their
 * slash. The empty base yields the component itself. */
static int join_component(const char *base, const char *name, char *out,
                          size_t cap)
{
    size_t base_len = text_len(base);
    size_t name_len = text_len(name);
    int slash = 1;
    if (base_len == 0 || base[base_len - 1] == ':' ||
        base[base_len - 1] == '/')
        slash = 0;

    size_t total = base_len + (size_t)slash + name_len + 1;
    if (total > cap)
        return 0;

    copy_bytes(out, base, base_len);
    size_t used = base_len;
    if (slash)
        out[used++] = '/';
    copy_bytes(out + used, name, name_len);
    out[used + name_len] = '\0';
    return 1;
}

static int name_is_transferable(const char *name)
{
    if (!path_name_is_safe(name))
        return 0;
    for (const char *p = name; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f)
            return 0;
    }
    return 1;
}

static void set_reason(XferPlan *plan, const char *reason)
{
    copy_text(plan->reason, sizeof(plan->reason),
              reason != NULL ? reason : "TRANSFER REFUSED");
}

static void plan_fail(XferPlan *plan, const char *reason)
{
    plan->ok = 0;
    plan->finalized = 0;
    set_reason(plan, reason);
    /* No partial plan may run: drop the table, the arena, and the worklist. */
    plan->entry_count = 0;
    plan->arena_used = 0;
    plan->work_head = 0;
    plan->work_tail = 0;
    plan->root_count = 0;
    plan->file_count = 0;
    plan->dir_count = 0;
    plan->skipped_count = 0;
    plan->total_bytes = 0;
    plan->bytes_known = 0;
    plan->conflicts.count = 0;
    plan->conflicts.named = 0;
}

static int entry_parent(const XferPlan *plan, int entry)
{
    if (entry < 0 || entry >= plan->entry_count)
        return -1;
    uint16_t parent = plan->entries[entry].parent;
    return parent == XFER_PLAN_NO_PARENT ? -1 : (int)parent;
}

static const char *path_at(const XferPlan *plan, uint32_t offset)
{
    if (offset == XFER_PLAN_NO_PATH || offset >= plan->arena_used)
        return "";
    return plan->arena + offset;
}

static uint32_t store_path(XferPlan *plan, const char *path)
{
    if (path == NULL)
        return XFER_PLAN_NO_PATH;
    size_t len = text_len(path) + 1;
    if (plan->arena_used + len > XFER_PLAN_ARENA_MAX) {
        plan_fail(plan, "PLAN TOO LARGE");
        return XFER_PLAN_NO_PATH;
    }
    uint32_t offset = (uint32_t)plan->arena_used;
    copy_bytes(plan->arena + offset, path, len);
    plan->arena_used += len;
    return offset;
}

/* A skipped or unverified entry poisons every ancestor for deletion: a source
 * folder is never removed while its subtree is incomplete (KTD8). */
static void mark_subtree_bad(XferPlan *plan, int entry)
{
    int guard = plan->entry_count + 1;
    while (entry >= 0 && guard-- > 0) {
        plan->subtree_bad[entry] = 1;
        entry = entry_parent(plan, entry);
    }
}

static int add_entry(XferPlan *plan, int parent, const char *source,
                     const char *dest, XferEntryType type, uint64_t size,
                     int size_known, XferEntryStatus status,
                     XferSkipReason skip)
{
    if (plan == NULL || !plan->ok)
        return -1;

    if (parent != XFER_PLAN_NO_INDEX) {
        if (parent < 0 || parent >= plan->entry_count ||
            plan->entries[parent].type != XFER_ENTRY_DIR ||
            plan->entries[parent].status == XFER_STATUS_SKIPPED) {
            plan_fail(plan, "INVALID PARENT");
            return -1;
        }
    }

    if (plan->entry_count >= XFER_PLAN_ENTRY_MAX) {
        plan_fail(plan, "PLAN TOO LARGE");
        return -1;
    }
    if (status != XFER_STATUS_SKIPPED && type == XFER_ENTRY_DIR &&
        plan->work_tail >= XFER_PLAN_ENTRY_MAX) {
        plan_fail(plan, "PLAN TOO LARGE");
        return -1;
    }

    uint32_t source_off = store_path(plan, source);
    if (source != NULL && source_off == XFER_PLAN_NO_PATH)
        return -1;
    uint32_t dest_off = store_path(plan, dest);
    if (dest != NULL && dest_off == XFER_PLAN_NO_PATH)
        return -1;

    int index = plan->entry_count++;
    XferPlanEntry *entry = &plan->entries[index];
    entry->source_off = source_off;
    entry->dest_off = dest_off;
    entry->parent = parent == XFER_PLAN_NO_INDEX ? XFER_PLAN_NO_PARENT
                                                 : (uint16_t)parent;
    entry->type = (uint8_t)type;
    entry->status = (uint8_t)status;
    entry->skip_reason = (uint8_t)skip;
    entry->size_known = size_known ? 1u : 0u;
    entry->dest_existed = 0;
    entry->size = size;
    entry->moved = 0;

    if (status == XFER_STATUS_SKIPPED)
        mark_subtree_bad(plan, index);
    else if (type == XFER_ENTRY_DIR)
        plan->worklist[plan->work_tail++] = (uint16_t)index;

    return index;
}

XferPlan *xfer_plan_create(XferPlanDirection direction)
{
    XferPlan *plan = malloc(sizeof(*plan));
    if (plan == NULL)
        return NULL;
    zero_bytes(plan, sizeof(*plan));
    plan->direction = direction;
    plan->ok = 1;
    plan->bytes_known = 1;
    return plan;
}

void xfer_plan_free(XferPlan *plan)
{
    free(plan);
}

int xfer_plan_add_root(XferPlan *plan, const char *source, const char *dest,
                       XferEntryType type, uint64_t size, int size_known)
{
    if (plan == NULL || !plan->ok)
        return -1;
    if (source == NULL || source[0] == '\0') {
        plan_fail(plan, "INVALID SOURCE");
        return -1;
    }
    return add_entry(plan, XFER_PLAN_NO_INDEX, source, dest, type, size,
                     size_known, XFER_STATUS_PENDING, XFER_SKIP_NONE);
}

int xfer_plan_add_child(XferPlan *plan, int parent, const char *name,
                        XferEntryType type, uint64_t size, int size_known)
{
    if (plan == NULL || !plan->ok)
        return -1;
    if (parent < 0 || parent >= plan->entry_count ||
        plan->entries[parent].type != XFER_ENTRY_DIR ||
        plan->entries[parent].status == XFER_STATUS_SKIPPED) {
        plan_fail(plan, "INVALID PARENT");
        return -1;
    }
    if (name == NULL) {
        plan_fail(plan, "INVALID NAME");
        return -1;
    }

    const XferPlanEntry *parent_entry = &plan->entries[parent];
    const char *base_source = path_at(plan, parent_entry->source_off);
    const char *base_dest = path_at(plan, parent_entry->dest_off);

    /* R5's empty local name has nowhere to point; record the miss at the
     * parent's own path rather than inventing one. */
    if (name[0] == '\0') {
        return add_entry(plan, parent, base_source, NULL, type, 0, 1,
                         XFER_STATUS_SKIPPED, XFER_SKIP_UNSAFE_NAME);
    }

    char source[PATH_MAX_LEN];
    char dest[PATH_MAX_LEN];
    if (!join_component(base_source, name, source, sizeof(source)) ||
        (parent_entry->dest_off != XFER_PLAN_NO_PATH &&
         !join_component(base_dest, name, dest, sizeof(dest)))) {
        plan_fail(plan, "PATH TOO LONG");
        return -1;
    }

    if (!name_is_transferable(name)) {
        /* The name cannot cross the boundary, so no destination path exists
         * and the whole subtree is one missing entry (R5). */
        return add_entry(plan, parent, source, NULL, type, 0, 1,
                         XFER_STATUS_SKIPPED, XFER_SKIP_UNSAFE_NAME);
    }

    const char *dest_path =
        parent_entry->dest_off == XFER_PLAN_NO_PATH ? NULL : dest;
    return add_entry(plan, parent, source, dest_path, type, size, size_known,
                     XFER_STATUS_PENDING, XFER_SKIP_NONE);
}

int xfer_plan_add_skip(XferPlan *plan, int parent, const char *source,
                       const char *dest, XferEntryType type,
                       XferSkipReason reason)
{
    if (plan == NULL || !plan->ok)
        return -1;
    if (source == NULL && dest == NULL) {
        plan_fail(plan, "INVALID SKIP");
        return -1;
    }
    return add_entry(plan, parent, source, dest, type, 0, 1,
                     XFER_STATUS_SKIPPED, reason);
}

static int entry_is_under(const XferPlan *plan, int entry, int ancestor)
{
    int guard = plan->entry_count + 1;
    int parent = entry_parent(plan, entry);
    while (parent >= 0 && guard-- > 0) {
        if (parent == ancestor)
            return 1;
        parent = entry_parent(plan, parent);
    }
    return 0;
}

int xfer_plan_skip_entry(XferPlan *plan, int entry, XferSkipReason reason)
{
    if (plan == NULL || !plan->ok || entry < 0 ||
        entry >= plan->entry_count)
        return 0;
    if (plan->entries[entry].status == XFER_STATUS_SKIPPED)
        return 0;

    plan->entries[entry].status = XFER_STATUS_SKIPPED;
    plan->entries[entry].skip_reason = (uint8_t)reason;
    /* A folder's whole subtree stays one missing record (R5). */
    for (int i = 0; i < plan->entry_count; ++i) {
        if (i == entry || plan->entries[i].status == XFER_STATUS_SKIPPED)
            continue;
        if (!entry_is_under(plan, i, entry))
            continue;
        plan->entries[i].status = XFER_STATUS_SKIPPED;
        plan->entries[i].skip_reason = (uint8_t)reason;
    }
    mark_subtree_bad(plan, entry);
    return 1;
}

int xfer_plan_next_dir(XferPlan *plan)
{
    if (plan == NULL || !plan->ok)
        return XFER_PLAN_NO_INDEX;
    while (plan->work_head < plan->work_tail) {
        int index = (int)plan->worklist[plan->work_head++];
        const XferPlanEntry *entry = &plan->entries[index];
        /* A folder skipped after it entered the worklist is not listed. */
        if (entry->status == XFER_STATUS_SKIPPED ||
            entry->type != XFER_ENTRY_DIR)
            continue;
        return index;
    }
    return XFER_PLAN_NO_INDEX;
}

void xfer_plan_note_dest_exists(XferPlan *plan, int entry)
{
    if (plan == NULL || !plan->ok || entry < 0 ||
        entry >= plan->entry_count)
        return;
    if (plan->entries[entry].status == XFER_STATUS_SKIPPED)
        return;
    plan->entries[entry].dest_existed = 1;
}

int xfer_plan_finalize(XferPlan *plan)
{
    if (plan == NULL || !plan->ok)
        return 0;
    if (plan->work_head != plan->work_tail) {
        plan_fail(plan, "WALK INCOMPLETE");
        return 0;
    }
    if (plan->entry_count == 0) {
        plan_fail(plan, "NO ENTRIES");
        return 0;
    }

    plan->root_count = 0;
    plan->file_count = 0;
    plan->dir_count = 0;
    plan->skipped_count = 0;
    plan->total_bytes = 0;
    plan->bytes_known = 1;
    plan->conflicts.count = 0;
    plan->conflicts.named = 0;

    for (int i = 0; i < plan->entry_count; ++i) {
        const XferPlanEntry *entry = &plan->entries[i];
        if (entry->status == XFER_STATUS_SKIPPED) {
            plan->skipped_count++;
            continue;
        }
        if (entry->parent == XFER_PLAN_NO_PARENT)
            plan->root_count++;
        if (entry->type == XFER_ENTRY_FILE) {
            plan->file_count++;
            if (entry->size_known)
                plan->total_bytes += entry->size;
            else
                plan->bytes_known = 0;
        } else {
            plan->dir_count++;
        }

        if (entry->dest_existed) {
            plan->conflicts.count++;
            if (plan->conflicts.named < XFER_PLAN_CONFLICT_NAMES) {
                Path *slot =
                    &plan->conflicts.names[plan->conflicts.named++];
                copy_text(slot->text, sizeof(slot->text),
                          leaf_name(path_at(plan, entry->dest_off)));
            }
        }
    }

    plan->finalized = 1;
    return 1;
}

int xfer_plan_parent(const XferPlan *plan, int entry)
{
    if (plan == NULL || entry < 0 || entry >= plan->entry_count)
        return XFER_PLAN_NO_INDEX;
    return entry_parent(plan, entry);
}

const char *xfer_plan_source(const XferPlan *plan, int entry)
{
    if (plan == NULL || entry < 0 || entry >= plan->entry_count)
        return "";
    return path_at(plan, plan->entries[entry].source_off);
}

const char *xfer_plan_dest(const XferPlan *plan, int entry)
{
    if (plan == NULL || entry < 0 || entry >= plan->entry_count)
        return "";
    return path_at(plan, plan->entries[entry].dest_off);
}

const char *xfer_skip_reason_text(XferSkipReason reason)
{
    switch (reason) {
    case XFER_SKIP_UNSAFE_NAME:
        return "UNSAFE NAME";
    case XFER_SKIP_READ_ONLY:
        return "READ ONLY";
    case XFER_SKIP_LIST_REFUSED:
        return "LISTING REFUSED";
    case XFER_SKIP_LIST_CAP:
        return "LISTING LIMIT";
    case XFER_SKIP_DEST_IS_FILE:
        return "DESTINATION IS A FILE";
    case XFER_SKIP_LOCAL_UNREADABLE:
        return "UNREADABLE";
    case XFER_SKIP_EXISTING:
        return "EXISTED";
    case XFER_SKIP_DEST_IS_DIR:
        return "DESTINATION IS A FOLDER";
    case XFER_SKIP_PATH_BUSY:
        return "PATH IN USE";
    case XFER_SKIP_NONE:
    default:
        return "SKIPPED";
    }
}

int xfer_plan_subtree_verified(const XferPlan *plan, int entry)
{
    if (plan == NULL || entry < 0 || entry >= plan->entry_count)
        return 0;
    return plan->entries[entry].status == XFER_STATUS_VERIFIED &&
           plan->subtree_bad[entry] == 0;
}

int xfer_plan_mark_moved(XferPlan *plan, int entry, uint64_t bytes)
{
    if (plan == NULL || !plan->ok || entry < 0 ||
        entry >= plan->entry_count)
        return 0;
    XferPlanEntry *target = &plan->entries[entry];
    if (target->type != XFER_ENTRY_FILE)
        return 0;
    if (target->status != XFER_STATUS_PENDING &&
        target->status != XFER_STATUS_COPIED)
        return 0;
    target->moved = bytes;
    target->status = XFER_STATUS_COPIED;
    return 1;
}

static void mark_unverified(XferPlan *plan, int entry)
{
    if (plan->entries[entry].status == XFER_STATUS_SKIPPED)
        return;
    plan->entries[entry].status = XFER_STATUS_UNVERIFIED;
    mark_subtree_bad(plan, entry);
}

XferVerifyResult xfer_plan_verify_row(XferPlan *plan, int parent,
                                      const char *name, XferEntryType type,
                                      uint64_t size, int size_known)
{
    if (plan == NULL || !plan->ok || name == NULL || name[0] == '\0')
        return XFER_VERIFY_IGNORED;
    if (text_equal(name, ".") || text_equal(name, ".."))
        return XFER_VERIFY_IGNORED;

    if (parent != XFER_PLAN_NO_INDEX) {
        if (parent < 0 || parent >= plan->entry_count ||
            plan->entries[parent].type != XFER_ENTRY_DIR ||
            plan->entries[parent].status == XFER_STATUS_SKIPPED)
            return XFER_VERIFY_IGNORED;
    }

    int exact = XFER_PLAN_NO_INDEX;
    int folded = XFER_PLAN_NO_INDEX;
    for (int i = 0; i < plan->entry_count; ++i) {
        const XferPlanEntry *entry = &plan->entries[i];
        if (entry_parent(plan, i) != parent)
            continue;
        if (entry->status == XFER_STATUS_SKIPPED)
            continue;
        const char *dest = path_at(plan, entry->dest_off);
        if (dest[0] == '\0')
            continue;
        const char *entry_name = leaf_name(dest);
        if (exact == XFER_PLAN_NO_INDEX && text_equal(name, entry_name)) {
            exact = i;
            break;
        }
        if (folded == XFER_PLAN_NO_INDEX && path_ci_equal(name, entry_name))
            folded = i;
    }

    if (exact == XFER_PLAN_NO_INDEX) {
        /* A case-only name is not a match: the source stays (KTD4). */
        if (folded != XFER_PLAN_NO_INDEX) {
            mark_unverified(plan, folded);
            return XFER_VERIFY_UNVERIFIED;
        }
        return XFER_VERIFY_IGNORED;
    }

    XferPlanEntry *entry = &plan->entries[exact];
    if (entry->type != (uint8_t)type) {
        mark_unverified(plan, exact);
        return XFER_VERIFY_UNVERIFIED;
    }
    if (entry->status == XFER_STATUS_VERIFIED)
        return XFER_VERIFY_VERIFIED;
    if (entry->status == XFER_STATUS_UNVERIFIED)
        return XFER_VERIFY_UNVERIFIED;

    if (type == XFER_ENTRY_DIR) {
        entry->status = XFER_STATUS_VERIFIED;
        return XFER_VERIFY_VERIFIED;
    }

    if (entry->status != XFER_STATUS_COPIED || !size_known ||
        size != entry->moved) {
        mark_unverified(plan, exact);
        return XFER_VERIFY_UNVERIFIED;
    }
    entry->status = XFER_STATUS_VERIFIED;
    return XFER_VERIFY_VERIFIED;
}

void xfer_plan_verify_folder_end(XferPlan *plan, int parent)
{
    if (plan == NULL || !plan->ok)
        return;
    if (parent == XFER_PLAN_NO_INDEX) {
        /* The folder holding root files has no planned folder entry; a file
         * that the listing did not name is unverified. Root folders are proved
         * by their own listings, never by this one. */
        for (int i = 0; i < plan->entry_count; ++i) {
            XferPlanEntry *entry = &plan->entries[i];
            if (entry_parent(plan, i) != -1 ||
                entry->type != XFER_ENTRY_FILE ||
                entry->status == XFER_STATUS_SKIPPED ||
                entry->status == XFER_STATUS_VERIFIED)
                continue;
            mark_unverified(plan, i);
        }
        return;
    }
    if (parent < 0 || parent >= plan->entry_count)
        return;
    XferPlanEntry *folder = &plan->entries[parent];
    if (folder->type != XFER_ENTRY_DIR ||
        folder->status == XFER_STATUS_SKIPPED)
        return;

    for (int i = 0; i < plan->entry_count; ++i) {
        XferPlanEntry *entry = &plan->entries[i];
        if (entry_parent(plan, i) != parent)
            continue;
        if (entry->status == XFER_STATUS_SKIPPED ||
            entry->status == XFER_STATUS_VERIFIED)
            continue;
        mark_unverified(plan, i);
    }

    /* A root folder has no parent listing; its own successful listing is the
     * proof that it exists. */
    if (folder->parent == XFER_PLAN_NO_PARENT &&
        folder->status == XFER_STATUS_PENDING)
        folder->status = XFER_STATUS_VERIFIED;
}

void xfer_plan_verify_folder_failed(XferPlan *plan, int parent)
{
    if (plan == NULL || !plan->ok)
        return;
    if (parent == XFER_PLAN_NO_INDEX) {
        for (int i = 0; i < plan->entry_count; ++i) {
            if (entry_parent(plan, i) != -1 ||
                plan->entries[i].status == XFER_STATUS_SKIPPED)
                continue;
            mark_unverified(plan, i);
        }
        return;
    }
    if (parent < 0 || parent >= plan->entry_count)
        return;
    if (plan->entries[parent].type != XFER_ENTRY_DIR)
        return;

    for (int i = 0; i < plan->entry_count; ++i) {
        if (entry_parent(plan, i) != parent)
            continue;
        if (plan->entries[i].status == XFER_STATUS_SKIPPED)
            continue;
        mark_unverified(plan, i);
    }

    if (plan->entries[parent].parent == XFER_PLAN_NO_PARENT)
        mark_unverified(plan, parent);
}

int xfer_plan_move_ready(const XferPlan *plan)
{
    if (plan == NULL || !plan->ok || !plan->finalized ||
        plan->entry_count == 0)
        return 0;
    for (int i = 0; i < plan->entry_count; ++i) {
        if (plan->entries[i].status == XFER_STATUS_SKIPPED)
            continue;
        if (plan->entries[i].status != XFER_STATUS_VERIFIED)
            return 0;
    }
    return 1;
}

static int list_matches(const XferPlan *plan, XferListKind kind, int entry)
{
    const XferPlanEntry *item = &plan->entries[entry];
    switch (kind) {
    case XFER_LIST_VERIFIED:
        return item->status == XFER_STATUS_VERIFIED;
    case XFER_LIST_UNVERIFIED:
        return item->status == XFER_STATUS_UNVERIFIED;
    case XFER_LIST_MISSING:
        return item->status == XFER_STATUS_SKIPPED ||
               item->status == XFER_STATUS_UNVERIFIED;
    case XFER_LIST_DELETE:
        return item->status == XFER_STATUS_VERIFIED &&
               plan->subtree_bad[entry] == 0;
    default:
        return 0;
    }
}

int xfer_plan_list_count(const XferPlan *plan, XferListKind kind)
{
    if (plan == NULL || !plan->ok)
        return 0;
    int count = 0;
    for (int i = 0; i < plan->entry_count; ++i) {
        if (list_matches(plan, kind, i))
            count++;
    }
    return count;
}

int xfer_plan_list_at(const XferPlan *plan, XferListKind kind, int index)
{
    if (plan == NULL || !plan->ok || index < 0)
        return XFER_PLAN_NO_INDEX;

    if (kind == XFER_LIST_DELETE) {
        /* Plan order places a folder before its children, so the reverse walk
         * removes files first and each folder last (KTD8). */
        for (int i = plan->entry_count - 1; i >= 0; --i) {
            if (!list_matches(plan, kind, i))
                continue;
            if (index == 0)
                return i;
            index--;
        }
        return XFER_PLAN_NO_INDEX;
    }

    for (int i = 0; i < plan->entry_count; ++i) {
        if (!list_matches(plan, kind, i))
            continue;
        if (index == 0)
            return i;
        index--;
    }
    return XFER_PLAN_NO_INDEX;
}

static int append_text(char *out, size_t cap, size_t *used, const char *text)
{
    size_t len = text_len(text);
    if (*used + len >= cap)
        return 0;
    copy_bytes(out + *used, text, len);
    *used += len;
    out[*used] = '\0';
    return 1;
}

static char hex_digit(unsigned value)
{
    if (value < 10)
        return (char)('0' + value);
    return (char)('A' + (value - 10));
}

static int append_escaped(char *out, size_t cap, size_t *used,
                          const char *text)
{
    for (const char *p = text; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) {
            if (*used + 4 >= cap)
                return 0;
            out[(*used)++] = '\\';
            out[(*used)++] = 'x';
            out[(*used)++] = hex_digit((unsigned)c >> 4);
            out[(*used)++] = hex_digit((unsigned)c & 0x0fu);
        } else {
            if (*used + 1 >= cap)
                return 0;
            out[(*used)++] = (char)c;
        }
    }
    out[*used] = '\0';
    return 1;
}

int xfer_plan_report_row(const XferPlan *plan, int entry,
                         const char *report_root, char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return -1;
    out[0] = '\0';
    if (plan == NULL || entry < 0 || entry >= plan->entry_count)
        return -1;

    const XferPlanEntry *item = &plan->entries[entry];
    const char *source = path_at(plan, item->source_off);
    const char *dest = path_at(plan, item->dest_off);
    const char *local =
        plan->direction == XFER_PLAN_UPLOAD ? source : dest;
    const char *remote =
        plan->direction == XFER_PLAN_UPLOAD ? dest : source;

    size_t used = 0;
    if (local[0] == '\0') {
        /* A download skip with no local destination names the remote path
         * and marks it so the report cannot be mistaken for a local one. */
        if (remote[0] == '\0')
            return -1;
        if (!append_text(out, cap, &used, "REMOTE ")) {
            out[0] = '\0';
            return -1;
        }
        if (!append_escaped(out, cap, &used, remote)) {
            out[0] = '\0';
            return -1;
        }
        return (int)used;
    }

    const char *relative = NULL;
    if (report_root != NULL) {
        size_t root_len = text_len(report_root);
        while (root_len > 0 && report_root[root_len - 1] == '/')
            root_len--;
        if (root_len > 0 && path_ci_equal(report_root, local)) {
            relative = ".";
        } else if (root_len > 0 && path_is_ancestor(report_root, local)) {
            size_t skip = root_len;
            if (local[skip] == '/')
                skip++;
            relative = local + skip;
        }
    }

    if (!append_escaped(out, cap, &used, relative != NULL ? relative : local)) {
        out[0] = '\0';
        return -1;
    }
    return (int)used;
}
