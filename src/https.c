#include "https.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "net.h"
#include "wifi.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "lwip/udp.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform.h"
#include "update_ca.h"

static mbedtls_ssl_context ssl;
static mbedtls_ssl_config conf;
static mbedtls_x509_crt ca;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context rng;
static https_idle_fn idle_cb;
static char *err;
static size_t errcap;
static bool initialized;
static uint64_t deadline, clock_us;
static time_t clock_epoch;
static uint8_t ntp_cookie[8];
static char host[64], path[2048], request[2304], line[8192], redirect[2048];
static uint8_t body[1024];

mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)(time_us_64() / 1000);
}

static bool error(const char *text) {
    if (!err[0]) snprintf(err, errcap, "%s", text);
    return false;
}

static bool poll_update(void) {
    sys_poll();
    if (!wifi_connected()) return error("WiFi connection lost.");
    if (idle_cb && !idle_cb()) return error("Cancelled.");
    if (time_us_64() > deadline) return error("HTTPS request timed out.");
    return true;
}

static time_t tls_time(time_t *out) {
    time_t now = clock_epoch + (time_us_64() - clock_us) / 1000000;
    if (out) *out = now;
    return now;
}

static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                     const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)addr;
    uint8_t b[48];
    if (port == 123 && p->tot_len >= sizeof(b)) {
        pbuf_copy_partial(p, b, sizeof(b), 0);
        if ((b[0] & 7) == 4 && (b[0] >> 6) != 3 && b[1] > 0 && b[1] < 16 &&
            !memcmp(b + 24, ntp_cookie, 8)) {
            uint32_t secs = (uint32_t)b[40] << 24 | (uint32_t)b[41] << 16 |
                            (uint32_t)b[42] << 8 | b[43];
            // Reject unsynchronised/implausible clocks (2025 through NTP era end).
            if (secs >= 3944678400u) {
                clock_epoch = (time_t)(secs - 2208988800u);
                clock_us = time_us_64();
            }
        }
    }
    pbuf_free(p);
}

static bool sync_clock(void) {
    if (clock_epoch) return true;
    struct udp_pcb *pcb = udp_new();
    if (!pcb) return error("Not enough memory for network time.");
    ip_addr_t server;
    ipaddr_aton("162.159.200.1", &server); // Cloudflare's public NTP service
    udp_recv(pcb, ntp_recv, NULL);
    err_t rc = udp_connect(pcb, &server, 123);
    for (int attempt = 0; rc == ERR_OK && attempt < 3 && !clock_epoch; ++attempt) {
        uint8_t b[48] = {0x23};
        uint64_t cookie = get_rand_64();
        memcpy(ntp_cookie, &cookie, 8);
        memcpy(b + 40, ntp_cookie, 8);
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(b), PBUF_RAM);
        if (!p) { rc = ERR_MEM; break; }
        pbuf_take(p, b, sizeof(b));
        rc = udp_send(pcb, p);
        pbuf_free(p);
        uint64_t until = time_us_64() + 3000000;
        while (rc == ERR_OK && !clock_epoch && time_us_64() < until) {
            if (!poll_update()) { udp_remove(pcb); return false; }
            sleep_ms(5);
        }
    }
    udp_remove(pcb);
    return clock_epoch != 0 || error("Cannot get network time (UDP port 123).");
}

static int send_tls(void *ctx, const unsigned char *buf, size_t n) {
    (void)ctx;
    if (!poll_update()) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int rc = net_write(buf, n);
    if (rc < 0) { error(net_strerror(rc)); return MBEDTLS_ERR_SSL_INTERNAL_ERROR; }
    return (int)n;
}

static int recv_tls(void *ctx, unsigned char *buf, size_t n) {
    (void)ctx;
    if (!poll_update()) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    size_t available = net_available();
    if (!available) return net_is_open() ? MBEDTLS_ERR_SSL_WANT_READ : 0;
    if (n > available) n = available;
    int rc = net_read(buf, n);
    if (rc < 0) { error(net_strerror(rc)); return MBEDTLS_ERR_SSL_INTERNAL_ERROR; }
    return (int)n;
}

bool https_init(https_idle_fn idle, char *error_buf, size_t cap) {
    err = error_buf; errcap = cap; err[0] = 0; idle_cb = idle;
    deadline = time_us_64() + 15000000;
    if (!sync_clock()) return false;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&ca);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
    initialized = true;
    mbedtls_platform_set_time(tls_time);
    int rc = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy,
                                  (const unsigned char *)"PicoCalc update", 15);
    if (!rc) rc = mbedtls_x509_crt_parse(&ca, (const unsigned char *)update_ca, sizeof(update_ca));
    if (!rc) rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                             MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) return error("Could not initialise verified TLS.");
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &rng);
    return true;
}

void https_free(void) {
    net_close();
    net_set_idle(NULL);
    if (initialized) {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&ca);
        mbedtls_ctr_drbg_free(&rng);
        mbedtls_entropy_free(&entropy);
        initialized = false;
    }
}

static bool allowed_url(const char *url) {
    if (strncmp(url, "https://", 8)) return error("Non-HTTPS update URL refused.");
    const char *slash = strchr(url + 8, '/');
    if (!slash || (size_t)(slash - url - 8) >= sizeof(host) || strlen(slash) >= sizeof(path))
        return error("Invalid or oversized update URL.");
    size_t n = (size_t)(slash - url - 8);
    memcpy(host, url + 8, n); host[n] = 0;
    if (strcmp(host, "github.com") && strcmp(host, "release-assets.githubusercontent.com") &&
        strcmp(host, "objects.githubusercontent.com"))
        return error("Unexpected update download host.");
    for (const char *p = slash; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127) return error("Invalid update URL.");
    memcpy(path, slash, strlen(slash) + 1);
    return true;
}

static int read_tls(uint8_t *buf, size_t n) {
    for (;;) {
        if (!poll_update()) return -1;
        int rc = mbedtls_ssl_read(&ssl, buf, n);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (rc < 0) { error("TLS read failed or download was truncated."); return -1; }
        return rc;
    }
}

static bool read_exact(uint8_t *buf, size_t n) {
    while (n) {
        int rc = read_tls(buf, n);
        if (rc <= 0) return error("Incomplete HTTPS response.");
        buf += rc; n -= rc;
    }
    return true;
}

static bool read_line(void) {
    for (size_t i = 0; i + 1 < sizeof(line); ++i) {
        if (!read_exact((uint8_t *)&line[i], 1)) return false;
        if (line[i] == '\n') {
            if (!i || line[i - 1] != '\r') return error("Malformed HTTP headers.");
            line[i - 1] = 0;
            return true;
        }
    }
    return error("HTTP header too long.");
}

static bool number(const char *s, unsigned base, size_t *value) {
    if (!*s) return false;
    size_t v = 0;
    for (; *s; ++s) {
        unsigned d = isdigit((unsigned char)*s) ? (unsigned)(*s - '0') :
                     (*s >= 'a' && *s <= 'f') ? (unsigned)(*s - 'a' + 10) :
                     (*s >= 'A' && *s <= 'F') ? (unsigned)(*s - 'A' + 10) : 99;
        if (d >= base || v > (SIZE_MAX - d) / base) return false;
        v = v * base + d;
    }
    *value = v; return true;
}

bool https_get(const char *url, bool head, size_t limit, https_sink_fn sink,
               void *arg, char *location, size_t location_cap) {
    if (location && location_cap) location[0] = 0;
    for (int redirects = 0; redirects < 5; ++redirects) {
        if (!allowed_url(url)) return false;
        deadline = time_us_64() + 180000000;
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_init(&ssl);
        int rc = net_connect(host, 443, poll_update);
        if (rc < 0) return error(net_strerror(rc));
        rc = mbedtls_ssl_setup(&ssl, &conf);
        if (!rc) rc = mbedtls_ssl_set_hostname(&ssl, host);
        if (rc) return error("TLS session setup failed.");
        mbedtls_ssl_set_bio(&ssl, NULL, send_tls, recv_tls, NULL);
        do {
            if (!poll_update()) return false;
            rc = mbedtls_ssl_handshake(&ssl);
        } while (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
        if (rc || mbedtls_ssl_get_verify_result(&ssl))
            return error("TLS certificate or handshake verification failed.");
        int len = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: PicoCalc-VNC\r\n"
            "Accept: application/octet-stream\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            head ? "HEAD" : "GET", path, host);
        if (len < 0 || (size_t)len >= sizeof(request)) return error("Update request too long.");
        size_t sent = 0;
        while (sent < (size_t)len) {
            if (!poll_update()) return false;
            rc = mbedtls_ssl_write(&ssl, (const unsigned char *)request + sent, len - sent);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (rc <= 0) return error("HTTPS request failed.");
            sent += rc;
        }
        if (!read_line()) return false;
        unsigned status;
        if (sscanf(line, "HTTP/1.1 %u", &status) != 1) return error("Invalid HTTP response.");
        bool chunked = false, have_length = false;
        size_t length = 0, headers = 0;
        redirect[0] = 0;
        for (;;) {
            if (!read_line()) return false;
            headers += strlen(line) + 2;
            if (headers > 32768) return error("Oversized HTTP headers.");
            if (!line[0]) break;
            char *colon = strchr(line, ':');
            if (!colon) return error("Malformed HTTP header.");
            *colon++ = 0;
            while (*colon == ' ' || *colon == '\t') ++colon;
            if (!strcasecmp(line, "Location")) {
                if (strlen(colon) >= sizeof(redirect)) return error("Redirect URL too long.");
                strcpy(redirect, colon);
            } else if (!strcasecmp(line, "Content-Length")) {
                size_t v;
                if (!number(colon, 10, &v) || (have_length && v != length))
                    return error("Invalid HTTP length.");
                length = v; have_length = true;
            } else if (!strcasecmp(line, "Transfer-Encoding")) {
                if (strcasecmp(colon, "chunked")) return error("Unsupported HTTP transfer encoding.");
                chunked = true;
            } else if (!strcasecmp(line, "Content-Encoding") && strcasecmp(colon, "identity")) {
                return error("Unexpected compressed update.");
            }
        }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (!redirect[0]) return error("Missing redirect URL.");
            if (head) {
                if (!location || strlen(redirect) >= location_cap) return error("Release URL too long.");
                strcpy(location, redirect); net_close(); return true;
            }
            // allowed_url copies the URL before the redirect buffer is reused.
            url = redirect;
            net_close();
            continue;
        }
        if (status != 200) {
            snprintf(line, sizeof(line), "GitHub returned HTTP %u%s.", status,
                     status == 404 ? " (no public release/asset found)" : "");
            return error(line);
        }
        if (head) return error("No latest release redirect found.");
        if (chunked && have_length) return error("Ambiguous HTTP response framing.");
        if (!chunked && !have_length) return error("Missing download length.");
        size_t total = 0;
        for (;;) {
            size_t remaining = length;
            if (chunked) {
                if (!read_line()) return false;
                char *extension = strchr(line, ';');
                if (extension) *extension = 0;
                if (!number(line, 16, &remaining)) return error("Invalid chunk length.");
            }
            if (remaining > limit - total) return error("Download exceeds permitted size.");
            if (!remaining) {
                if (chunked) {
                    size_t trailers = 0;
                    do {
                        if (!read_line()) return false;
                        trailers += strlen(line) + 2;
                        if (trailers > 8192) return error("Oversized HTTP trailers.");
                    } while (line[0]);
                }
                net_close();
                return true;
            }
            total += remaining;
            while (remaining) {
                size_t n = remaining < sizeof(body) ? remaining : sizeof(body);
                if (!read_exact(body, n)) return false;
                if (!sink(body, n, arg)) return error("Could not save update data.");
                remaining -= n;
            }
            if (!chunked) { net_close(); return true; }
            uint8_t crlf[2];
            if (!read_exact(crlf, 2)) return false;
            if (memcmp(crlf, "\r\n", 2)) return error("Invalid chunk terminator.");
        }
    }
    return error("Too many download redirects.");
}
