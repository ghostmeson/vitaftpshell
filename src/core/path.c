/* SPDX-License-Identifier: GPL-3.0-only */

#include "core/path.h"

#include <stdio.h>
#include <string.h>


static char lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int ci_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (lower(*a) != lower(*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int ci_prefix(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (lower(*text) != lower(*prefix))
            return 0;
        text++;
        prefix++;
    }
    return 1;
}

/* app0: is the installed app's tree. The alias is comparison-only: display
 * and canonical paths keep the spelling the user typed, while a same-path,
 * ancestry, or reservation test sees one location. [C] app0: resolves to
 * ux0:app/VFTPSHELL on 3.65 Enso. */
#define PATH_APP0_ALIAS "ux0:app/VFTPSHELL"

static const char *location_key(const char *path, char *buf, size_t cap)
{
    if (path == NULL || cap == 0 || !ci_prefix(path, "app0:"))
        return path;
    const char *rest = path + 5;
    int n = rest[0] == '\0'
                ? snprintf(buf, cap, "%s", PATH_APP0_ALIAS)
                : snprintf(buf, cap, "%s/%s", PATH_APP0_ALIAS, rest);
    if (n <= 0 || (size_t)n >= cap)
        return path;
    return buf;
}

int path_ci_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return 0;
    char ab[PATH_MAX_LEN];
    char bb[PATH_MAX_LEN];
    return ci_equal(location_key(a, ab, sizeof(ab)),
                    location_key(b, bb, sizeof(bb)));
}

int path_is_ancestor(const char *a, const char *b)
{
    if (a == NULL || b == NULL || a[0] == '\0' || b[0] == '\0')
        return 0;

    char ab[PATH_MAX_LEN];
    char bb[PATH_MAX_LEN];
    a = location_key(a, ab, sizeof(ab));
    b = location_key(b, bb, sizeof(bb));

    size_t len = strlen(a);
    while (len > 0 && a[len - 1] == '/')
        len--;
    if (len == 0)
        return 0;
    if (!ci_prefix(b, a))
        return 0;
    if (b[len] == '\0')
        return 0;
    if (a[len - 1] == ':')
        return 1;
    return b[len] == '/';
}

static int is_under(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    if (!ci_prefix(path, dir))
        return 0;
    if (path[n] == '\0')
        return 1;
    return path[n] == '/';
}

static int ends_with_ci(const char *text, const char *suffix)
{
    size_t tn = strlen(text);
    size_t sn = strlen(suffix);
    if (sn > tn)
        return 0;
    return ci_equal(text + tn - sn, suffix);
}

static int name_of_path(const char *path, const char **name, size_t *len)
{
    const char *slash = strrchr(path, '/');
    if (slash != NULL) {
        *name = slash + 1;
        *len = strlen(slash + 1);
        return 1;
    }

    const char *colon = strrchr(path, ':');
    if (colon == NULL)
        return 0;
    *name = colon + 1;
    *len = strlen(colon + 1);
    return 1;
}

/* Display order is ALL DRIVES order. Membership defines the local list;
 * the flags define server exposure, read-only, and protected data roots. */
typedef struct {
    const char *name;
    unsigned flags;
} VolumeDef;

#define VOLUME_FLAG_SERVER (1u << 0)
#define VOLUME_FLAG_READ_ONLY (1u << 1)
#define VOLUME_FLAG_PROTECT_DATA (1u << 2)

static const VolumeDef VOLUMES[VOLUME_COUNT] = {
    {"ux0:", VOLUME_FLAG_SERVER | VOLUME_FLAG_PROTECT_DATA},
    {"ur0:", VOLUME_FLAG_SERVER | VOLUME_FLAG_PROTECT_DATA},
    {"uma0:", VOLUME_FLAG_SERVER},
    {"imc0:", VOLUME_FLAG_PROTECT_DATA},
    {"gro0:", VOLUME_FLAG_READ_ONLY},
    {"grw0:", 0},
    {"app0:", VOLUME_FLAG_READ_ONLY},
};

static unsigned volume_flags(Volume volume)
{
    if ((unsigned)volume >= (unsigned)VOLUME_COUNT)
        return 0;
    return VOLUMES[volume].flags;
}

int path_volume_from_token(const char *token, size_t len, Volume *out)
{
    if (token == NULL || out == NULL)
        return 0;
    for (int v = 0; v < VOLUME_COUNT; ++v) {
        const char *name = VOLUMES[v].name;
        size_t n = strlen(name);
        if (n < 2 || name[n - 1] != ':')
            continue;
        --n;
        if (n != len)
            continue;
        size_t i = 0;
        while (i < len && lower(token[i]) == lower(name[i]))
            ++i;
        if (i == len) {
            *out = (Volume)v;
            return 1;
        }
    }
    return 0;
}

int path_volume_is_server_exposed(Volume volume)
{
    return (volume_flags(volume) & VOLUME_FLAG_SERVER) != 0;
}

int path_volume_is_read_only(Volume volume)
{
    return (volume_flags(volume) & VOLUME_FLAG_READ_ONLY) != 0;
}

static int volume_from_token(const char *token, size_t len, Volume *out)
{
    return path_volume_from_token(token, len, out);
}

const char *path_volume_name(Volume volume)
{
    if ((unsigned)volume >= (unsigned)VOLUME_COUNT)
        return "";
    return VOLUMES[volume].name;
}

PathResult path_canonicalize(const char *input, Path *out)
{
    if (input == NULL || out == NULL)
        return PATH_INVALID;

    /* The denial table must see the bytes the adapter will open. A backslash
     * is not a separator here, and accepting it would let one spelling reach a
     * denied file if the platform folds it. */
    if (strchr(input, '\\') != NULL)
        return PATH_INVALID;

    const char *p = input;
    while (*p == '/')
        p++;

    const char *colon = strchr(p, ':');
    if (colon == NULL || colon == p)
        return PATH_INVALID;

    Volume volume;
    if (!volume_from_token(p, (size_t)(colon - p), &volume))
        return PATH_INVALID;

    const char *prefix = path_volume_name(volume);
    size_t prefix_len = strlen(prefix);
    size_t used = prefix_len;
    memcpy(out->text, prefix, prefix_len);
    out->text[used] = '\0';

    const char *rest = colon + 1;
    int segments = 0;

    while (*rest != '\0') {
        while (*rest == '/')
            rest++;
        if (*rest == '\0')
            break;

        const char *start = rest;
        while (*rest != '\0' && *rest != '/')
            rest++;
        size_t len = (size_t)(rest - start);

        if (len == 1 && start[0] == '.')
            continue;

        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (segments > 0) {
                char *last = strrchr(out->text, '/');
                if (last != NULL) {
                    *last = '\0';
                    used = (size_t)(last - out->text);
                } else {
                    out->text[prefix_len] = '\0';
                    used = prefix_len;
                }
                segments--;
            }
            continue;
        }

        if (segments == 0) {
            if (used + len >= sizeof(out->text))
                return PATH_INVALID;
            memcpy(out->text + used, start, len);
            used += len;
            out->text[used] = '\0';
        } else {
            if (used + 1 + len >= sizeof(out->text))
                return PATH_INVALID;
            out->text[used++] = '/';
            memcpy(out->text + used, start, len);
            used += len;
            out->text[used] = '\0';
        }
        segments++;
    }

    return PATH_OK;
}

PathResult path_join(const char *base, const char *name, Path *out)
{
    if (base == NULL || name == NULL || out == NULL)
        return PATH_INVALID;
    if (name[0] == '/' || strchr(name, ':') != NULL)
        return path_canonicalize(name, out);

    char combined[PATH_MAX_LEN];
    int n = snprintf(combined, sizeof(combined), "%s/%s", base, name);
    if (n < 0 || (size_t)n >= sizeof(combined))
        return PATH_INVALID;
    return path_canonicalize(combined, out);
}

PathResult path_parent(const char *path, Path *out)
{
    Path canonical;
    if (path_canonicalize(path, &canonical) != PATH_OK)
        return PATH_INVALID;

    char *slash = strrchr(canonical.text, '/');
    if (slash != NULL) {
        *slash = '\0';
    } else {
        char *colon = strchr(canonical.text, ':');
        if (colon != NULL)
            colon[1] = '\0';
    }
    *out = canonical;
    return PATH_OK;
}

int path_is_volume_root(const char *path)
{
    Path canonical;
    if (path_canonicalize(path, &canonical) != PATH_OK)
        return 0;
    for (int v = 0; v < VOLUME_COUNT; ++v) {
        if (ci_equal(canonical.text, path_volume_name((Volume)v)))
            return 1;
    }
    return 0;
}

int path_volume(const char *path, Volume *out)
{
    Path canonical;
    if (path_canonicalize(path, &canonical) != PATH_OK)
        return 0;
    for (int v = 0; v < VOLUME_COUNT; ++v) {
        const char *name = path_volume_name((Volume)v);
        if (ci_prefix(canonical.text, name))
            return *out = (Volume)v, 1;
    }
    return 0;
}

int path_is_protected(const char *path)
{
    Path canonical;
    if (path_canonicalize(path, &canonical) != PATH_OK)
        return 0;

    const char *p = canonical.text;
    for (int v = 0; v < VOLUME_COUNT; ++v) {
        const char *name = VOLUMES[v].name;
        if (ci_equal(p, name))
            return 1;
        if ((VOLUMES[v].flags & VOLUME_FLAG_PROTECT_DATA) != 0) {
            char data[16];
            snprintf(data, sizeof(data), "%sdata", name);
            if (ci_equal(p, data))
                return 1;
        }
    }
    return ci_equal(p, "ur0:tai") || ci_equal(p, "ux0:tai");
}

int path_is_high_risk(const char *path)
{
    Path canonical;
    if (path_canonicalize(path, &canonical) != PATH_OK)
        return 0;

    const char *p = canonical.text;
    if (path_is_protected(p))
        return 1;
    if (is_under(p, "ur0:tai") || is_under(p, "ux0:tai"))
        return 1;

    const char *name = NULL;
    size_t len = 0;
    if (!name_of_path(p, &name, &len))
        return 0;

    if (ci_equal(name, "config.txt") || ci_equal(name, "plugins.txt"))
        return 1;

    return ends_with_ci(name, ".skprx") || ends_with_ci(name, ".suprx");
}

static PathResult check_path(const char *path, int server_role, PathRisk *risk)
{
    /* The server_role no longer selects a write denial; boot_config below is
     * denied for every role and the protected-path table is shared. */
    (void)server_role;
    Path canonical;
    PathResult result = path_canonicalize(path, &canonical);
    if (result != PATH_OK)
        return result;

    const char *p = canonical.text;

    if (ci_equal(p, "ur0:tai/boot_config.txt"))
        return PATH_DENIED;

    if (path_is_protected(p))
        return PATH_PROTECTED;

    Volume volume;
    if (path_volume(p, &volume) && path_volume_is_read_only(volume))
        return PATH_READ_ONLY;

    if (risk != NULL)
        *risk = path_is_high_risk(p) ? PATH_RISK_HIGH : PATH_RISK_NORMAL;
    return PATH_OK;
}

PathResult path_check_read(const char *path, int server_role, PathRisk *risk)
{
    Path canonical;
    PathResult result = path_canonicalize(path, &canonical);
    if (result != PATH_OK)
        return result;

    const char *p = canonical.text;
    if (server_role && ci_equal(p, "ur0:tai/boot_config.txt"))
        return PATH_DENIED;

    if (risk != NULL)
        *risk = path_is_high_risk(p) ? PATH_RISK_HIGH : PATH_RISK_NORMAL;
    return PATH_OK;
}

PathSidecarKind path_sidecar_kind(const char *path)
{
    if (path == NULL)
        return PATH_SIDECAR_NONE;
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == ':')
            name = p + 1;
    }
    if (ends_with_ci(name, PATH_SIDECAR_TEMP))
        return PATH_SIDECAR_PART;
    if (ends_with_ci(name, PATH_SIDECAR_ASIDE))
        return PATH_SIDECAR_OLD;
    return PATH_SIDECAR_NONE;
}

int path_is_sidecar(const char *path)
{
    return path_sidecar_kind(path) != PATH_SIDECAR_NONE;
}

int path_is_sidecar_of(const char *candidate, const char *base)
{
    if (candidate == NULL || base == NULL)
        return 0;
    char expected[PATH_MAX_LEN];
    if (path_sidecar_path(expected, sizeof(expected), base, PATH_SIDECAR_TEMP) &&
        ci_equal(candidate, expected))
        return 1;
    return path_sidecar_path(expected, sizeof(expected), base,
                             PATH_SIDECAR_ASIDE) &&
           ci_equal(candidate, expected);
}

int path_sidecar_path(char *out, size_t cap, const char *target,
                      const char *suffix)
{
    if (out == NULL || cap == 0 || target == NULL || suffix == NULL)
        return 0;
    int n = snprintf(out, cap, "%s%s", target, suffix);
    return n > 0 && (size_t)n < cap;
}

PathResult path_check_write(const char *path, int server_role, PathRisk *risk)
{
    PathResult result = check_path(path, server_role, risk);
    if (result == PATH_OK && path_is_sidecar(path))
        return PATH_DENIED;
    return result;
}

PathResult path_check_delete(const char *path, int server_role, PathRisk *risk)
{
    return check_path(path, server_role, risk);
}

PathResult path_check_rename(const char *from, const char *to, int server_role,
                             PathRisk *risk)
{
    PathRisk from_risk = PATH_RISK_NORMAL;
    PathRisk to_risk = PATH_RISK_NORMAL;

    PathResult a = check_path(from, server_role, &from_risk);
    if (a != PATH_OK)
        return a;
    PathResult b = check_path(to, server_role, &to_risk);
    if (b != PATH_OK)
        return b;
    /* A rename may not create a sidecar, and it may not steal a live temp.
     * The aside stays a legal source so a leftover can be repaired. */
    if (path_is_sidecar(to) || path_sidecar_kind(from) == PATH_SIDECAR_PART)
        return PATH_DENIED;

    if (risk != NULL)
        *risk = (from_risk == PATH_RISK_HIGH || to_risk == PATH_RISK_HIGH)
                    ? PATH_RISK_HIGH
                    : PATH_RISK_NORMAL;
    return PATH_OK;
}

int path_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;

    size_t len = strlen(name);
    if (len >= PATH_NAME_MAX)
        return 0;

    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        if (c == '/' || c == '\\' || c == ':')
            return 0;
    }
    return 1;
}
