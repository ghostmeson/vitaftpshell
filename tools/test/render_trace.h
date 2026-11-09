/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_TEST_RENDER_TRACE_H
#define VFTP_TEST_RENDER_TRACE_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gfx/font.h"
#include "vita2d.h"

/*
 * Shared render-test trace. Each test file gets its own copy; the harness
 * records the strings font.c was asked to draw and the rectangles and lines
 * the software vita2d shim received.
 */

#define TRACE_TEXT_MAX 4096
#define TRACE_TEXT_LEN 256

typedef struct {
    char text[TRACE_TEXT_LEN];
    float x, y, px;
    uint32_t color;
} TraceText;

static TraceText s_trace_text[TRACE_TEXT_MAX];
static int s_trace_text_count;

static __attribute__((unused)) void trace_text_hook(const char *text, float x, float y, float px,
                            uint32_t color)
{
    if (s_trace_text_count >= TRACE_TEXT_MAX || text == NULL)
        return;
    TraceText *t = &s_trace_text[s_trace_text_count++];
    snprintf(t->text, sizeof(t->text), "%s", text);
    t->x = x;
    t->y = y;
    t->px = px;
    t->color = color;
}

static __attribute__((unused)) void trace_reset(void)
{
    s_trace_text_count = 0;
    preview_trace_reset();
    font_set_trace(trace_text_hook);
}

static __attribute__((unused)) int near(float a, float b)
{
    return fabsf(a - b) <= 0.51f;
}

static __attribute__((unused)) int trace_has_text(const char *text)
{
    for (int i = 0; i < s_trace_text_count; ++i) {
        if (strcmp(s_trace_text[i].text, text) == 0)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_has_text_prefix(const char *prefix)
{
    size_t n = strlen(prefix);
    for (int i = 0; i < s_trace_text_count; ++i) {
        if (strncmp(s_trace_text[i].text, prefix, n) == 0)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_has_text_color(const char *text, uint32_t color)
{
    for (int i = 0; i < s_trace_text_count; ++i) {
        if (strcmp(s_trace_text[i].text, text) == 0 &&
            s_trace_text[i].color == color)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_count_text_color(const char *text, uint32_t color)
{
    int count = 0;
    for (int i = 0; i < s_trace_text_count; ++i) {
        if (strcmp(s_trace_text[i].text, text) == 0 &&
            s_trace_text[i].color == color)
            count++;
    }
    return count;
}

static __attribute__((unused)) int trace_has_text_suffix(const char *suffix)
{
    size_t n = strlen(suffix);
    for (int i = 0; i < s_trace_text_count; ++i) {
        size_t len = strlen(s_trace_text[i].text);
        if (len >= n && strcmp(s_trace_text[i].text + len - n, suffix) == 0)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_text_at(const char *text, float x, float y)
{
    for (int i = 0; i < s_trace_text_count; ++i) {
        if (strcmp(s_trace_text[i].text, text) == 0 &&
            near(s_trace_text[i].x, x) && near(s_trace_text[i].y, y))
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_text_at_color(const char *text, float x, float y,
                               uint32_t color)
{
    for (int i = 0; i < s_trace_text_count; ++i) {
        if (strcmp(s_trace_text[i].text, text) == 0 &&
            near(s_trace_text[i].x, x) && near(s_trace_text[i].y, y) &&
            s_trace_text[i].color == color)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_text_right(const char *text, float x_right, float y,
                            float px, uint32_t color)
{
    float expected = x_right - font_width(text, px);
    return trace_text_at_color(text, expected, y, color);
}

static __attribute__((unused)) int trace_has_rect(float x, float y, float w, float h, uint32_t color)
{
    for (int i = 0; i < preview_trace_count(); ++i) {
        PreviewTrace t;
        if (!preview_trace_get(i, &t))
            continue;
        if (t.kind == PREVIEW_TRACE_RECT && near(t.x, x) && near(t.y, y) &&
            near(t.w, w) && near(t.h, h) && t.color == color)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_has_rect_at(float x, float y, uint32_t color)
{
    for (int i = 0; i < preview_trace_count(); ++i) {
        PreviewTrace t;
        if (!preview_trace_get(i, &t))
            continue;
        if (t.kind == PREVIEW_TRACE_RECT && near(t.x, x) && near(t.y, y) &&
            t.color == color)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_has_rect_within(float x, float y, float w, float h,
                                 uint32_t color)
{
    for (int i = 0; i < preview_trace_count(); ++i) {
        PreviewTrace t;
        if (!preview_trace_get(i, &t))
            continue;
        if (t.kind != PREVIEW_TRACE_RECT || t.color != color)
            continue;
        if (t.x >= x - 0.51f && t.y >= y - 0.51f &&
            t.x + t.w <= x + w + 0.51f && t.y + t.h <= y + h + 0.51f)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_has_line(float x0, float y0, float x1, float y1,
                          uint32_t color)
{
    for (int i = 0; i < preview_trace_count(); ++i) {
        PreviewTrace t;
        if (!preview_trace_get(i, &t))
            continue;
        if (t.kind == PREVIEW_TRACE_LINE && near(t.x, x0) && near(t.y, y0) &&
            near(t.w, x1) && near(t.h, y1) && t.color == color)
            return 1;
    }
    return 0;
}

static __attribute__((unused)) int trace_count_rect_at(float x, float y, uint32_t color)
{
    int count = 0;
    for (int i = 0; i < preview_trace_count(); ++i) {
        PreviewTrace t;
        if (!preview_trace_get(i, &t))
            continue;
        if (t.kind == PREVIEW_TRACE_RECT && near(t.x, x) && near(t.y, y) &&
            t.color == color)
            count++;
    }
    return count;
}

#endif
