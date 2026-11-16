/* SPDX-License-Identifier: GPL-3.0-only */

/*
 * The product starts the worker on a pthread; the integration suite drives
 * worker_step inline. This binary is the only host test that runs the real
 * thread, so it is separate: the fakes are process-wide singletons.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app/app.h"
#include "core/handoff.h"
#include "fake_clock.h"
#include "fake_fs.h"
#include "fake_ime.h"
#include "fake_input.h"
#include "fake_log.h"
#include "fake_net.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void nap(void)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000;
    nanosleep(&ts, NULL);
}

int main(void)
{
    fake_net_reset();
    fake_fs_reset();
    fake_clock_reset();
    fake_input_reset();
    fake_ime_reset();
    fake_log_reset();

    fake_fs_add_dir("ux0:");
    fake_fs_add_dir("ux0:data");
    fake_fs_seed_file("ux0:data/A.TXT", "A", 1);
    fake_net_set_link(1, 1, 0xC0A81DC0u, 0);

    AppPorts ports;
    ports.net = fake_net_port();
    ports.fs = fake_fs_port();
    ports.input = fake_input_port();
    ports.ime = fake_ime_port();
    ports.clock = fake_clock_port();
    ports.log = fake_log_port();

    App *app = app_create(&ports, 1);
    CHECK(app != NULL);
    if (app == NULL)
        return 1;
    CHECK(app_start(app) == 1);

    int listed = 0;
    for (int i = 0; i < 2000 && !listed; ++i) {
        (void)app_tick(app);
        nap();
        if (app_state(app)->panes[UI_PANE_LOCAL].row_count >= 1)
            listed = 1;
    }
    CHECK(listed);

    UiCommand quit;
    memset(&quit, 0, sizeof(quit));
    quit.type = UI_CMD_QUIT;
    CHECK(handoff_command_push(app_handoff(app), &quit) == 1);

    int running = 1;
    for (int i = 0; i < 2000 && running; ++i) {
        running = app_tick(app);
        nap();
    }
    CHECK(running == 0);

    app_destroy(app);

    if (s_failures != 0) {
        fprintf(stderr, "test_app_thread: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_app_thread: OK\n");
    return 0;
}
