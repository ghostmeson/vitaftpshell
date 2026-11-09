#ifndef VFTP_CORE_SETTINGS_H
#define VFTP_CORE_SETTINGS_H

#include <stdint.h>

#include "core/path.h"
#include "core/site.h"

#define SETTINGS_LAST_LOCAL_MAX PATH_MAX_LEN
#define SETTINGS_LAST_SITE_MAX SITE_NAME_MAX

typedef struct {
    char last_local[SETTINGS_LAST_LOCAL_MAX];
    char last_site[SETTINGS_LAST_SITE_MAX];
    uint16_t server_port;
} Settings;

void settings_init(Settings *settings);

#endif
