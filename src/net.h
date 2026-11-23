// Single TCP client connection over lwIP raw API (NO_SYS / poll mode)
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NET_OK = 0,
    NET_ERR_DNS = -1,
    NET_ERR_CONNECT = -2,
    NET_ERR_TIMEOUT = -3,
    NET_ERR_CLOSED = -4,
    NET_ERR_ABORTED = -5,   // idle callback requested abort
    NET_ERR_MEM = -6,
};

// Called while waiting for data (and periodically during long reads).
// Return false to abort the current operation.
typedef bool (*net_idle_fn)(void);

int    net_connect(const char *host, uint16_t port, net_idle_fn idle);
void   net_close(void);
void   net_set_idle(net_idle_fn idle);
size_t net_available(void);
bool   net_is_open(void);
int    net_read(void *buf, size_t n);
int    net_skip(size_t n);
int    net_write(const void *buf, size_t n);
const char *net_strerror(int err);
