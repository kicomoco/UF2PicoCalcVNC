#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ff.h"

config_t g_cfg;

static FATFS fs;
static bool mounted;
// Worst case (16 WiFi + 16 VNC entries, all fields escaped) is ~10 KB.
static char filebuf[12288];
#define CONFIG_BAK "/picocalc-vnc.bak"

static void copy_unescape(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    while (*src && n + 1 < cap) {
        char c = *src++;
        if (c == '\\' && *src) {
            c = *src++;
            if (c == 'n') c = '\n';
        }
        dst[n++] = c;
    }
    dst[n] = 0;
}

static int parse_brightness(const char *text, int minimum) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < minimum || value > 100) {
        printf("Ignoring invalid brightness setting: %s\n", text);
        return -1;
    }
    return (int)value;
}

static void parse(char *text) {
    enum { SEC_NONE, SEC_WIFI, SEC_VNC, SEC_GENERAL } sec = SEC_NONE;
    wifi_entry_t *w = NULL;
    vnc_entry_t *v = NULL;
    char *line = text;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = 0;

        if (line[0] == '#' || line[0] == 0) {
            // comment / blank
        } else if (!strcmp(line, "[wifi]")) {
            sec = SEC_WIFI;
            w = g_cfg.nwifi < MAX_WIFI ? &g_cfg.wifi[g_cfg.nwifi++] : NULL;
            if (w) memset(w, 0, sizeof(*w));
        } else if (!strcmp(line, "[vnc]")) {
            sec = SEC_VNC;
            v = g_cfg.nvnc < MAX_VNC ? &g_cfg.vnc[g_cfg.nvnc++] : NULL;
            if (v) {
                memset(v, 0, sizeof(*v));
                v->port = 5900;
                v->depth = 16;
            }
        } else if (!strcmp(line, "[general]")) {
            sec = SEC_GENERAL;
        } else {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                const char *k = line, *val = eq + 1;
                if (sec == SEC_WIFI && w) {
                    if (!strcmp(k, "ssid")) copy_unescape(w->ssid, sizeof(w->ssid), val);
                    else if (!strcmp(k, "pass")) copy_unescape(w->pass, sizeof(w->pass), val);
                    else if (!strcmp(k, "auth")) w->auth = strtoul(val, NULL, 0);
                    else if (!strcmp(k, "hidden")) w->hidden = atoi(val) != 0;
                } else if (sec == SEC_VNC && v) {
                    if (!strcmp(k, "name")) copy_unescape(v->name, sizeof(v->name), val);
                    else if (!strcmp(k, "host")) copy_unescape(v->host, sizeof(v->host), val);
                    else if (!strcmp(k, "pass")) copy_unescape(v->pass, sizeof(v->pass), val);
                    else if (!strcmp(k, "port")) v->port = (uint16_t)atoi(val);
                    else if (!strcmp(k, "depth")) v->depth = atoi(val) == 8 ? 8 : 16;
                    else if (!strcmp(k, "zoom")) v->zoom = (uint8_t)(atoi(val) % ZOOM_COUNT);
                } else if (sec == SEC_GENERAL) {
                    if (!strcmp(k, "last_vnc")) g_cfg.last_vnc = atoi(val);
                    else if (!strcmp(k, "screen_brightness")) g_cfg.screen_brightness = parse_brightness(val, 10);
                    else if (!strcmp(k, "keyboard_brightness")) g_cfg.keyboard_brightness = parse_brightness(val, 0);
                }
            }
        }
        line = next;
    }
    // Drop invalid entries
    int j = 0;
    for (int i = 0; i < g_cfg.nwifi; i++)
        if (g_cfg.wifi[i].ssid[0]) g_cfg.wifi[j++] = g_cfg.wifi[i];
    g_cfg.nwifi = j;
    j = 0;
    for (int i = 0; i < g_cfg.nvnc; i++) {
        vnc_entry_t *e = &g_cfg.vnc[i];
        if (!e->host[0]) continue;
        if (!e->port) e->port = 5900;
        if (!e->name[0]) {
            size_t len = strlen(e->host);
            if (len >= sizeof(e->name)) len = sizeof(e->name) - 1;
            memcpy(e->name, e->host, len);
            e->name[len] = 0;
        }
        g_cfg.vnc[j++] = *e;
    }
    g_cfg.nvnc = j;
    if (g_cfg.last_vnc < 0 || g_cfg.last_vnc >= g_cfg.nvnc) g_cfg.last_vnc = 0;
}

bool config_init(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.screen_brightness = g_cfg.keyboard_brightness = -1;
    mounted = f_mount(&fs, "", 1) == FR_OK;
    if (!mounted) return false;
    FIL f;
    // Fall back to the backup left by an interrupted save.
    if (f_open(&f, CONFIG_PATH, FA_READ) == FR_OK || f_open(&f, CONFIG_BAK, FA_READ) == FR_OK) {
        UINT br = 0;
        f_read(&f, filebuf, sizeof(filebuf) - 1, &br);
        f_close(&f);
        filebuf[br] = 0;
        parse(filebuf);
    }
    return true;
}

bool config_storage_ok(void) { return mounted; }

typedef struct {
    char *p;
    size_t left;
    bool overflow;
} wbuf_t;

static void put(wbuf_t *b, const char *s) {
    while (*s && b->left > 1) {
        *b->p++ = *s++;
        b->left--;
    }
    if (*s) b->overflow = true;
    *b->p = 0;
}

static void put_kv(wbuf_t *b, const char *k, const char *v) {
    char esc[160];
    size_t n = 0;
    for (; *v && n + 2 < sizeof(esc); v++) {
        if (*v == '\\') { esc[n++] = '\\'; esc[n++] = '\\'; }
        else if (*v == '\n') { esc[n++] = '\\'; esc[n++] = 'n'; }
        else if (*v != '\r') esc[n++] = *v;
    }
    esc[n] = 0;
    put(b, k);
    put(b, "=");
    put(b, esc);
    put(b, "\n");
}

static void put_ki(wbuf_t *b, const char *k, unsigned long v) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%lu", v);
    put_kv(b, k, tmp);
}

bool config_save(void) {
    if (!mounted) return false;
    wbuf_t b = {filebuf, sizeof(filebuf), false};
    put(&b, "# PicoCalc VNC settings\n[general]\n");
    put_ki(&b, "last_vnc", (unsigned long)g_cfg.last_vnc);
    if (g_cfg.screen_brightness >= 0)
        put_ki(&b, "screen_brightness", (unsigned long)g_cfg.screen_brightness);
    if (g_cfg.keyboard_brightness >= 0)
        put_ki(&b, "keyboard_brightness", (unsigned long)g_cfg.keyboard_brightness);
    for (int i = 0; i < g_cfg.nwifi; i++) {
        wifi_entry_t *w = &g_cfg.wifi[i];
        put(&b, "\n[wifi]\n");
        put_kv(&b, "ssid", w->ssid);
        put_kv(&b, "pass", w->pass);
        put_ki(&b, "auth", w->auth);
        put_ki(&b, "hidden", w->hidden);
    }
    for (int i = 0; i < g_cfg.nvnc; i++) {
        vnc_entry_t *v = &g_cfg.vnc[i];
        put(&b, "\n[vnc]\n");
        put_kv(&b, "name", v->name);
        put_kv(&b, "host", v->host);
        put_ki(&b, "port", v->port);
        put_kv(&b, "pass", v->pass);
        put_ki(&b, "depth", v->depth);
        put_ki(&b, "zoom", v->zoom);
    }
    if (b.overflow) return false;
    size_t len = sizeof(filebuf) - b.left;

    static const char *tmp = "/picocalc-vnc.tmp";
    FIL f;
    if (f_open(&f, tmp, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT bw = 0;
    FRESULT r = f_write(&f, filebuf, (UINT)len, &bw);
    f_close(&f);
    if (r != FR_OK || bw != len) {
        f_unlink(tmp);
        return false;
    }
    f_unlink(CONFIG_BAK);
    FRESULT rb = f_rename(CONFIG_PATH, CONFIG_BAK);
    if (rb != FR_OK && rb != FR_NO_FILE) {
        f_unlink(tmp);
        return false;
    }
    if (f_rename(tmp, CONFIG_PATH) != FR_OK) {
        if (rb == FR_OK) f_rename(CONFIG_BAK, CONFIG_PATH);
        return false;
    }
    f_unlink(CONFIG_BAK);
    return true;
}

int config_find_wifi(const char *ssid) {
    for (int i = 0; i < g_cfg.nwifi; i++)
        if (!strcmp(g_cfg.wifi[i].ssid, ssid)) return i;
    return -1;
}

void config_wifi_remember(const char *ssid, const char *pass, uint32_t auth, bool hidden) {
    // Inputs may point into g_cfg.wifi, which is shuffled below.
    char ssid_copy[33], pass_copy[65];
    snprintf(ssid_copy, sizeof(ssid_copy), "%s", ssid);
    snprintf(pass_copy, sizeof(pass_copy), "%s", pass ? pass : "");
    ssid = ssid_copy;
    pass = pass_copy;

    wifi_entry_t e = {0};
    int idx = config_find_wifi(ssid);
    if (idx >= 0) {
        e = g_cfg.wifi[idx];
        config_wifi_delete(idx);
        e.hidden = e.hidden || hidden;
    } else {
        e.hidden = hidden;
    }
    snprintf(e.ssid, sizeof(e.ssid), "%s", ssid);
    snprintf(e.pass, sizeof(e.pass), "%s", pass ? pass : "");
    e.auth = auth;
    if (g_cfg.nwifi == MAX_WIFI) g_cfg.nwifi--; // drop least recently used
    memmove(&g_cfg.wifi[1], &g_cfg.wifi[0], g_cfg.nwifi * sizeof(wifi_entry_t));
    g_cfg.wifi[0] = e;
    g_cfg.nwifi++;
}

void config_wifi_delete(int idx) {
    if (idx < 0 || idx >= g_cfg.nwifi) return;
    memmove(&g_cfg.wifi[idx], &g_cfg.wifi[idx + 1], (g_cfg.nwifi - idx - 1) * sizeof(wifi_entry_t));
    g_cfg.nwifi--;
}

void config_vnc_delete(int idx) {
    if (idx < 0 || idx >= g_cfg.nvnc) return;
    memmove(&g_cfg.vnc[idx], &g_cfg.vnc[idx + 1], (g_cfg.nvnc - idx - 1) * sizeof(vnc_entry_t));
    g_cfg.nvnc--;
    if (g_cfg.last_vnc >= g_cfg.nvnc) g_cfg.last_vnc = g_cfg.nvnc ? g_cfg.nvnc - 1 : 0;
}
