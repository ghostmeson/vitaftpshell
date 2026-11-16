/* SPDX-License-Identifier: GPL-3.0-only */

#include "fake_net.h"

#include <pthread.h>
#include <string.h>

#define FAKE_NET_RX_MAX 8192
#define FAKE_NET_TX_MAX 8192
#define FAKE_NET_ACCEPT_MAX 4

struct NetSocket {
    int used;
    int open;
    int listening;
    int peer_closed;
    int peer_reset;
    int block_sends;
    NetAddress peer;
    unsigned ready;
    unsigned char rx[FAKE_NET_RX_MAX];
    size_t rx_len;
    unsigned char tx[FAKE_NET_TX_MAX];
    size_t tx_len;
    NetAddress pending[FAKE_NET_ACCEPT_MAX];
    unsigned pending_count;
};

static struct NetSocket s_sockets[FAKE_NET_SOCKETS];
static NetPort s_port;
static NetAddress s_peer;
static uint32_t s_resolve_addr;
static int s_resolve_fail;
static NetResult s_fail_next;
static uint16_t s_fail_listen_port;
static int s_fail_listen;
static NetResult s_connect_result;
static NetResult s_send_result;
static unsigned s_overflow;
static unsigned s_last_wait_count;
static NetSocket *s_last_connected;
static NetSocket *s_last_listener;
static NetSocket *s_listeners[FAKE_NET_LISTENERS];
static unsigned s_listener_count;
static NetSocket *s_accepted[FAKE_NET_ACCEPTS];
static unsigned s_accept_count;
static NetLink s_link;

/*
 * One mutex for every socket and listener field. The app's worker and a host
 * test's scripted peer can run on different threads, and both reach the same
 * singleton through the fake's public accessors and NetPort callbacks.
 */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static void net_lock(void)
{
    (void)pthread_mutex_lock(&s_lock);
}

static void net_unlock(void)
{
    (void)pthread_mutex_unlock(&s_lock);
}

static NetSocket *alloc_socket(void)
{
    for (int i = 0; i < FAKE_NET_SOCKETS; ++i) {
        if (!s_sockets[i].used) {
            memset(&s_sockets[i], 0, sizeof(s_sockets[i]));
            s_sockets[i].used = 1;
            s_sockets[i].open = 1;
            s_sockets[i].ready = NET_WAIT_WRITE;
            return &s_sockets[i];
        }
    }
    return NULL;
}

static NetResult take_failure(void)
{
    NetResult r = s_fail_next;
    s_fail_next = NET_OK;
    return r;
}

static NetResult fake_resolve(void *ctx, const char *host, unsigned timeout_ms,
                              NetAddress *out)
{
    (void)ctx;
    (void)host;
    (void)timeout_ms;

    net_lock();
    NetResult result = take_failure();
    if (result == NET_OK && s_resolve_fail)
        result = NET_RESOLVE_FAILED;
    if (result == NET_OK) {
        out->addr = s_resolve_addr;
        out->port = 21;
    }
    net_unlock();
    return result;
}

static NetResult fake_connect(void *ctx, const NetAddress *addr,
                              unsigned timeout_ms, NetSocket **out)
{
    (void)ctx;
    (void)timeout_ms;

    net_lock();
    NetResult result = take_failure();
    if (result == NET_OK && s_connect_result != NET_OK) {
        result = s_connect_result;
        s_connect_result = NET_OK;
    }
    if (result == NET_OK) {
        NetSocket *socket = alloc_socket();
        if (socket == NULL) {
            result = NET_BUSY;
        } else {
            socket->peer = *addr;
            s_last_connected = socket;
            *out = socket;
        }
    }
    net_unlock();
    return result;
}

static NetResult fake_send(void *ctx, NetSocket *socket, const void *data,
                           size_t len, size_t *sent)
{
    (void)ctx;

    net_lock();
    NetResult result = NET_OK;
    if (socket == NULL || !socket->open) {
        result = NET_INVALID;
    } else if (socket->block_sends) {
        result = NET_WOULD_BLOCK;
    } else if (s_send_result != NET_OK) {
        result = s_send_result;
        s_send_result = NET_OK;
    } else if (socket->tx_len + len > FAKE_NET_TX_MAX) {
        result = NET_WOULD_BLOCK;
    } else {
        memcpy(socket->tx + socket->tx_len, data, len);
        socket->tx_len += len;
        socket->ready |= NET_WAIT_WRITE;
        *sent = len;
    }
    net_unlock();
    return result;
}

static NetResult fake_receive(void *ctx, NetSocket *socket, void *data,
                              size_t cap, size_t *got)
{
    (void)ctx;

    net_lock();
    NetResult result = NET_OK;
    if (socket == NULL || !socket->open) {
        result = NET_INVALID;
    } else if (socket->rx_len == 0) {
        *got = 0;
        if (socket->peer_reset)
            result = NET_ERROR;
        else if (socket->peer_closed)
            result = NET_CLOSED;
        else
            result = NET_WOULD_BLOCK;
    } else {
        size_t n = socket->rx_len < cap ? socket->rx_len : cap;
        memcpy(data, socket->rx, n);
        memmove(socket->rx, socket->rx + n, socket->rx_len - n);
        socket->rx_len -= n;
        if (socket->rx_len == 0 && !socket->peer_closed && !socket->peer_reset)
            socket->ready &= ~(unsigned)NET_WAIT_READ;
        *got = n;
    }
    net_unlock();
    return result;
}

static void fake_close(void *ctx, NetSocket *socket)
{
    (void)ctx;
    net_lock();
    if (socket != NULL) {
        /* Keep rx/tx for inspection; a reused slot is zeroed in alloc_socket.
         * Tests read a server's last data bytes after it closes the socket. */
        socket->open = 0;
        socket->listening = 0;
        socket->ready = 0;
        socket->used = 0;
    }
    net_unlock();
}

static NetResult fake_listen(void *ctx, const NetAddress *addr,
                             NetSocket **out)
{
    (void)ctx;

    net_lock();
    NetResult result = NET_OK;
    /* One named port can be made to fail while every other bind succeeds: the
     * control listener has to survive so the 425 path is the only failure. */
    if (s_fail_listen && addr != NULL && addr->port == s_fail_listen_port) {
        result = NET_ERROR;
    } else {
        result = take_failure();
        if (result == NET_OK) {
            NetSocket *socket = alloc_socket();
            if (socket == NULL) {
                result = NET_BUSY;
            } else {
                socket->listening = 1;
                socket->peer = *addr;
                socket->ready = 0; /* readable only once a peer is queued */
                s_last_listener = socket;
                if (s_listener_count < FAKE_NET_LISTENERS)
                    s_listeners[s_listener_count++] = socket;
                *out = socket;
            }
        }
    }
    net_unlock();
    return result;
}

static NetResult fake_accept(void *ctx, NetSocket *listener, NetSocket **out,
                             NetAddress *peer)
{
    (void)ctx;

    net_lock();
    NetResult result = NET_OK;
    if (listener == NULL || !listener->listening) {
        result = NET_INVALID;
    } else if (listener->pending_count == 0) {
        result = NET_WOULD_BLOCK;
    } else {
        NetSocket *socket = alloc_socket();
        if (socket == NULL) {
            result = NET_BUSY;
        } else {
            socket->peer = listener->pending[0];
            listener->pending_count--;
            memmove(&listener->pending[0], &listener->pending[1],
                    listener->pending_count * sizeof(listener->pending[0]));
            if (listener->pending_count == 0)
                listener->ready &= ~(unsigned)NET_WAIT_READ;

            if (peer != NULL)
                *peer = socket->peer;
            if (s_accept_count < FAKE_NET_ACCEPTS)
                s_accepted[s_accept_count++] = socket;
            *out = socket;
        }
    }
    net_unlock();
    return result;
}

static NetResult fake_wait(void *ctx, const NetWaitSet *set, unsigned timeout_ms,
                           NetReadySet *ready)
{
    (void)ctx;
    (void)timeout_ms; /* a timeout is an empty set, not an error */

    net_lock();
    NetResult result = NET_OK;
    if (set == NULL || ready == NULL) {
        result = NET_INVALID;
    } else {
        ready->count = 0;
        s_last_wait_count = set->count;
        /* The adapter refuses an over-cap set; truncating here would let a
         * server bug pass on the host and stop the server on hardware. */
        if (set->count > NET_WAIT_MAX) {
            result = NET_INVALID;
        } else {
            for (unsigned i = 0; i < set->count; ++i) {
                NetSocket *socket = set->items[i].socket;
                if (socket == NULL || !socket->open)
                    continue;
                unsigned matched = socket->ready & set->items[i].flags;
                if (matched == 0)
                    continue;
                if (ready->count == NET_WAIT_MAX)
                    break;
                ready->items[ready->count].socket = socket;
                ready->items[ready->count].flags = matched;
                ready->count++;
            }
        }
    }
    net_unlock();
    return result;
}

static NetResult fake_link(void *ctx, NetLink *out)
{
    (void)ctx;
    net_lock();
    *out = s_link;
    net_unlock();
    return NET_OK;
}

void fake_net_reset(void)
{
    net_lock();
    memset(s_sockets, 0, sizeof(s_sockets));
    memset(&s_port, 0, sizeof(s_port));
    s_peer.addr = 0xC0A8012Au; /* 192.168.1.42 */
    s_peer.port = 21;
    s_resolve_addr = 0xC0A8012Au;
    s_resolve_fail = 0;
    s_fail_next = NET_OK;
    s_fail_listen = 0;
    s_fail_listen_port = 0;
    s_connect_result = NET_OK;
    s_send_result = NET_OK;
    s_overflow = 0;
    s_last_wait_count = 0;
    s_last_connected = NULL;
    s_last_listener = NULL;
    s_listener_count = 0;
    s_accept_count = 0;
    memset(s_listeners, 0, sizeof(s_listeners));
    memset(s_accepted, 0, sizeof(s_accepted));

    s_link.up = 1;
    s_link.address_known = 1;
    s_link.address.addr = 0xC0A8012Au;
    s_link.address.port = 0;

    s_port.ctx = NULL;
    s_port.resolve = fake_resolve;
    s_port.connect = fake_connect;
    s_port.send = fake_send;
    s_port.receive = fake_receive;
    s_port.close = fake_close;
    s_port.listen = fake_listen;
    s_port.accept = fake_accept;
    s_port.wait = fake_wait;
    s_port.link = fake_link;
    net_unlock();
}

NetPort *fake_net_port(void)
{
    net_lock();
    NetPort *port = &s_port;
    net_unlock();
    return port;
}

void fake_net_set_peer(uint32_t addr, uint16_t port)
{
    net_lock();
    s_peer.addr = addr;
    s_peer.port = port;
    net_unlock();
}

void fake_net_set_resolve(uint32_t addr, int fail)
{
    net_lock();
    s_resolve_addr = addr;
    s_resolve_fail = fail;
    net_unlock();
}

void fake_net_set_connect_result(NetResult result)
{
    net_lock();
    s_connect_result = result;
    net_unlock();
}

void fake_net_fail_send_next(NetResult result)
{
    net_lock();
    s_send_result = result;
    net_unlock();
}

unsigned fake_net_overflow_count(void)
{
    net_lock();
    unsigned count = s_overflow;
    net_unlock();
    return count;
}

unsigned fake_net_last_wait_count(void)
{
    net_lock();
    unsigned count = s_last_wait_count;
    net_unlock();
    return count;
}

void fake_net_set_link(int up, int known, uint32_t addr, uint16_t port)
{
    net_lock();
    s_link.up = up;
    s_link.address_known = known;
    s_link.address.addr = addr;
    s_link.address.port = port;
    net_unlock();
}

void fake_net_fail_next(NetResult result)
{
    net_lock();
    s_fail_next = result;
    net_unlock();
}

void fake_net_fail_listen_port(uint16_t port, int fail)
{
    net_lock();
    s_fail_listen_port = port;
    s_fail_listen = fail;
    net_unlock();
}

NetSocket *fake_net_last_connected(void)
{
    net_lock();
    NetSocket *socket = s_last_connected;
    net_unlock();
    return socket;
}

void fake_net_socket_peer(NetSocket *socket, uint32_t *addr, uint16_t *port)
{
    net_lock();
    if (addr != NULL)
        *addr = socket != NULL ? socket->peer.addr : 0;
    if (port != NULL)
        *port = socket != NULL ? socket->peer.port : 0;
    net_unlock();
}

NetSocket *fake_net_last_listener(void)
{
    net_lock();
    NetSocket *socket = s_last_listener;
    net_unlock();
    return socket;
}

unsigned fake_net_listener_count(void)
{
    net_lock();
    unsigned count = s_listener_count;
    net_unlock();
    return count;
}

NetSocket *fake_net_listener_at(unsigned index)
{
    net_lock();
    NetSocket *socket = index < s_listener_count ? s_listeners[index] : NULL;
    net_unlock();
    return socket;
}

NetAddress fake_net_listener_addr(unsigned index)
{
    NetAddress empty = {0, 0};
    net_lock();
    NetAddress addr = index < s_listener_count ? s_listeners[index]->peer : empty;
    net_unlock();
    return addr;
}

unsigned fake_net_accept_count(void)
{
    net_lock();
    unsigned count = s_accept_count;
    net_unlock();
    return count;
}

NetSocket *fake_net_accepted_at(unsigned index)
{
    net_lock();
    NetSocket *socket = index < s_accept_count ? s_accepted[index] : NULL;
    net_unlock();
    return socket;
}

void fake_net_queue_rx(NetSocket *socket, const void *data, size_t len)
{
    net_lock();
    if (socket == NULL || len > FAKE_NET_RX_MAX - socket->rx_len) {
        s_overflow++;
    } else {
        memcpy(socket->rx + socket->rx_len, data, len);
        socket->rx_len += len;
        socket->ready |= NET_WAIT_READ;
    }
    net_unlock();
}

void fake_net_shutdown(NetSocket *socket)
{
    net_lock();
    if (socket != NULL) {
        socket->peer_closed = 1;
        socket->ready |= NET_WAIT_READ; /* wake the reader for the EOF */
    }
    net_unlock();
}

void fake_net_reset_peer(NetSocket *socket)
{
    net_lock();
    if (socket != NULL) {
        socket->peer_reset = 1;
        socket->ready |= NET_WAIT_READ; /* wake the reader for the error */
    }
    net_unlock();
}

size_t fake_net_tx_len(NetSocket *socket)
{
    net_lock();
    size_t len = socket != NULL ? socket->tx_len : 0;
    net_unlock();
    return len;
}

int fake_net_is_open(NetSocket *socket)
{
    net_lock();
    int open = socket != NULL && socket->open;
    net_unlock();
    return open;
}

size_t fake_net_read_tx(NetSocket *socket, void *buf, size_t cap)
{
    net_lock();
    size_t n = 0;
    if (socket != NULL) {
        n = socket->tx_len < cap ? socket->tx_len : cap;
        memcpy(buf, socket->tx, n);
        memmove(socket->tx, socket->tx + n, socket->tx_len - n);
        socket->tx_len -= n;
    }
    net_unlock();
    return n;
}

void fake_net_block_sends(NetSocket *socket, int blocked)
{
    net_lock();
    if (socket != NULL)
        socket->block_sends = blocked;
    net_unlock();
}

void fake_net_set_ready(NetSocket *socket, unsigned flags)
{
    net_lock();
    if (socket != NULL)
        socket->ready = flags;
    net_unlock();
}

void fake_net_queue_accept(NetSocket *listener, uint32_t addr, uint16_t port)
{
    net_lock();
    if (listener != NULL && listener->pending_count < FAKE_NET_ACCEPT_MAX) {
        listener->pending[listener->pending_count].addr = addr;
        listener->pending[listener->pending_count].port = port;
        listener->pending_count++;
        listener->ready |= NET_WAIT_READ;
    }
    net_unlock();
}
