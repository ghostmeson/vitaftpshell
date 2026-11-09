#include "core/path.h"

#include <stdio.h>
#include <string.h>

#define APP_DATA "ux0:data/vitaFTPShell"

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

static int volume_from_token(const char *token, size_t len, Volume *out)
{
    if (len == 3) {
        if ((token[0] == 'u' || token[0] == 'U') &&
            (token[1] == 'x' || token[1] == 'X') && token[2] == '0') {
            *out = VOLUME_UX0;
            return 1;
        }
        if ((token[0] == 'u' || token[0] == 'U') &&
            (token[1] == 'r' || token[1] == 'R') && token[2] == '0') {
            *out = VOLUME_UR0;
            return 1;
        }
    }
    if (len == 4 &&
        (token[0] == 'u' || token[0] == 'U') &&
        (token[1] == 'm' || token[1] == 'M') &&
        (token[2] == 'a' || token[2] == 'A') && token[3] == '0') {
        *out = VOLUME_UMA0;
        return 1;
    }
    return 0;
}

const char *path_volume_name(Volume volume)
{
    switch (volume) {
    case VOLUME_UX0:
        return "ux0:";
    case VOLUME_UR0:
        return "ur0:";
    case VOLUME_UMA0:
        return "uma0:";
    default:
        return "";
    }
}

PathResult path_canonicalize(const char *input, Path *out)
{
    if (input == NULL || out == NULL)
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
    return strlen(canonical.text) == 4;
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
    return ci_equal(p, "ux0:") || ci_equal(p, "ur0:") || ci_equal(p, "uma0:") ||
           ci_equal(p, "ur0:data") || ci_equal(p, "ux0:data") ||
           ci_equal(p, "ur0:tai") || ci_equal(p, "ux0:tai");
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
    Path canonical;
    PathResult result = path_canonicalize(path, &canonical);
    if (result != PATH_OK)
        return result;

    const char *p = canonical.text;

    if (ci_equal(p, "ur0:tai/boot_config.txt"))
        return PATH_DENIED;

    if (server_role) {
        if (is_under(p, "ur0:tai") || is_under(p, "ux0:tai"))
            return PATH_DENIED;
        if (is_under(p, APP_DATA))
            return PATH_DENIED;
    }

    if (path_is_protected(p))
        return PATH_PROTECTED;

    if (risk != NULL)
        *risk = path_is_high_risk(p) ? PATH_RISK_HIGH : PATH_RISK_NORMAL;
    return PATH_OK;
}

PathResult path_check_write(const char *path, int server_role, PathRisk *risk)
{
    return check_path(path, server_role, risk);
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
