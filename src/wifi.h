#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAX_SCAN 32

typedef struct {
    char ssid[33];
    int16_t rssi;
    uint8_t auth;     // scan security bits: 1=WEP 2=WPA 4=WPA2 (0=open)
    uint8_t channel;
} scan_ap_t;

typedef bool (*cancel_fn)(void);

enum {
    WIFI_OK = 0,
    WIFI_ERR_FAIL = -1,
    WIFI_ERR_NONET = -2,
    WIFI_ERR_BADAUTH = -3,
    WIFI_ERR_CANCELLED = -100,
    WIFI_ERR_TIMEOUT = -101,
    WIFI_ERR_INIT = -102,
};

bool wifi_init(void);
bool wifi_available(void);
void sys_poll(void);    // service the WiFi chip / lwIP; call often

int  wifi_scan(scan_ap_t *out, int max, cancel_fn cancel);   // sorted by RSSI, <0 on error
// Connect, trying suitable auth modes. *auth_used receives the mode that worked.
int  wifi_connect(const char *ssid, const char *pass, uint32_t auth_hint, uint32_t *auth_used, cancel_fn cancel);
void wifi_disconnect(void);
bool wifi_connected(void);
const char *wifi_ssid(void);
const char *wifi_ip(void);
const char *wifi_strerror(int err);
