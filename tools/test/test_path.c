/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>

#include "core/path.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void check_canonical(const char *input, const char *expected)
{
    Path p;
    CHECK(path_canonicalize(input, &p) == PATH_OK);
    if (strcmp(p.text, expected) != 0) {
        fprintf(stderr, "canonical %s -> %s, expected %s\n", input, p.text,
                expected);
        s_failures++;
    }
}

static void test_canonical(void)
{
    check_canonical("ux0:data", "ux0:data");
    check_canonical("UX0:/DATA", "ux0:DATA");
    check_canonical("ux0:data/../tai", "ux0:tai");
    check_canonical("ux0:data/../../tai", "ux0:tai");
    check_canonical("ux0:..", "ux0:");
    check_canonical("ux0:data/..", "ux0:");
    check_canonical("ux0:a/b/../../c", "ux0:c");
    check_canonical("//UR0://TAI//CONFIG.TXT", "ur0:TAI/CONFIG.TXT");
    check_canonical("ux0:data/", "ux0:data");

    Path p;
    CHECK(path_canonicalize("data/x", &p) == PATH_INVALID);
    CHECK(path_canonicalize("", &p) == PATH_INVALID);
    CHECK(path_canonicalize("ux1:data", &p) == PATH_INVALID);
    CHECK(path_canonicalize("ur0:tai\\boot_config.txt", &p) == PATH_INVALID);
}

static void test_parent_join(void)
{
    Path p;
    CHECK(path_parent("ux0:data/file", &p) == PATH_OK);
    CHECK(strcmp(p.text, "ux0:data") == 0);
    CHECK(path_parent("ux0:data", &p) == PATH_OK);
    CHECK(strcmp(p.text, "ux0:") == 0);
    CHECK(path_parent("ux0:", &p) == PATH_OK);
    CHECK(strcmp(p.text, "ux0:") == 0);

    CHECK(path_join("ux0:data", "file", &p) == PATH_OK);
    CHECK(strcmp(p.text, "ux0:data/file") == 0);
    CHECK(path_join("ux0:data", "dir/../file", &p) == PATH_OK);
    CHECK(strcmp(p.text, "ux0:data/file") == 0);
}

static void test_volume(void)
{
    CHECK(path_is_volume_root("ux0:"));
    CHECK(path_is_volume_root("UR0:/"));
    CHECK(path_is_volume_root("uma0:"));
    CHECK(!path_is_volume_root("ux0:data"));

    Volume v;
    CHECK(path_volume("ux0:data", &v) && v == VOLUME_UX0);
    CHECK(path_volume("UR0:data", &v) && v == VOLUME_UR0);
    CHECK(path_volume("uma0:data", &v) && v == VOLUME_UMA0);
    CHECK(!path_volume("nope:data", &v));
}

static void test_safety(void)
{
    CHECK(path_name_is_safe("file.txt"));
    CHECK(path_name_is_safe("name with spaces"));
    CHECK(!path_name_is_safe(""));
    CHECK(!path_name_is_safe("."));
    CHECK(!path_name_is_safe(".."));
    CHECK(!path_name_is_safe("a/b"));
    CHECK(!path_name_is_safe("a\\b"));
    CHECK(!path_name_is_safe("a:b"));
}

static void test_denials_and_risk(void)
{
    PathRisk risk = PATH_RISK_NORMAL;

    CHECK(path_check_write("ur0:tai/boot_config.txt", 0, &risk) == PATH_DENIED);
    CHECK(path_check_write("UR0:/TAI/./BOOT_CONFIG.TXT", 0, &risk) ==
          PATH_DENIED);
    CHECK(path_check_write("ux0:tai/boot_config.txt", 1, &risk) == PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_check_write("ur0:tai\\boot_config.txt", 0, &risk) ==
          PATH_INVALID);
    CHECK(path_check_write("ur0:tai\\boot_config.txt", 1, &risk) ==
          PATH_INVALID);

    CHECK(path_check_read("ux0:data/vitaFTPShell/sites.cfg", 1, &risk) ==
          PATH_OK);
    CHECK(path_check_read("ux0:data/vitaFTPShell/sites.cfg", 0, &risk) ==
          PATH_OK);
    CHECK(path_check_read("ur0:tai/config.txt", 1, &risk) == PATH_OK);
    CHECK(path_check_read("ur0:tai/boot_config.txt", 1, &risk) == PATH_DENIED);
    CHECK(path_check_read("ur0:tai/boot_config.txt", 0, &risk) == PATH_OK);

    CHECK(path_check_write("ur0:tai/config.txt", 0, &risk) == PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_check_write("ur0:tai/config.txt", 1, &risk) == PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);

    CHECK(path_check_write("UX0:/TAI/X.SUPRX", 1, &risk) == PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_check_write("UX0:/TAI/X.SUPRX", 0, &risk) == PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_check_write("//UR0://TAI//CONFIG.TXT", 1, &risk) == PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_check_write("//UR0://TAI//CONFIG.TXT", 0, &risk) == PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_is_high_risk("ux0:"));
    CHECK(path_is_high_risk("ux0:data/file.skprx"));
    CHECK(!path_is_high_risk("ux0:data/file.txt"));

    CHECK(path_check_write("ux0:data/vitaFTPShell/sites.cfg", 0, &risk) ==
          PATH_OK);
    CHECK(risk == PATH_RISK_NORMAL);
    CHECK(path_check_write("ux0:data/vitaFTPShell/sites.cfg", 1, &risk) ==
          PATH_OK);

    CHECK(path_check_delete("ux0:", 0, &risk) == PATH_PROTECTED);
    CHECK(path_check_delete("ux0:data", 0, &risk) == PATH_PROTECTED);
    CHECK(path_check_delete("ux0:tai", 1, &risk) == PATH_PROTECTED);
    CHECK(path_check_delete("ux0:tai", 0, &risk) == PATH_PROTECTED);

    CHECK(path_check_rename("ux0:data/a", "ux0:data/b", 0, &risk) == PATH_OK);
    CHECK(path_check_rename("ux0:data/a", "ux0:data", 0, &risk) ==
          PATH_PROTECTED);
    CHECK(path_check_rename("ur0:tai/config.txt", "ux0:data/x", 0, &risk) ==
          PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_check_rename("ur0:tai/config.txt", "ux0:data/x", 1, &risk) ==
          PATH_OK);
    CHECK(risk == PATH_RISK_HIGH);
    CHECK(path_check_rename("ux0:data/a", "ux0:data/a.vftp.part", 0, &risk) ==
          PATH_DENIED);
    CHECK(path_check_rename("ux0:data/a", "ux0:data/a.VFTP.OLD", 0, &risk) ==
          PATH_DENIED);
    CHECK(path_check_rename("ux0:data/a.vftp.part", "ux0:data/a", 0, &risk) ==
          PATH_DENIED);
    CHECK(path_check_rename("ux0:data/a.vftp.old", "ux0:data/a", 0, &risk) ==
          PATH_OK);
}

static void test_sidecar_names(void)
{
    CHECK(path_sidecar_kind("ux0:data/A.BIN.vftp.part") == PATH_SIDECAR_PART);
    CHECK(path_sidecar_kind("ux0:data/A.BIN.VFTP.OLD") == PATH_SIDECAR_OLD);
    CHECK(path_sidecar_kind("ux0:data/A.BIN") == PATH_SIDECAR_NONE);
    CHECK(path_is_sidecar_of("ux0:data/A.BIN.vftp.old", "ux0:data/A.BIN"));
    CHECK(!path_is_sidecar_of("ux0:data/B.BIN.vftp.old", "ux0:data/A.BIN"));

    char out[PATH_MAX_LEN];
    CHECK(path_sidecar_path(out, sizeof(out), "ux0:data/A.BIN",
                            PATH_SIDECAR_TEMP));
    CHECK(strcmp(out, "ux0:data/A.BIN.vftp.part") == 0);

    /* The app never creates a name its own recovery would claim. */
    CHECK(path_check_write("ux0:data/A.BIN.vftp.part", 0, NULL) ==
          PATH_DENIED);
    CHECK(path_check_write("ux0:data/A.BIN.vftp.old", 1, NULL) ==
          PATH_DENIED);
    CHECK(path_check_write("ux0:data/A.BIN", 0, NULL) == PATH_OK);
}

/* The reservation module consumes these; one rule, one owner. */
static void test_shared_helpers(void)
{
    CHECK(path_ci_equal("ux0:data/A.TXT", "UX0:DATA/a.txt"));
    CHECK(!path_ci_equal("ux0:data/a", "ux0:data/b"));
    CHECK(path_is_ancestor("ux0:", "ux0:data"));
    CHECK(path_is_ancestor("ux0:data", "ux0:data/a.bin"));
    CHECK(!path_is_ancestor("ux0:data/a", "ux0:data/a"));
    CHECK(!path_is_ancestor("ux0:data/a", "ux0:data/ab"));
}

int main(void)
{
    test_canonical();
    test_parent_join();
    test_volume();
    test_safety();
    test_denials_and_risk();
    test_sidecar_names();
    test_shared_helpers();

    if (s_failures != 0) {
        fprintf(stderr, "test_path: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_path: OK\n");
    return 0;
}
