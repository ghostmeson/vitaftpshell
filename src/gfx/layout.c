/* SPDX-License-Identifier: GPL-3.0-only */

#include "gfx/layout.h"

#include <stddef.h>

/*
 * Every region is copied from the geometry table in docs/DESIGN.md. The order
 * is the draw order: header, content, dock, legend.
 */

static const LayoutRect WORKSPACE[] = {
    {0, 0, 960, 64},
    {18, 76, 456, 352},
    {486, 76, 456, 352},
    {18, 440, 924, 64},
    {18, 510, 924, 16},
};

static const LayoutRect SITES[] = {
    {0, 0, 960, 64},
    {18, 76, 924, 110},
    {18, 198, 924, 230},
    {18, 440, 924, 64},
    {18, 510, 924, 16},
};

static const LayoutRect SITE_EDITOR[] = {
    {0, 0, 960, 64},
    {18, 76, 924, 110},
    {18, 198, 924, 230},
    {18, 440, 924, 64},
    {18, 510, 924, 16},
};

static const LayoutRect SERVER[] = {
    {0, 0, 960, 64},
    {18, 76, 552, 352},
    {582, 76, 360, 200},
    {582, 288, 360, 140},
    {18, 440, 924, 64},
    {18, 510, 924, 16},
};

static const LayoutRect ACTIONS[] = {
    {0, 0, 960, 64},
    {18, 76, 552, 352},
    {582, 76, 360, 352},
    {18, 440, 924, 64},
    {18, 510, 924, 16},
};

static const LayoutRect CONFIRM[] = {
    {180, 122, 600, 300},
};

/* The system IME owns the screen; the app draws no region for it. */
static const LayoutRect TEXT_ENTRY[] = {
    {0, 0, 0, 0},
};

const LayoutRect *layout_regions(LayoutSurface surface, unsigned *count)
{
    const LayoutRect *regions = NULL;
    unsigned n = 0;

    switch (surface) {
    case LAYOUT_WORKSPACE:
        regions = WORKSPACE;
        n = sizeof(WORKSPACE) / sizeof(WORKSPACE[0]);
        break;
    case LAYOUT_SITES:
        regions = SITES;
        n = sizeof(SITES) / sizeof(SITES[0]);
        break;
    case LAYOUT_SITE_EDITOR:
        regions = SITE_EDITOR;
        n = sizeof(SITE_EDITOR) / sizeof(SITE_EDITOR[0]);
        break;
    case LAYOUT_SERVER:
        regions = SERVER;
        n = sizeof(SERVER) / sizeof(SERVER[0]);
        break;
    case LAYOUT_ACTIONS:
        regions = ACTIONS;
        n = sizeof(ACTIONS) / sizeof(ACTIONS[0]);
        break;
    case LAYOUT_CONFIRM:
        regions = CONFIRM;
        n = sizeof(CONFIRM) / sizeof(CONFIRM[0]);
        break;
    case LAYOUT_TEXT_ENTRY:
        regions = TEXT_ENTRY;
        n = 0; /* the zero rect is a placeholder, not a drawable region */
        break;
    default:
        break;
    }

    if (count != NULL)
        *count = n;
    return regions;
}

const char *layout_surface_name(LayoutSurface surface)
{
    switch (surface) {
    case LAYOUT_WORKSPACE:
        return "WORKSPACE";
    case LAYOUT_SITES:
        return "SITES";
    case LAYOUT_SITE_EDITOR:
        return "SITE EDITOR";
    case LAYOUT_SERVER:
        return "SERVER";
    case LAYOUT_ACTIONS:
        return "ACTIONS";
    case LAYOUT_CONFIRM:
        return "CONFIRM";
    case LAYOUT_TEXT_ENTRY:
        return "TEXT ENTRY";
    default:
        return "UNKNOWN";
    }
}
