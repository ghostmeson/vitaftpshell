#include "fake_net.h"

#include <string.h>

#define FAKE_NET_RX_MAX 8192
#define FAKE_NET_TX_MAX 8192
#define FAKE_NET_ACCEPT_MAX 4

struct NetSocket {
    int used;
    int open;
    int listening;
    int peer_closed;
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
static NetSocket *s_last_connected;
static NetSocket *s_last_listener;
static NetLink s_link;

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
    NetResult failure = take_failure();
    if (failure != NET_OK)
        return failure;
    if (s_resolve_fail)
        return NET_RESOLVE_FAILED;
    out->addr = s_resolve_addr;
    out->port = 21;
    return NET_OK;
}

static NetResult fake_connect(void *ctx, const NetAddress *addr,
                              unsigned timeout_ms, NetSocket **out)
{
    (void)ctx;
    (void)timeout_ms;
    NetResult failure = take_failure();
    if (failure != NET_OK)
        return failure;

    NetSocket *socket = alloc_socket();
    if (socket == NULL)
        return NET_BUSY;
    socket->peer = *addr;
    s_last_connected = socket;
    *out = socket;
    return NET_OK;
}

static NetResult fake_send(void *ctx, NetSocket *socket, const void *data,
                           size_t len, size_t *sent)
{
    (void)ctx;
    if (socket == NULL || !socket->open)
        return NET_INVALID;
    if (socket->tx_len + len > FAKE_NET_TX_MAX)
        return NET_WOULD_BLOCK;
    memcpy(socket->tx + socket->tx_len, data, len);
    socket->tx_len += len;
    socket->ready |= NET_WAIT_WRITE;
    *sent = len;
    return NET_OK;
}

static NetResult fake_receive(void *ctx, NetSocket *socket, void *data,
                              size_t cap, size_t *got)
{
    (void)ctx;
    if (socket == NULL || !socket->open)
        return NET_INVALID;
    if (socket->rx_len == 0) {
        *got = 0;
        if (socket->peer_closed)
            return NET_CLOSED;
        return NET_WOULD_BLOCK;
    }

    size_t n = socket->rx_len < cap ? socket->rx_len : cap;
    memcpy(data, socket->rx, n);
    memmove(socket->rx, socket->rx + n, socket->rx_len - n);
    socket->rx_len -= n;
    if (socket->rx_len == 0)
        socket->ready &= ~NET_WAIT_READ;
    *got = n;
    return NET_OK;
}

static void fake_close(void *ctx, NetSocket *socket)
{
    (void)ctx;
    if (socket != NULL) {
        socket->open = 0;
        socket->listening = 0;
        socket->ready = 0;
        socket->rx_len = 0;
        socket->tx_len = 0;
        socket->used = 0;
    }
}

static NetResult fake_listen(void *ctx, const NetAddress *addr,
                             NetSocket **out)
{
    (void)ctx;
    NetResult failure = take_failure();
    if (failure != NET_OK)
        return failure;

    NetSocket *socket = alloc_socket();
    if (socket == NULL)
        return NET_BUSY;
    socket->listening = 1;
    socket->peer = *addr;
    socket->ready = 0; /* readable only once a peer is queued */
    s_last_listener = socket;
    *out = socket;
    return NET_OK;
}

static NetResult fake_accept(void *ctx, NetSocket *listener, NetSocket **out,
                             NetAddress *peer)
{
    (void)ctx;
    if (listener == NULL || !listener->listening)
        return NET_INVALID;
    if (listener->pending_count == 0)
        return NET_WOULD_BLOCK;

    NetSocket *socket = alloc_socket();
    if (socket == NULL)
        return NET_BUSY;
    socket->peer = listener->pending[0];
    listener->pending_count--;
    memmove(&listener->pending[0], &listener->pending[1],
            listener->pending_count * sizeof(listener->pending[0]));
    if (listener->pending_count == 0)
        listener->ready &= ~NET_WAIT_READ;

    if (peer != NULL)
        *peer = socket->peer;
    *out = socket;
    return NET_OK;
}

static NetResult fake_wait(void *ctx, const NetWaitSet *set, unsigned timeout_ms,
                           NetReadySet *ready)
{
    (void)ctx;
    (void)timeout_ms; /* a timeout is an empty set, not an error */

    ready->count = 0;
    for (unsigned i = 0; i < set->count && i < NET_WAIT_MAX; ++i) {
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
    return NET_OK;
}

static NetResult fake_link(void *ctx, NetLink *out)
{
    (void)ctx;
    *out = s_link;
    return NET_OK;
}

void fake_net_reset(void)
{
    memset(s_sockets, 0, sizeof(s_sockets));
    memset(&s_port, 0, sizeof(s_port));
    s_peer.addr = 0xC0A8012Au; /* 192.168.1.42 */
    s_peer.port = 21;
    s_resolve_addr = 0xC0A8012Au;
    s_resolve_fail = 0;
    s_fail_next = NET_OK;
    s_last_connected = NULL;
    s_last_listener = NULL;

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
}

NetPort *fake_net_port(void)
{
    return &s_port;
}

void fake_net_set_peer(uint32_t addr, uint16_t port)
{
    s_peer.addr = addr;
    s_peer.port = port;
}

void fake_net_set_resolve(uint32_t addr, int fail)
{
    s_resolve_addr = addr;
    s_resolve_fail = fail;
}

void fake_net_set_link(int up, int known, uint32_t addr, uint16_t port)
{
    s_link.up = up;
    s_link.address_known = known;
    s_link.address.addr = addr;
    s_link.address.port = port;
}

void fake_net_fail_next(NetResult result)
{
    s_fail_next = result;
}

NetSocket *fake_net_last_connected(void)
{
    return s_last_connected;
}

NetSocket *fake_net_last_listener(void)
{
    return s_last_listener;
}

void fake_net_queue_rx(NetSocket *socket, const void *data, size_t len)
{
    if (socket == NULL || len > FAKE_NET_RX_MAX - socket->rx_len)
        return;
    memcpy(socket->rx + socket->rx_len, data, len);
    socket->rx_len += len;
    socket->ready |= NET_WAIT_READ;
}

void fake_net_shutdown(NetSocket *socket)
{
    if (socket == NULL)
        return;
    socket->peer_closed = 1;
    socket->ready |= NET_WAIT_READ; /* wake the reader for the EOF */
}

size_t fake_net_tx_len(NetSocket *socket)
{
    return socket != NULL ? socket->tx_len : 0;
}

size_t fake_net_read_tx(NetSocket *socket, void *buf, size_t cap)
{
    if (socket == NULL)
        return 0;
    size_t n = socket->tx_len < cap ? socket->tx_len : cap;
    memcpy(buf, socket->tx, n);
    memmove(socket->tx, socket->tx + n, socket->tx_len - n);
    socket->tx_len -= n;
    return n;
}

void fake_net_set_ready(NetSocket *socket, unsigned flags)
{
    if (socket != NULL)
        socket->ready = flags;
}

void fake_net_queue_accept(NetSocket *listener, uint32_t addr, uint16_t port)
{
    if (listener == NULL || listener->pending_count == FAKE_NET_ACCEPT_MAX)
        return;
    listener->pending[listener->pending_count].addr = addr;
    listener->pending[listener->pending_count].port = port;
    listener->pending_count++;
    listener->ready |= NET_WAIT_READ;
}
