/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef VFTP_FAKE_NET_H
#define VFTP_FAKE_NET_H

#include <stddef.h>
#include <stdint.h>

#include "platform/net.h"

#define FAKE_NET_SOCKETS 16
#define FAKE_NET_LISTENERS 8
#define FAKE_NET_ACCEPTS 32

/*
 * Scripted NetPort for host tests. One process, one singleton.
 *
 * A test seeds the control peer and link state, then queues bytes on the socket
 * the core opened. Every call is non-blocking, exactly like the adapter.
 */

void fake_net_reset(void);
NetPort *fake_net_port(void);

void fake_net_set_peer(uint32_t addr, uint16_t port);
void fake_net_set_resolve(uint32_t addr, int fail);
void fake_net_set_connect_result(NetResult result);
void fake_net_fail_send_next(NetResult result);
unsigned fake_net_overflow_count(void);
void fake_net_set_link(int up, int known, uint32_t addr, uint16_t port);
void fake_net_fail_next(NetResult result);

NetSocket *fake_net_last_connected(void);
NetSocket *fake_net_last_listener(void);
/* Listen/accept order, for a server test that binds two ports. */
unsigned fake_net_listener_count(void);
NetSocket *fake_net_listener_at(unsigned index);
NetAddress fake_net_listener_addr(unsigned index);
unsigned fake_net_accept_count(void);
NetSocket *fake_net_accepted_at(unsigned index);

void fake_net_queue_rx(NetSocket *socket, const void *data, size_t len);
/* Mark the peer as gone; the next empty receive returns NET_CLOSED. */
void fake_net_shutdown(NetSocket *socket);
/* Mark an abrupt reset; the next empty receive returns NET_ERROR. */
void fake_net_reset_peer(NetSocket *socket);
size_t fake_net_tx_len(NetSocket *socket);
size_t fake_net_read_tx(NetSocket *socket, void *buf, size_t cap);
int fake_net_is_open(NetSocket *socket);
void fake_net_set_ready(NetSocket *socket, unsigned flags);

/* Queue one peer for the listener; the next accept returns a new socket. */
void fake_net_queue_accept(NetSocket *listener, uint32_t addr, uint16_t port);

#endif
