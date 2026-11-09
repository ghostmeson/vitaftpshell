/* SPDX-License-Identifier: GPL-3.0-only */

#include "app/app.h"
#include "gfx/draw.h"
#include "platform/psp2_clock.h"
#include "platform/psp2_fs.h"
#include "platform/psp2_ime.h"
#include "platform/psp2_input.h"
#include "platform/psp2_log.h"
#include "platform/psp2_net.h"

/*
 * Composition root. It constructs each adapter once, starts the app, and owns
 * the process lifetime. No policy is decided here and no platform type crosses
 * into the app; the adapters are the only psp2 boundary.
 */
int main(void)
{
    AppPorts ports;
    ports.net = psp2_net_port();
    ports.fs = psp2_fs_port();
    ports.input = psp2_input_port();
    ports.ime = psp2_ime_port();
    ports.clock = psp2_clock_port();
    ports.log = psp2_log_port();

    if (!draw_init())
        return 1;

    App *app = app_create(&ports, 1);
    if (app == NULL) {
        draw_shutdown();
        return 1;
    }

    if (!app_start(app)) {
        app_destroy(app);
        draw_shutdown();
        return 1;
    }

    while (app_tick(app))
        ports.clock->sleep_ms(ports.clock->ctx, 16);

    app_destroy(app);
    draw_shutdown();
    return 0;
}
