#include "wifi.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

static bool inited;
static char cur_ssid[33];
static char ip_str[16];

static scan_ap_t *scan_out;
static int scan_max, scan_n;

bool wifi_init(void) {
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_WORLDWIDE)) return false;
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    inited = true;
    return true;
}

bool wifi_available(void) { return inited; }

void sys_poll(void) {
    if (inited) cyw43_arch_poll();
}

static int scan_cb(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    if (!r || r->ssid_len == 0 || r->ssid[0] == 0) return 0;
    char ssid[33];
    int len = r->ssid_len > 32 ? 32 : r->ssid_len;
    memcpy(ssid, r->ssid, len);
    ssid[len] = 0;
    for (int i = 0; i < scan_n; i++) {
        if (!strcmp(scan_out[i].ssid, ssid)) {
            if (r->rssi > scan_out[i].rssi) {
                scan_out[i].rssi = r->rssi;
                scan_out[i].channel = (uint8_t)r->channel;
            }
            scan_out[i].auth |= r->auth_mode;
            return 0;
        }
    }
    if (scan_n < scan_max) {
        scan_ap_t *a = &scan_out[scan_n++];
        memcpy(a->ssid, ssid, len + 1);
        a->rssi = r->rssi;
        a->auth = r->auth_mode;
        a->channel = (uint8_t)r->channel;
    }
    return 0;
}

int wifi_scan(scan_ap_t *out, int max, cancel_fn cancel) {
    if (!inited) return WIFI_ERR_INIT;
    scan_out = out;
    scan_max = max;
    scan_n = 0;
    cyw43_wifi_scan_options_t opts = {0};
    if (cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_cb) != 0) return WIFI_ERR_FAIL;
    absolute_time_t to = make_timeout_time_ms(15000);
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        cyw43_arch_poll();
        sleep_ms(5);
        if (cancel && cancel()) break;
        if (time_reached(to)) break;
    }
    // Sort strongest first
    for (int i = 1; i < scan_n; i++) {
        scan_ap_t t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    return scan_n;
}

static int try_connect(const char *ssid, const char *pass, uint32_t auth, cancel_fn cancel) {
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    for (int i = 0; i < 20; i++) {
        cyw43_arch_poll();
        sleep_ms(5);
    }
    if (cyw43_arch_wifi_connect_async(ssid, auth == CYW43_AUTH_OPEN ? NULL : pass, auth) != 0)
        return WIFI_ERR_FAIL;
    absolute_time_t to = make_timeout_time_ms(20000);
    for (;;) {
        cyw43_arch_poll();
        int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (st == CYW43_LINK_UP) return WIFI_OK;
        if (st == CYW43_LINK_BADAUTH) return WIFI_ERR_BADAUTH;
        if (st == CYW43_LINK_NONET) return WIFI_ERR_NONET;
        if (st == CYW43_LINK_FAIL) return WIFI_ERR_FAIL;
        if (cancel && cancel()) {
            cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            return WIFI_ERR_CANCELLED;
        }
        if (time_reached(to)) {
            cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            return WIFI_ERR_TIMEOUT;
        }
        sleep_ms(10);
    }
}

int wifi_connect(const char *ssid, const char *pass, uint32_t auth_hint, uint32_t *auth_used, cancel_fn cancel) {
    if (!inited) return WIFI_ERR_INIT;
    cur_ssid[0] = 0;
    uint32_t modes[3];
    int n = 0;
    if (!pass || !pass[0]) {
        modes[n++] = CYW43_AUTH_OPEN;
    } else {
        if (auth_hint && auth_hint != CYW43_AUTH_OPEN) modes[n++] = auth_hint;
        if (auth_hint != CYW43_AUTH_WPA2_MIXED_PSK) modes[n++] = CYW43_AUTH_WPA2_MIXED_PSK;
        if (auth_hint != CYW43_AUTH_WPA3_WPA2_AES_PSK) modes[n++] = CYW43_AUTH_WPA3_WPA2_AES_PSK;
    }
    int err = WIFI_ERR_FAIL;
    for (int i = 0; i < n; i++) {
        err = try_connect(ssid, pass, modes[i], cancel);
        if (err == WIFI_OK) {
            snprintf(cur_ssid, sizeof(cur_ssid), "%s", ssid);
            if (auth_used) *auth_used = modes[i];
            return WIFI_OK;
        }
        if (err == WIFI_ERR_CANCELLED || err == WIFI_ERR_NONET) break;
    }
    return err;
}

void wifi_disconnect(void) {
    if (!inited) return;
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    cur_ssid[0] = 0;
}

bool wifi_connected(void) {
    return inited && cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

const char *wifi_ssid(void) {
    return wifi_connected() ? cur_ssid : "";
}

const char *wifi_ip(void) {
    if (!wifi_connected()) return "";
    const ip4_addr_t *a = netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
    ip4addr_ntoa_r(a, ip_str, sizeof(ip_str));
    return ip_str;
}

const char *wifi_strerror(int err) {
    switch (err) {
    case WIFI_OK: return "Connected";
    case WIFI_ERR_NONET: return "Network not found";
    case WIFI_ERR_BADAUTH: return "Wrong password (authentication failed)";
    case WIFI_ERR_CANCELLED: return "Cancelled";
    case WIFI_ERR_TIMEOUT: return "Timed out (check password / signal)";
    case WIFI_ERR_INIT: return "WiFi chip not initialised";
    default: return "Connection failed";
    }
}
