#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "vita2d.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_font(void)
{
    CHECK(font_width("ABCDEFGHIJ", 2.0f) == 118.0f);
    CHECK(font_width("", 2.0f) == 0.0f);

    char out[64];
    font_fit("SHORT", 2.0f, 200.0f, out, sizeof(out));
    CHECK(strcmp(out, "SHORT") == 0);

    font_fit("A VERY LONG FILE NAME THAT DOES NOT FIT", 2.0f, 80.0f, out,
             sizeof(out));
    CHECK(strlen(out) >= 2);
    CHECK(strcmp(out + strlen(out) - 2, "..") == 0);
    CHECK(font_width(out, 2.0f) <= 80.0f);

    font_fit("ABCDEFGHIJ", 2.0f, 5.0f, out, sizeof(out));
    CHECK(out[0] == '\0');
}

static void test_palette(void)
{
    CHECK(C_FIELD == 0xFF000000u);
    CHECK(C_PANEL == 0xFF120E0Du);
    CHECK(C_HAIR == 0xFF241D1Cu);
    CHECK(C_DIM == 0xFF7E706Eu);
    CHECK(C_WHITE == 0xFFF0EDEDu);
    CHECK(C_YELLOW == 0xFF0AEEFCu);
    CHECK(C_CYAN == 0xFFE8D82Au);
    CHECK(C_GREEN == 0xFF7AE03Bu);
    CHECK(C_GREEN_DIM == 0xFF5CA82Fu);
    CHECK(C_RED == 0xFF4D2EFFu);
    CHECK(C_CYAN_DIM == 0xFF8B8119u);
}

static void test_layout(void)
{
    for (int s = 0; s < LAYOUT_SURFACE_COUNT; ++s) {
        unsigned n = 0;
        const LayoutRect *r = layout_regions((LayoutSurface)s, &n);
        for (unsigned i = 0; i < n; ++i) {
            CHECK(r[i].x >= 0);
            CHECK(r[i].y >= 0);
            CHECK(r[i].w >= 0);
            CHECK(r[i].h >= 0);
            CHECK(r[i].x + r[i].w <= LAYOUT_SCREEN_W);
            CHECK(r[i].y + r[i].h <= LAYOUT_SCREEN_H);
        }
    }

    unsigned n = 0;
    const LayoutRect *w = layout_regions(LAYOUT_WORKSPACE, &n);
    CHECK(n == 5);
    CHECK(w[0].x == 0 && w[0].y == 0 && w[0].w == 960 && w[0].h == 64);
    CHECK(w[1].x == 18 && w[1].y == 76 && w[1].w == 456 && w[1].h == 352);
    CHECK(w[2].x == 486 && w[2].y == 76);
    CHECK(w[2].x == w[1].x + w[1].w + LAYOUT_GUTTER);
    CHECK(w[3].y == w[1].y + w[1].h + LAYOUT_DOCK_GAP);
    CHECK(strcmp(layout_surface_name(LAYOUT_WORKSPACE), "WORKSPACE") == 0);

    const LayoutRect *c = layout_regions(LAYOUT_CONFIRM, &n);
    CHECK(n == 1);
    CHECK(c[0].x == 180 && c[0].y == 122 && c[0].w == 600 && c[0].h == 300);
}

static void test_theme_draws(void)
{
    preview_clear();
    theme_panel((Rect){18, 76, 456, 352}, "LOCAL", C_CYAN,
                CHROME_SATELLITE);
    CHECK(preview_draw_calls() > 0);

    char buf[16];
    CHECK(theme_fmt_int(buf, sizeof(buf), 0, 0) == 0);
    CHECK(strcmp(buf, "--") == 0);
    CHECK(theme_fmt_int(buf, sizeof(buf), 7, 1) == 1);
    CHECK(strcmp(buf, "7") == 0);
}

int main(void)
{
    test_font();
    test_palette();
    test_layout();
    test_theme_draws();

    if (s_failures != 0) {
        fprintf(stderr, "test_gfx: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_gfx: OK\n");
    return 0;
}
