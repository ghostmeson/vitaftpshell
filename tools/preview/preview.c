/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "vita2d.h"

/*
 * Host preview. Draws a settled WORKSPACE composition through the real gfx
 * layer and the software vita2d shim, then writes a PNG. It also proves every
 * layout region is inside the 960x544 field before it renders.
 *
 * Run with tools/preview/run.sh [output-dir].
 */

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data,
                      uint32_t len)
{
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, sizeof(hdr), f);
    if (len > 0)
        fwrite(data, 1, len, f);

    uint32_t c = (uint32_t)crc32(0L, Z_NULL, 0);
    c = (uint32_t)crc32(c, (const Bytef *)type, 4);
    if (len > 0)
        c = (uint32_t)crc32(c, data, len);

    uint8_t cb[4];
    be32(cb, c);
    fwrite(cb, 1, sizeof(cb), f);
}

static int write_png(const char *path)
{
    int w = preview_width();
    int h = preview_height();
    const uint32_t *fb = preview_pixels();

    size_t stride = 1 + (size_t)w * 3;
    size_t raw_len = (size_t)h * stride;
    uint8_t *raw = malloc(raw_len);
    if (raw == NULL)
        return 0;

    for (int y = 0; y < h; ++y) {
        uint8_t *row = raw + (size_t)y * stride;
        row[0] = 0;
        for (int x = 0; x < w; ++x) {
            uint32_t c = fb[(size_t)y * (size_t)w + (size_t)x];
            row[1 + x * 3 + 0] = (uint8_t)(c & 0xFF);
            row[1 + x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
            row[1 + x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
        }
    }

    uLongf comp_len = compressBound((uLong)raw_len);
    uint8_t *comp = malloc(comp_len);
    if (comp == NULL) {
        free(raw);
        return 0;
    }
    if (compress2(comp, &comp_len, raw, (uLong)raw_len, 9) != Z_OK) {
        free(raw);
        free(comp);
        return 0;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(raw);
        free(comp);
        return 0;
    }

    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(SIG, 1, sizeof(SIG), f);

    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    png_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    png_chunk(f, "IDAT", comp, (uint32_t)comp_len);
    png_chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(raw);
    free(comp);
    return 1;
}

static int layout_is_inside(void)
{
    for (int s = 0; s < LAYOUT_SURFACE_COUNT; ++s) {
        unsigned n = 0;
        const LayoutRect *r = layout_regions((LayoutSurface)s, &n);
        for (unsigned i = 0; i < n; ++i) {
            if (r[i].x < 0 || r[i].y < 0 || r[i].w < 0 || r[i].h < 0 ||
                r[i].x + r[i].w > LAYOUT_SCREEN_W ||
                r[i].y + r[i].h > LAYOUT_SCREEN_H) {
                fprintf(stderr, "layout: %s region %u escapes the field\n",
                        layout_surface_name((LayoutSurface)s), i);
                return 0;
            }
        }
    }
    return 1;
}

static void draw_demo(void)
{
    unsigned n = 0;
    const LayoutRect *r = layout_regions(LAYOUT_WORKSPACE, &n);
    if (n < 5)
        return;

    draw_rect(0, 0, LAYOUT_SCREEN_W, LAYOUT_SCREEN_H, C_FIELD);

    theme_panel((Rect){r[0].x, r[0].y, r[0].w, r[0].h}, "VITAFTPSHELL",
                C_YELLOW, CHROME_STRIP);
    font_text_right("NOT CONNECTED", 942, 22, PX_MICRO, C_DIM);
    font_text_right("SERVER OFF", 942, 45.5f, PX_MICRO, C_DIM);

    theme_panel((Rect){r[1].x, r[1].y, r[1].w, r[1].h}, "LOCAL", C_CYAN,
                CHROME_SATELLITE);
    font_text("UX0:DATA", r[1].x + 32, r[1].y + 30, PX_MICRO, C_CYAN_DIM);
    draw_rect((float)(r[1].x + LAYOUT_ROW_CURSOR_X), r[1].y + 52, 2.0f,
              (float)LAYOUT_ROW_H, C_WHITE);
    font_text("..", r[1].x + LAYOUT_ROW_NAME_X, r[1].y + 60, PX_LABEL, C_DIM);
    font_text("UP", r[1].x + r[1].w - 14 - (int)font_width("UP", PX_LABEL),
              r[1].y + 60, PX_LABEL, C_DIM);
    font_text("VITASHELL.VPK", r[1].x + LAYOUT_ROW_NAME_X, r[1].y + 90,
              PX_LABEL, C_WHITE);
    font_text("12.4 MB", r[1].x + r[1].w - 14 - (int)font_width("12.4 MB", PX_LABEL),
              r[1].y + 90, PX_LABEL, C_WHITE);

    theme_panel((Rect){r[2].x, r[2].y, r[2].w, r[2].h}, "REMOTE", C_CYAN,
                CHROME_SATELLITE);
    font_text("NOT CONNECTED", r[2].x + r[2].w / 2 - 45, r[2].y + 150,
              PX_LABEL, C_DIM);

    theme_panel((Rect){r[3].x, r[3].y, r[3].w, r[3].h}, "IDLE", C_WHITE,
                CHROME_LIST);
    font_text("LOCAL  UX0:DATA", r[3].x + 32, r[3].y + 32, PX_LABEL, C_WHITE);
    font_text_right("NO MARKS", r[3].x + r[3].w - 14, r[3].y + 32, PX_MICRO,
                    C_DIM);
    font_text("[X] OPEN/SEND   [O] UP   [SQUARE] MARK   [TRIANGLE] SITES",
              r[4].x, r[4].y, PX_MICRO, C_DIM);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/preview";
    char path[1024];

    if (!layout_is_inside())
        return 1;

    preview_clear();
    draw_demo();

    if (preview_draw_calls() == 0) {
        fprintf(stderr, "preview: nothing was drawn\n");
        return 1;
    }

    snprintf(path, sizeof(path), "%s/workspace.png", dir);
    if (!write_png(path)) {
        fprintf(stderr, "preview: could not write %s\n", path);
        return 1;
    }

    printf("preview: %s (%d draw calls)\n", path, preview_draw_calls());
    return 0;
}
