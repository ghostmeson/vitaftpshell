/* SPDX-License-Identifier: GPL-3.0-only */

#include "ui/server.h"

#include <stdio.h>
#include <string.h>

#include "core/fmt.h"
#include "gfx/font.h"
#include "gfx/layout.h"
#include "gfx/theme.h"
#include "ui/dock.h"
#include "ui/parts.h"

static const char *state_word(const UiServer *server)
{
    switch (server->run) {
    case UI_SERVER_STARTING:
        return "STARTING";
    case UI_SERVER_ON:
        return "ON";
    case UI_SERVER_OFF:
    default:
        return "OFF";
    }
}

static uint32_t state_color(const UiServer *server)
{
    switch (server->run) {
    case UI_SERVER_STARTING:
        return C_YELLOW;
    case UI_SERVER_ON:
        return C_GREEN;
    case UI_SERVER_OFF:
    default:
        return C_DIM;
    }
}

static void status_draw(const UiState *state, const LayoutRect *r)
{
    const UiServer *server = &state->server;

    Rect panel = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(panel, 0, C_CYAN);

    font_text_tracked("STATUS", 32.0f, 84.0f, PX_LABEL, C_DIM, 2.0f);
    font_text(state_word(server), 32.0f, 108.0f, PX_PRIMARY,
              state_color(server));

    font_text("FTP://", 32.0f, 166.5f, PX_MICRO, C_DIM);
    float ip_x = 32.0f + font_width("FTP://", PX_MICRO) + 8.0f;
    const char *address =
        server->address_known && server->address[0] != '\0'
            ? server->address
            : "--";
    font_text(address, ip_x, 156.0f, PX_VALUE,
              server->run == UI_SERVER_ON ? C_CYAN : C_DIM);
    char port[16];
    snprintf(port, sizeof(port), ":%d", server->port);
    font_text(port, ip_x + font_width(address, PX_VALUE) + 6.0f, 166.5f,
              PX_MICRO, server->run == UI_SERVER_ON ? C_CYAN_DIM : C_DIM);

    if (server->error[0] != '\0')
        font_text(server->error, 32.0f, 190.0f, PX_MICRO, C_YELLOW);

    if (server->port_focus) {
        ui_wash(26.0f, 210.0f, 516.0f, 30.0f, 0.04f);
        draw_rect(26.0f, 210.0f, 2.0f, 30.0f, C_WHITE);
    }

    char value[32];
    snprintf(value, sizeof(value), "%d", server->port);
    font_text("PORT", 32.0f, 218.0f, PX_LABEL, C_DIM);
    font_text_right(value, 538.0f, 214.0f, PX_VALUE, C_WHITE);

    snprintf(value, sizeof(value), "%d", server->clients);
    font_text("CLIENTS", 32.0f, 248.0f, PX_LABEL, C_DIM);
    font_text_right(value, 538.0f, 244.0f, PX_VALUE, C_WHITE);

    font_text("ACCESS", 32.0f, 278.0f, PX_LABEL, C_DIM);
    font_text_right("READ-WRITE", 538.0f, 274.0f, PX_VALUE, C_WHITE);

    const char *slab_label;
    int primary = 0;
    if (server->run == UI_SERVER_OFF) {
        slab_label = "START SERVER";
        primary = 1;
    } else if (server->run == UI_SERVER_STARTING) {
        slab_label = "CANCEL";
        primary = 1;
    } else {
        slab_label = "STOP SERVER";
    }
    ui_slab(32.0f, 340.0f, 488.0f, 40.0f, slab_label, primary,
            server->focus_start, PX_LABEL);
}

static void warning_draw(const LayoutRect *r)
{
    Rect panel = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(panel, 0, C_YELLOW);

    font_text_tracked("WARNING", 596.0f, 84.0f, PX_LABEL, C_YELLOW, 2.0f);

    static const char *const yellow[] = {
        "ANONYMOUS CLIENTS CAN READ",
        "AND WRITE UX0:, UR0:, AND UMA0.",
    };
    static const char *const dim[] = {
        "UX0:/DATA/VITAFTPSHELL IS EXCLUDED.",
        "UR0:TAI AND UX0:TAI ARE READ-ONLY.",
        "UR0:TAI/BOOT_CONFIG.TXT IS DENIED.",
        "AN UPLOAD REPLACES A FILE OF THE",
        "SAME NAME.",
        "THE SERVER IS OFF AT LAUNCH.",
        "STOPPING CLOSES THE LISTENER",
        "AND THE ACTIVE SESSION.",
    };

    ui_lines(yellow, 2, 596.0f, 108.0f, 16.0f, PX_MICRO, C_YELLOW);
    ui_lines(dim, 8, 596.0f, 140.0f, 16.0f, PX_MICRO, C_DIM);
}

static void client_draw(const UiState *state, const LayoutRect *r)
{
    const UiServer *server = &state->server;

    Rect panel = {(float)r->x, (float)r->y, (float)r->w, (float)r->h};
    ui_panel_frame(panel, 0, C_CYAN);

    font_text_tracked("CLIENT", 596.0f, 296.0f, PX_LABEL, C_DIM, 2.0f);

    if (!server->client_active) {
        font_text("NO CLIENT", 596.0f, 320.0f, PX_LABEL, C_DIM);
        font_text("ONE CLIENT AT A TIME", 596.0f, 342.0f, PX_MICRO, C_DIM);
        return;
    }

    font_text_right("CONNECTED", 928.0f, 299.5f, PX_MICRO, C_GREEN);
    font_text(server->client_addr[0] != '\0' ? server->client_addr : "--",
              596.0f, 320.0f, PX_LABEL, C_WHITE);

    const char *op = server->client_op[0] != '\0' ? server->client_op
                                                  : "BROWSING";
    font_text(op, 596.0f, 342.0f, PX_LABEL, C_WHITE);
    font_text(server->client_path, 596.0f + font_width(op, PX_LABEL) + 10.0f,
              345.5f, PX_MICRO, C_CYAN);

    char bytes[96];
    if (server->client_total_known) {
        char done[32];
        char total[32];
        fmt_size(done, sizeof(done), server->client_bytes, 1);
        fmt_size(total, sizeof(total), server->client_total, 1);
        snprintf(bytes, sizeof(bytes), "%s OF %s", done, total);
        font_text(bytes, 596.0f, 362.0f, PX_MICRO, C_WHITE);
    } else {
        font_text("NO TRANSFER", 596.0f, 362.0f, PX_MICRO, C_DIM);
    }

    font_text("ONE CLIENT AT A TIME", 596.0f, 382.0f, PX_MICRO, C_DIM);
}

static void dock_server(const UiState *state)
{
    const UiServer *server = &state->server;

    if (state->dock.active &&
        (state->dock.direction == XFER_DIR_SERVER_UPLOAD ||
         state->dock.direction == XFER_DIR_SERVER_DOWNLOAD)) {
        dock_draw(state);
        return;
    }

    DockPanel p;
    memset(&p, 0, sizeof(p));
    char clients[32];
    char port[24];

    if (server->run == UI_SERVER_STARTING) {
        p.word = "STARTING";
        p.word_color = C_YELLOW;
        p.subject = "SERVER";
        p.left = "X TO CANCEL";
    } else if (server->run == UI_SERVER_ON) {
        p.word = "RUNNING";
        p.word_color = C_GREEN;
        snprintf(clients, sizeof(clients), "%d CLIENT%s", server->clients,
                 server->clients == 1 ? "" : "S");
        p.subject = clients;
        p.left = "X TO STOP";
    } else {
        p.word = "IDLE";
        p.word_color = C_DIM;
        p.subject = "SERVER OFF";
        p.left = "X TO START";
    }

    p.left_color = C_WHITE;
    snprintf(port, sizeof(port), "PORT %d", server->port);
    p.right_a = port;
    p.right_a_color = C_DIM;
    dock_panel_draw(&p);
}

static void legend_draw(const UiState *state)
{
    float x = (float)LAYOUT_MARGIN;
    if (state->server.run == UI_SERVER_STARTING)
        x = ui_keycap("X", "CANCEL", x, 510.0f);
    else if (state->server.run == UI_SERVER_ON)
        x = ui_keycap("X", "STOP SERVER", x, 510.0f);
    else
        x = ui_keycap("X", "START SERVER", x, 510.0f);

    x = ui_keycap("O", "CLOSE", x, 510.0f);

    font_text("UP/DOWN", x, 512.0f, PX_MICRO, C_DIM);
    x += font_width("UP/DOWN", PX_MICRO) + 6.0f;
    font_text(state->server.focus_start ? "START" : "PORT", x, 512.0f,
              PX_MICRO, C_DIM);
}

void server_draw(const UiState *state)
{
    if (state == NULL)
        return;

    unsigned count = 0;
    const LayoutRect *r = layout_regions(LAYOUT_SERVER, &count);
    if (count < 6)
        return;

    status_draw(state, &r[1]);
    warning_draw(&r[2]);
    client_draw(state, &r[3]);
    dock_server(state);
    legend_draw(state);
}
