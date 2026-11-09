#include "core/settings.h"

#include <string.h>

void settings_init(Settings *settings)
{
    if (settings == NULL)
        return;
    memset(settings, 0, sizeof(*settings));
    strncpy(settings->last_local, "ux0:data",
            sizeof(settings->last_local) - 1);
    settings->server_port = 1337;
}
