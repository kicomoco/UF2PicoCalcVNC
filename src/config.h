// Persistent settings (saved WiFi networks and VNC connections) on the SD card
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAX_WIFI 16
#define MAX_VNC  16

#define CONFIG_PATH "/picocalc-vnc.ini"

typedef struct {
    char ssid[33];
    char pass[65];
    uint32_t auth;   // last auth mode that worked (0 = unknown/open)
    bool hidden;     // added manually (may not appear in scans)
} wifi_entry_t;

enum { ZOOM_FIT = 0, ZOOM_HALF = 1, ZOOM_FULL = 2, ZOOM_COUNT };

typedef struct {
    char name[32];
    char host[64];
    uint16_t port;
    char pass[64];
    uint8_t depth;   // 16 or 8 bits per pixel
    uint8_t zoom;    // ZOOM_*
} vnc_entry_t;

typedef struct {
    wifi_entry_t wifi[MAX_WIFI];   // most recently used first
    int nwifi;
    vnc_entry_t vnc[MAX_VNC];
    int nvnc;
    int last_vnc;
} config_t;

extern config_t g_cfg;

bool config_init(void);       // mount SD and load; false if no storage available
bool config_storage_ok(void);
bool config_save(void);

int  config_find_wifi(const char *ssid);
void config_wifi_remember(const char *ssid, const char *pass, uint32_t auth, bool hidden);
void config_wifi_delete(int idx);
void config_vnc_delete(int idx);
