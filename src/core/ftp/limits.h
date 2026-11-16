/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared bounds for the FTP server's ports.
 *
 * The server gives each of its session slots one passive data port directly
 * above the control port, so the highest startable control port is the one
 * whose last slot port still fits a uint16_t.  One definition, because the
 * server, the settings loader, and the port editor must agree; when they drift
 * a user can save a port the server then refuses.
 */
#ifndef CORE_FTP_LIMITS_H
#define CORE_FTP_LIMITS_H

#include <stdint.h>

#define FTP_SERVER_MAX_CLIENTS 4

#define FTP_SERVER_PORT_MIN 1024
#define FTP_SERVER_PORT_MAX ((uint16_t)(65535 - FTP_SERVER_MAX_CLIENTS))

#endif
