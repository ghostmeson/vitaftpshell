/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_UI_DOCK_H
#define VFTP_UI_DOCK_H

#include <stdint.h>

#include "core/uistate.h"

/*
 * The 64 px transfer strip. `dock_draw` is the WORKSPACE dock with its five
 * settled states; `dock_panel_draw` is the same anatomy for the other screens,
 * which supply their own words.
 */
typedef struct {
    const char *word;
    uint32_t word_color;
    const char *subject;
    const char *path;

    const char *right_a;
    uint32_t right_a_color;
    const char *right_b;
    uint32_t right_b_color;

    const char *left;
    uint32_t left_color;
    const char *left2;
    const char *right_text;
    uint32_t right_text_color;

    float progress; /* 0..1; ignored when progress_valid is 0 */
    int progress_valid;
    uint32_t bar_color;
    int focused;
} DockPanel;

void dock_panel_draw(const DockPanel *panel);
void dock_draw(const UiState *state);

#endif
