#include "net.h"
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

#define CONNECT_TIMEOUT_MS 10000
#define READ_TIMEOUT_MS    30000
#define WRITE_TIMEOUT_MS   10000
#define IDLE_INTERVAL_US   15000

static struct tcp_pcb *pcb;
static struct pbuf *rxq;
static volatile bool connected, closed;
static bool dns_done, dns_ok;
static uintptr_t dns_gen;  // ignores callbacks from abandoned lookups
static ip_addr_t dns_addr;
static net_idle_fn idle_cb;
static uint64_t last_idle_us, last_poll_us;

static err_t on_recv(void *arg, struct tcp_pcb *t, struct pbuf *p, err_t err) {
    (void)arg; (void)t; (void)err;
    if (!p) {
        closed = true;
        return ERR_OK;
    }
    if (rxq) pbuf_cat(rxq, p);
    else rxq = p;
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)arg; (void)err;
    pcb = NULL; // already freed by lwIP
    closed = true;
}

static err_t on_connected(void *arg, struct tcp_pcb *t, err_t err) {
    (void)arg; (void)t;
    if (err == ERR_OK) connected = true;
    else closed = true;
    return ERR_OK;
}

static void on_dns(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name;
    if ((uintptr_t)arg != dns_gen) return;
    dns_done = true;
    if (addr) {
        dns_ok = true;
        dns_addr = *addr;
    }
}

void net_set_idle(net_idle_fn idle) { idle_cb = idle; }

// Service the network and give the UI a chance to run. Returns false to abort.
static bool tick(bool force_idle) {
    uint64_t now = time_us_64();
    if (now - last_poll_us > 1000 || force_idle) {
        cyw43_arch_poll();
        last_poll_us = now;
    }
    if (idle_cb && (force_idle || now - last_idle_us > IDLE_INTERVAL_US)) {
        last_idle_us = now;
        if (!idle_cb()) return false;
    }
    return true;
}

static void free_rx(void) {
    if (rxq) pbuf_free(rxq);
    rxq = NULL;
}

void net_close(void) {
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
        pcb = NULL;
    }
    free_rx();
    connected = false;
    closed = true;
}

static void abort_pcb(void) {
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_abort(pcb);
        pcb = NULL;
    }
}

int net_connect(const char *host, uint16_t port, net_idle_fn idle) {
    net_close();
    idle_cb = idle;
    ip_addr_t addr;
    if (!ipaddr_aton(host, &addr)) {
        dns_done = dns_ok = false;
        err_t e = dns_gethostbyname(host, &addr, on_dns, (void *)++dns_gen);
        if (e == ERR_INPROGRESS) {
            absolute_time_t to = make_timeout_time_ms(CONNECT_TIMEOUT_MS);
            while (!dns_done) {
                if (!tick(true)) return NET_ERR_ABORTED;
                if (time_reached(to)) return NET_ERR_DNS;
                sleep_ms(2);
            }
            if (!dns_ok) return NET_ERR_DNS;
            addr = dns_addr;
        } else if (e != ERR_OK) {
            return NET_ERR_DNS;
        }
    }

    pcb = tcp_new_ip_type(IP_GET_TYPE(&addr));
    if (!pcb) return NET_ERR_MEM;
    connected = closed = false;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    tcp_nagle_disable(pcb);
    if (tcp_connect(pcb, &addr, port, on_connected) != ERR_OK) {
        abort_pcb();
        return NET_ERR_CONNECT;
    }
    absolute_time_t to = make_timeout_time_ms(CONNECT_TIMEOUT_MS);
    while (!connected) {
        if (closed) {
            abort_pcb();
            return NET_ERR_CONNECT;
        }
        if (!tick(true)) {
            abort_pcb();
            return NET_ERR_ABORTED;
        }
        if (time_reached(to)) {
            abort_pcb();
            return NET_ERR_TIMEOUT;
        }
        sleep_ms(2);
    }
    pcb->so_options |= SOF_KEEPALIVE;
    return NET_OK;
}

bool net_is_open(void) { return pcb && !closed; }

size_t net_available(void) { return rxq ? rxq->tot_len : 0; }

static void consume(size_t k) {
    rxq = pbuf_free_header(rxq, (u16_t)k);
    if (pcb) tcp_recved(pcb, (u16_t)k);
}

static int read_or_skip(uint8_t *dst, size_t n) {
    uint64_t deadline = time_us_64() + READ_TIMEOUT_MS * 1000ull;
    while (n) {
        if (rxq) {
            size_t take = rxq->tot_len;
            if (take > n) take = n;
            if (dst) {
                pbuf_copy_partial(rxq, dst, (u16_t)take, 0);
                dst += take;
            }
            consume(take);
            n -= take;
            deadline = time_us_64() + READ_TIMEOUT_MS * 1000ull;
            if (!tick(false)) return NET_ERR_ABORTED;
            continue;
        }
        if (closed || !pcb) return NET_ERR_CLOSED;
        if (!tick(false)) return NET_ERR_ABORTED;
        if (time_us_64() > deadline) return NET_ERR_TIMEOUT;
    }
    return NET_OK;
}

int net_read(void *buf, size_t n) { return read_or_skip(buf, n); }
int net_skip(size_t n) { return read_or_skip(NULL, n); }

int net_write(const void *buf, size_t n) {
    const uint8_t *p = buf;
    absolute_time_t to = make_timeout_time_ms(WRITE_TIMEOUT_MS);
    while (n) {
        if (!pcb || closed) return NET_ERR_CLOSED;
        size_t space = tcp_sndbuf(pcb);
        if (space == 0 || tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN - 2) {
            cyw43_arch_poll();
            if (time_reached(to)) return NET_ERR_TIMEOUT;
            continue;
        }
        size_t chunk = n < space ? n : space;
        err_t e = tcp_write(pcb, p, (u16_t)chunk, TCP_WRITE_FLAG_COPY);
        if (e == ERR_MEM) {
            tcp_output(pcb);
            cyw43_arch_poll();
            if (time_reached(to)) return NET_ERR_TIMEOUT;
            continue;
        }
        if (e != ERR_OK) return NET_ERR_CLOSED;
        p += chunk;
        n -= chunk;
    }
    if (pcb) tcp_output(pcb);
    return NET_OK;
}

const char *net_strerror(int err) {
    switch (err) {
    case NET_OK: return "OK";
    case NET_ERR_DNS: return "Could not resolve host name";
    case NET_ERR_CONNECT: return "Connection refused or unreachable";
    case NET_ERR_TIMEOUT: return "Network timeout";
    case NET_ERR_CLOSED: return "Connection closed by server";
    case NET_ERR_ABORTED: return "Cancelled";
    case NET_ERR_MEM: return "Out of memory";
    default: return "Network error";
    }
}
