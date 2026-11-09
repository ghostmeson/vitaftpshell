/* SPDX-License-Identifier: GPL-3.0-only */

#include "platform/psp2_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

/*
 * The one adapter that owns every socket. It uses the SDK's BSD socket
 * wrappers, which route to SceNet and keep the non-blocking state in the
 * socket option layer; the core sees only NetSocket handles and NetResult.
 */

#define PSP2_NET_MEMORY_BYTES (1024u * 1024u)
#define PSP2_NET_SOCKETS 16
#define PSP2_NET_LISTEN_BACKLOG 4
#define PSP2_NET_RESOLVE_RETRIES 1
#define PSP2_NET_RESOLVE_NAME "vftpshell"
/* sceNetCtlInit reports this when net control is already up. The value is not
 * exported by this SDK's netctl.h, so it is named here. */
#define PSP2_NET_CTL_ALREADY_INITIALIZED ((int)0x80412102)

struct NetSocket {
    int used;
    int fd;
};

/* SceNetInit wants a caller-owned work area; static keeps it off the heap and
 * alive for the process lifetime, which the stack outlives anyway. */
static unsigned char s_net_memory[PSP2_NET_MEMORY_BYTES]
    __attribute__((aligned(16)));
static struct NetSocket s_sockets[PSP2_NET_SOCKETS];
static int s_net_state; /* 0 = untried, 1 = ready, -1 = failed */
static int s_ctl_ready;
static NetPort s_port;

static int net_ensure_ready(void)
{
    if (s_net_state != 0)
        return s_net_state > 0;

    s_net_state = -1;

    if (sceSysmoduleIsLoaded(SCE_SYSMODULE_NET) < 0 &&
        sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0)
        return 0;

    SceNetInitParam param;
    memset(&param, 0, sizeof(param));
    param.memory = s_net_memory;
    param.size = (int)PSP2_NET_MEMORY_BYTES;
    param.flags = 0;

    int rc = sceNetInit(&param);
    /* The SDK libc starts the stack itself on the first BSD socket call. A
     * second init is the expected EBUSY, not a failure. */
    if (rc < 0 && rc != (int)SCE_NET_ERROR_EBUSY)
        return 0;

    /* [C] Net control may already have been initialized by the SDK libc; the
     * documented success value has not been checked on hardware. A failure here
     * only disables link reporting, not TCP. */
    rc = sceNetCtlInit();
    s_ctl_ready = rc >= 0 || rc == PSP2_NET_CTL_ALREADY_INITIALIZED;

    s_net_state = 1;
    return 1;
}

static NetSocket *socket_alloc(void)
{
    for (int i = 0; i < PSP2_NET_SOCKETS; ++i) {
        if (!s_sockets[i].used) {
            s_sockets[i].used = 1;
            s_sockets[i].fd = -1;
            return &s_sockets[i];
        }
    }
    return NULL;
}

static void socket_release(NetSocket *socket)
{
    if (socket != NULL) {
        socket->used = 0;
        socket->fd = -1;
    }
}

static int socket_set_nonblocking(int fd)
{
    /* [C] The SDK fcntl shim maps O_NONBLOCK onto SCE_NET_SO_NBIO; that mapping
     * and the resulting send/recv behavior are unverified on hardware. Every
     * socket in this adapter goes through here. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static NetResult result_from_errno(int error)
{
    switch (error) {
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
        return NET_WOULD_BLOCK;
    case ECONNRESET:
    case EPIPE:
        return NET_CLOSED;
    case ETIMEDOUT:
        return NET_TIMEOUT;
    case ECONNREFUSED:
        return NET_REFUSED;
    case ENETUNREACH:
    case EHOSTUNREACH:
        return NET_UNREACHABLE;
    case EBADF:
    case EINVAL:
        return NET_INVALID;
    default:
        return NET_ERROR;
    }
}

static void address_to_sockaddr(const NetAddress *address,
                                struct sockaddr_in *out)
{
    memset(out, 0, sizeof(*out));
    out->sin_len = (uint8_t)sizeof(*out);
    out->sin_family = AF_INET;
    out->sin_port = htons(address->port);
    out->sin_addr.s_addr = htonl(address->addr);
}

static int connect_in_progress(int error)
{
    return error == EINPROGRESS || error == EALREADY || error == EAGAIN
#if EWOULDBLOCK != EAGAIN
           || error == EWOULDBLOCK
#endif
        ;
}

/* 1 writable, 0 timed out, -1 failed. A stray EINTR restarts the wait; this
 * app installs no signal handlers, so that path is a guard only. */
static int wait_writable(int fd, unsigned timeout_ms)
{
    if (fd < 0 || fd >= FD_SETSIZE)
        return -1;

    for (;;) {
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(fd, &write_set);

        struct timeval tv;
        tv.tv_sec = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);

        /* [C] select over sceNet sockets for a non-blocking connect, and later
         * for the mixed control/data/listener set, is unverified on hardware.
         * The SDK implements select with sceNetEpoll; sceNetEpollWait is the
         * fallback if a hardware run shows this path unreliable. */
        int rc = select(fd + 1, NULL, &write_set, NULL, &tv);
        if (rc >= 0)
            return rc;
        if (errno != EINTR)
            return -1;
    }
}

static NetResult net_resolve(void *ctx, const char *host, unsigned timeout_ms,
                             NetAddress *out)
{
    (void)ctx;
    if (host == NULL || out == NULL)
        return NET_INVALID;
    if (!net_ensure_ready())
        return NET_ERROR;

    int rid = sceNetResolverCreate(PSP2_NET_RESOLVE_NAME, NULL, 0);
    if (rid < 0)
        return NET_RESOLVE_FAILED;

    unsigned seconds = timeout_ms / 1000u + (timeout_ms % 1000u != 0 ? 1u : 0u);
    if (seconds < 1)
        seconds = 1;

    SceNetInAddr address;
    memset(&address, 0, sizeof(address));

    /* [C] The resolver timeout is documented in seconds and the retry count is
     * unverified on hardware; one retry is used so a lost UDP packet does not
     * consume the caller's whole budget. */
    int rc = sceNetResolverStartNtoa(rid, host, &address, (int)seconds,
                                     PSP2_NET_RESOLVE_RETRIES, 0);
    int destroyed = sceNetResolverDestroy(rid);
    if (rc < 0 || destroyed < 0)
        return NET_RESOLVE_FAILED;

    out->addr = ntohl(address.s_addr);
    out->port = 0; /* resolution supplies an address; the caller owns the port */
    return NET_OK;
}

static NetResult net_connect(void *ctx, const NetAddress *addr,
                             unsigned timeout_ms, NetSocket **out)
{
    (void)ctx;
    if (addr == NULL || out == NULL || addr->port == 0)
        return NET_INVALID;
    if (!net_ensure_ready())
        return NET_ERROR;

    NetSocket *handle = socket_alloc();
    if (handle == NULL)
        return NET_BUSY;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        socket_release(handle);
        return result_from_errno(errno);
    }
    handle->fd = fd;

    if (!socket_set_nonblocking(fd)) {
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return NET_ERROR;
    }

    struct sockaddr_in target;
    address_to_sockaddr(addr, &target);

    int rc = connect(fd, (struct sockaddr *)&target, sizeof(target));
    if (rc < 0 && !connect_in_progress(errno)) {
        int failure = result_from_errno(errno);
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return failure;
    }

    rc = wait_writable(fd, timeout_ms);
    if (rc == 0) {
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return NET_TIMEOUT;
    }
    if (rc < 0) {
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return NET_ERROR;
    }

    int so_error = 0;
    socklen_t so_len = (socklen_t)sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) < 0 ||
        so_len != (socklen_t)sizeof(so_error)) {
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return NET_ERROR;
    }
    if (so_error != 0) {
        NetResult failure = result_from_errno(so_error);
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return failure;
    }

    *out = handle;
    return NET_OK;
}

static NetResult net_send(void *ctx, NetSocket *socket, const void *data,
                          size_t len, size_t *sent)
{
    (void)ctx;
    if (socket == NULL || !socket->used || sent == NULL ||
        (data == NULL && len != 0))
        return NET_INVALID;

    *sent = 0;
    ssize_t wrote = send(socket->fd, data, len, 0);
    if (wrote < 0)
        return result_from_errno(errno);
    if (wrote == 0 && len != 0)
        return NET_CLOSED;

    *sent = (size_t)wrote;
    return NET_OK;
}

static NetResult net_receive(void *ctx, NetSocket *socket, void *data,
                             size_t cap, size_t *got)
{
    (void)ctx;
    if (socket == NULL || !socket->used || got == NULL ||
        (data == NULL && cap != 0))
        return NET_INVALID;

    *got = 0;
    if (cap == 0)
        return NET_OK;

    ssize_t read_bytes = recv(socket->fd, data, cap, 0);
    if (read_bytes < 0)
        return result_from_errno(errno);
    if (read_bytes == 0)
        return NET_CLOSED;

    *got = (size_t)read_bytes;
    return NET_OK;
}

static void net_close(void *ctx, NetSocket *socket)
{
    (void)ctx;
    if (socket == NULL || !socket->used)
        return;

    /* A close result has no caller to act on it: the slot must be released
     * either way, and a second close would be worse than a leaked descriptor. */
    int rc = close(socket->fd);
    (void)rc;
    socket_release(socket);
}

static NetResult net_listen(void *ctx, const NetAddress *addr, NetSocket **out)
{
    (void)ctx;
    if (addr == NULL || out == NULL || addr->port == 0)
        return NET_INVALID;
    if (!net_ensure_ready())
        return NET_ERROR;

    NetSocket *handle = socket_alloc();
    if (handle == NULL)
        return NET_BUSY;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        socket_release(handle);
        return result_from_errno(errno);
    }
    handle->fd = fd;

    if (!socket_set_nonblocking(fd)) {
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return NET_ERROR;
    }

    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return NET_ERROR;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_len = (uint8_t)sizeof(bind_addr);
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(addr->port);
    /* The wildcard keeps the listener alive across a link-address change; the
     * server advertises the LAN address it read from the link query. */
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0 ||
        listen(fd, PSP2_NET_LISTEN_BACKLOG) < 0) {
        int closed = close(fd);
        (void)closed;
        socket_release(handle);
        return NET_ERROR;
    }

    *out = handle;
    return NET_OK;
}

static NetResult net_accept(void *ctx, NetSocket *listener, NetSocket **out,
                            NetAddress *peer)
{
    (void)ctx;
    if (listener == NULL || !listener->used || out == NULL)
        return NET_INVALID;
    if (!net_ensure_ready())
        return NET_ERROR;

    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    socklen_t peer_len = (socklen_t)sizeof(peer_addr);

    int fd = accept(listener->fd, (struct sockaddr *)&peer_addr, &peer_len);
    if (fd < 0)
        return result_from_errno(errno);

    /* A short address would leave the peer unknown; reporting 0.0.0.0 would be
     * a fabricated address, so the accepted socket is refused instead. */
    if (peer_len < (socklen_t)sizeof(peer_addr)) {
        int closed = close(fd);
        (void)closed;
        return NET_ERROR;
    }

    NetSocket *socket = socket_alloc();
    if (socket == NULL) {
        int closed = close(fd);
        (void)closed;
        return NET_BUSY;
    }
    socket->fd = fd;

    if (!socket_set_nonblocking(fd)) {
        int closed = close(fd);
        (void)closed;
        socket_release(socket);
        return NET_ERROR;
    }

    if (peer != NULL) {
        peer->addr = ntohl(peer_addr.sin_addr.s_addr);
        peer->port = ntohs(peer_addr.sin_port);
    }

    *out = socket;
    return NET_OK;
}

static NetResult net_wait(void *ctx, const NetWaitSet *set, unsigned timeout_ms,
                          NetReadySet *ready)
{
    (void)ctx;
    if (set == NULL || ready == NULL)
        return NET_INVALID;
    if (!net_ensure_ready())
        return NET_ERROR;

    ready->count = 0;
    if (set->count == 0)
        return NET_OK;
    if (set->count > NET_WAIT_MAX)
        return NET_INVALID;

    fd_set read_set;
    fd_set write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);

    unsigned count = set->count;
    int max_fd = -1;

    for (unsigned i = 0; i < count; ++i) {
        NetSocket *socket = set->items[i].socket;
        if (socket == NULL || !socket->used || socket->fd < 0 ||
            socket->fd >= FD_SETSIZE)
            return NET_INVALID;
        if ((set->items[i].flags & NET_WAIT_READ) != 0)
            FD_SET(socket->fd, &read_set);
        if ((set->items[i].flags & NET_WAIT_WRITE) != 0)
            FD_SET(socket->fd, &write_set);
        if (socket->fd > max_fd)
            max_fd = socket->fd;
    }

    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);

    /* [C] The same select claim as wait_writable: a mixed readiness set over
     * sceNet sockets is unverified on hardware. A timeout is an empty set, not
     * an error, which is what the engine expects. */
    int rc = select(max_fd + 1, &read_set, &write_set, NULL, &tv);
    if (rc < 0)
        return result_from_errno(errno);
    if (rc == 0)
        return NET_OK;

    for (unsigned i = 0; i < count; ++i) {
        NetSocket *socket = set->items[i].socket;
        unsigned matched = 0;

        if ((set->items[i].flags & NET_WAIT_READ) != 0 &&
            FD_ISSET(socket->fd, &read_set))
            matched |= NET_WAIT_READ;
        if ((set->items[i].flags & NET_WAIT_WRITE) != 0 &&
            FD_ISSET(socket->fd, &write_set))
            matched |= NET_WAIT_WRITE;

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

static NetResult net_link(void *ctx, NetLink *out)
{
    (void)ctx;
    if (out == NULL)
        return NET_INVALID;

    memset(out, 0, sizeof(*out));
    if (!net_ensure_ready() || !s_ctl_ready)
        return NET_ERROR;

    int state = 0;
    if (sceNetCtlInetGetState(&state) < 0)
        return NET_ERROR;

    if (state != SCE_NETCTL_STATE_CONNECTED)
        return NET_OK; /* down: up = 0 and the address stays unknown */

    out->up = 1;

    SceNetCtlInfo info;
    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0)
        return NET_OK; /* connected but the address cannot be reported */

    struct in_addr parsed;
    memset(&parsed, 0, sizeof(parsed));

    /* [C] The GetInfo IP address is assumed to be a NUL-terminated dotted quad
     * in SceNetCtlInfo.ip_address; that format is unverified on hardware. */
    if (inet_pton(AF_INET, info.ip_address, &parsed) != 1)
        return NET_OK;

    out->address_known = 1;
    out->address.addr = parsed.s_addr;
    out->address.port = 0; /* the link has no peer */
    return NET_OK;
}

NetPort *psp2_net_port(void)
{
    s_port.ctx = NULL;
    s_port.resolve = net_resolve;
    s_port.connect = net_connect;
    s_port.send = net_send;
    s_port.receive = net_receive;
    s_port.close = net_close;
    s_port.listen = net_listen;
    s_port.accept = net_accept;
    s_port.wait = net_wait;
    s_port.link = net_link;
    return &s_port;
}
