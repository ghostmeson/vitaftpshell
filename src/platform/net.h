#ifndef VFTP_PLATFORM_NET_H
#define VFTP_PLATFORM_NET_H

#include <stddef.h>
#include <stdint.h>

/*
 * Network contract. The core sees IPv4 addresses, opaque socket handles, and
 * plain result codes; no SceNet type or socket number crosses this boundary.
 *
 * Every socket is non-blocking. `connect` performs the bounded connect wait for
 * the worker (the caller is the worker thread, never the UI), and `wait` is the
 * one aggregate readiness call. No file outside src/platform/psp2_net.c names
 * `select`.
 */

#define NET_WAIT_MAX 8

typedef enum {
    NET_OK = 0,
    NET_WOULD_BLOCK,
    NET_TIMEOUT,
    NET_CLOSED,
    NET_REFUSED,
    NET_UNREACHABLE,
    NET_RESOLVE_FAILED,
    NET_INVALID,
    NET_BUSY,
    NET_ERROR
} NetResult;

/* IPv4 address in network byte order, port in host byte order. */
typedef struct {
    uint32_t addr;
    uint16_t port;
} NetAddress;

typedef struct NetSocket NetSocket;

typedef enum {
    NET_WAIT_READ = 1u,
    NET_WAIT_WRITE = 2u
} NetWaitFlag;

typedef struct {
    NetSocket *socket;
    unsigned flags;
} NetWaitItem;

typedef struct {
    NetWaitItem items[NET_WAIT_MAX];
    unsigned count;
} NetWaitSet;

typedef struct {
    NetWaitItem items[NET_WAIT_MAX];
    unsigned count;
} NetReadySet;

/* `address_known` is explicit: an unknown link address is not 0.0.0.0. */
typedef struct {
    int up;
    int address_known;
    NetAddress address;
} NetLink;

typedef struct NetPort NetPort;

struct NetPort {
    void *ctx;

    NetResult (*resolve)(void *ctx, const char *host, unsigned timeout_ms,
                         NetAddress *out);
    NetResult (*connect)(void *ctx, const NetAddress *addr, unsigned timeout_ms,
                         NetSocket **out);
    NetResult (*send)(void *ctx, NetSocket *socket, const void *data, size_t len,
                      size_t *sent);
    NetResult (*receive)(void *ctx, NetSocket *socket, void *data, size_t cap,
                         size_t *got);
    void (*close)(void *ctx, NetSocket *socket);
    NetResult (*listen)(void *ctx, const NetAddress *addr, NetSocket **out);
    NetResult (*accept)(void *ctx, NetSocket *listener, NetSocket **out,
                        NetAddress *peer);
    NetResult (*wait)(void *ctx, const NetWaitSet *set, unsigned timeout_ms,
                      NetReadySet *ready);
    NetResult (*link)(void *ctx, NetLink *out);
};

#endif
