// PicoCalc VNC client - application entry point and screens
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "lcd.h"
#include "kbd.h"
#include "ui.h"
#include "config.h"
#include "wifi.h"
#include "vnc.h"
#include "update.h"
#include "settings.h"

enum { SCREEN_WIFI, SCREEN_VNC };

static scan_ap_t aps[MAX_SCAN];
static int naps = -1;  // -1 = never scanned
static ui_item_t items[MAX_SCAN + MAX_VNC + 16];

static void save_config(void) {
    if (config_storage_ok() && !config_save()) ui_message("Storage error", "Could not write settings to the SD card.");
}

static bool cancel_cb(void) { return ui_cancel_requested(); }

static void do_scan(void) {
    ui_progress("WiFi", "Scanning for networks...");
    naps = wifi_scan(aps, MAX_SCAN, cancel_cb);
    ui_progress_end();
    if (naps < 0) naps = 0;
}

static int find_ap(const char *ssid) {
    for (int i = 0; i < naps; i++)
        if (!strcmp(aps[i].ssid, ssid)) return i;
    return -1;
}

// Try connecting with progress UI. Returns WIFI_* code.
static int connect_with_ui(const char *ssid, const char *pass, uint32_t auth_hint, uint32_t *auth_used) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Connecting to %s ...", ssid);
    ui_progress("WiFi", msg);
    int r = wifi_connect(ssid, pass, auth_hint, auth_used, cancel_cb);
    ui_progress_end();
    if (r == WIFI_OK) ui_status_refresh();
    return r;
}

// Full connection flow for a network, prompting for a password if needed.
// Returns true when connected (and the network has been remembered).
static bool join_network(const char *ssid_in, bool secured, bool hidden) {
    char ssid[33], pass[65] = "";
    snprintf(ssid, sizeof(ssid), "%s", ssid_in);
    int idx = config_find_wifi(ssid);
    uint32_t hint = 0;
    bool have_pass = false;
    if (idx >= 0) {
        snprintf(pass, sizeof(pass), "%s", g_cfg.wifi[idx].pass);
        hint = g_cfg.wifi[idx].auth;
        hidden = hidden || g_cfg.wifi[idx].hidden;
        have_pass = true;
    }
    if (secured && !have_pass) {
        if (!ui_input(ssid, "Password", pass, sizeof(pass), true)) return false;
    }
    for (;;) {
        uint32_t used = 0;
        int r = connect_with_ui(ssid, pass, hint, &used);
        if (r == WIFI_OK) {
            config_wifi_remember(ssid, pass, used, hidden);
            save_config();
            return true;
        }
        if (r == WIFI_ERR_CANCELLED) return false;
        char msg[160];
        if (pass[0] || secured) {
            snprintf(msg, sizeof(msg), "%s.\n\nTry again with a different password?", wifi_strerror(r));
            if (!ui_confirm("Connection failed", msg)) return false;
            if (!ui_input(ssid, "Password", pass, sizeof(pass), true)) return false;
            hint = 0;
        } else {
            snprintf(msg, sizeof(msg), "Could not connect to %s: %s.", ssid, wifi_strerror(r));
            ui_message("Connection failed", msg);
            return false;
        }
    }
}

// Startup: scan and join the best known network. Returns true if connected.
static bool auto_connect(void) {
    if (!wifi_available() || g_cfg.nwifi == 0) return false;
    do_scan();
    // Known networks that are visible, strongest first (aps is sorted by RSSI)
    for (int i = 0; i < naps; i++) {
        int k = config_find_wifi(aps[i].ssid);
        if (k < 0) continue;
        uint32_t used = 0;
        wifi_entry_t w = g_cfg.wifi[k];
        int r = connect_with_ui(w.ssid, w.pass, w.auth, &used);
        if (r == WIFI_OK) {
            config_wifi_remember(w.ssid, w.pass, used, w.hidden);
            save_config();
            return true;
        }
        if (r == WIFI_ERR_CANCELLED) return false;
    }
    // Hidden networks (not broadcast, so not in the scan)
    for (int k = 0; k < g_cfg.nwifi; k++) {
        wifi_entry_t w = g_cfg.wifi[k];
        if (!w.hidden || find_ap(w.ssid) >= 0) continue;
        uint32_t used = 0;
        int r = connect_with_ui(w.ssid, w.pass, w.auth, &used);
        if (r == WIFI_OK) {
            config_wifi_remember(w.ssid, w.pass, used, true);
            save_config();
            return true;
        }
        if (r == WIFI_ERR_CANCELLED) return false;
    }
    return false;
}

// ------------------------------------------------------------------ WiFi screen

static bool hidden_network_form(void) {
    char ssid[33] = "", pass[65] = "";
    ui_field_t f[] = {
        {.label = "SSID", .type = F_TEXT, .buf = ssid, .cap = sizeof(ssid)},
        {.label = "Password", .type = F_PASSWORD, .buf = pass, .cap = sizeof(pass)},
        {.label = "Connect", .type = F_BUTTON},
        {.label = "Cancel", .type = F_BUTTON},
    };
    int focus = 0;
    for (;;) {
        int r = ui_form("Hidden network", f, 4, &focus);
        if (r != 2) return false;
        if (!ssid[0]) {
            ui_message("Hidden network", "Please enter the network name (SSID).");
            focus = 0;
            continue;
        }
        uint32_t used = 0;
        int e = connect_with_ui(ssid, pass, 0, &used);
        if (e == WIFI_OK) {
            config_wifi_remember(ssid, pass, used, true);
            save_config();
            return true;
        }
        if (e != WIFI_ERR_CANCELLED) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Could not connect to %s: %s.", ssid, wifi_strerror(e));
            ui_message("Connection failed", msg);
        }
    }
}

static void edit_wifi(int idx) {
    wifi_entry_t w = g_cfg.wifi[idx];
    bool hid = w.hidden;
    int hid_choice = hid ? 1 : 0;
    static const char *const yesno[] = {"No", "Yes"};
    ui_field_t f[] = {
        {.label = "SSID", .type = F_TEXT, .buf = w.ssid, .cap = sizeof(w.ssid)},
        {.label = "Password", .type = F_PASSWORD, .buf = w.pass, .cap = sizeof(w.pass)},
        {.label = "Hidden", .type = F_CHOICE, .choices = yesno, .nchoices = 2, .choice = &hid_choice},
        {.label = "Save", .type = F_BUTTON},
        {.label = "Cancel", .type = F_BUTTON},
    };
    int focus = 0;
    for (;;) {
        int r = ui_form("Edit network", f, 5, &focus);
        if (r != 3) return;
        if (!w.ssid[0]) {
            ui_message("Edit network", "The SSID cannot be empty.");
            focus = 0;
            continue;
        }
        int other = config_find_wifi(w.ssid);
        if (other >= 0 && other != idx) {
            ui_message("Edit network", "A network with that SSID is already saved.");
            focus = 0;
            continue;
        }
        w.hidden = hid_choice != 0;
        if (strcmp(w.pass, g_cfg.wifi[idx].pass) || strcmp(w.ssid, g_cfg.wifi[idx].ssid)) w.auth = 0;
        g_cfg.wifi[idx] = w;
        save_config();
        return;
    }
}

enum { SAVED_BACK, SAVED_JOINED, SAVED_TO_VNC };

static int saved_networks(void) {
    int sel = 0;
    for (;;) {
        if (g_cfg.nwifi == 0) {
            ui_message("Saved networks", "No saved networks.");
            return SAVED_BACK;
        }
        int n = 0;
        for (int i = 0; i < g_cfg.nwifi; i++, n++) {
            ui_item_t *it = &items[n];
            memset(it, 0, sizeof(*it));
            snprintf(it->text, sizeof(it->text), "%s", g_cfg.wifi[i].ssid);
            bool vis = find_ap(g_cfg.wifi[i].ssid) >= 0;
            bool cur = wifi_connected() && !strcmp(wifi_ssid(), g_cfg.wifi[i].ssid);
            snprintf(it->right, sizeof(it->right), "%s", cur ? "connected" : (g_cfg.wifi[i].hidden ? "hidden" : (vis ? "in range" : "")));
            if (cur) it->color = C_ACCENT;
        }
        static const int ex[] = {'e', 'E', 'd', 'D', KEY_DEL, KEY_F2, 0};
        int key = 0;
        int r = ui_list("Saved networks", "Enter: options  E: edit  D: delete  Esc: back", items, n, &sel, ex, &key);
        if (r == -1) return SAVED_BACK;
        int action;  // 0 connect 1 edit 2 delete
        if (r == -2) {
            if (key == KEY_F2) return SAVED_TO_VNC;
            action = (key == 'e' || key == 'E') ? 1 : 2;
        } else {
            static const char *const opts[] = {"Connect", "Edit", "Delete", "Cancel"};
            bool dis[4] = {!wifi_available(), false, false, false};
            action = ui_menu(g_cfg.wifi[sel].ssid, opts, dis, 4, 0);
            if (action < 0 || action == 3) continue;
        }
        if (action == 0) {
            wifi_entry_t w = g_cfg.wifi[sel];
            if (join_network(w.ssid, w.pass[0] != 0, w.hidden)) return SAVED_JOINED;
        } else if (action == 1) {
            edit_wifi(sel);
        } else {
            char msg[80];
            snprintf(msg, sizeof(msg), "Forget network '%s'?", g_cfg.wifi[sel].ssid);
            if (ui_confirm("Delete", msg)) {
                config_wifi_delete(sel);
                save_config();
                if (sel >= g_cfg.nwifi && sel > 0) sel--;
            }
        }
    }
}

static void signal_str(char *out, size_t cap, const scan_ap_t *a, bool current) {
    const char *bars = a->rssi >= -55 ? "||||" : a->rssi >= -67 ? "|||" : a->rssi >= -78 ? "||" : "|";
    snprintf(out, cap, "%s%s %s", current ? "conn " : "", a->auth ? "lock" : "open", bars);
}

static int wifi_screen(void) {
    enum { T_AP = 0, T_RESCAN = 1000, T_HIDDEN, T_SAVED, T_DISCONNECT, T_VNC, T_UPDATE, T_SETTINGS };
    int sel = -1;
    for (;;) {
        ui_help_ctx = HELP_WIFI;
        int n = 0;
        ui_item_t *it;
        bool conn = wifi_connected();
        char ip[40];

        it = &items[n++];
        memset(it, 0, sizeof(*it));
        it->flags = UI_ITEM_SEP;
        if (!wifi_available()) snprintf(it->text, sizeof(it->text), "WiFi hardware unavailable");
        else if (conn) snprintf(it->text, sizeof(it->text), "Connected: %.20s %s", wifi_ssid(), wifi_ip());
        else snprintf(it->text, sizeof(it->text), "Not connected");

        it = &items[n++];
        memset(it, 0, sizeof(*it));
        it->flags = UI_ITEM_SEP;
        snprintf(it->text, sizeof(it->text), naps < 0 ? "Not scanned" : naps == 0 ? "No networks found" : "Available networks");
        int first_ap = n;
        for (int i = 0; i < naps; i++) {
            it = &items[n++];
            memset(it, 0, sizeof(*it));
            bool saved = config_find_wifi(aps[i].ssid) >= 0;
            bool cur = conn && !strcmp(wifi_ssid(), aps[i].ssid);
            snprintf(it->text, sizeof(it->text), "%s%s", saved ? "* " : "", aps[i].ssid);
            signal_str(it->right, sizeof(it->right), &aps[i], cur);
            if (cur) it->color = C_ACCENT;
            else if (saved) it->color = C_WARN;
            it->tag = (int16_t)(T_AP + i);
        }
        it = &items[n++];
        memset(it, 0, sizeof(*it));
        it->flags = UI_ITEM_SEP;
        snprintf(it->text, sizeof(it->text), "Options");
        const struct { const char *t; int tag; bool show; } acts[] = {
            {"Rescan", T_RESCAN, wifi_available()},
            {"Hidden network...", T_HIDDEN, wifi_available()},
            {"Saved networks...", T_SAVED, g_cfg.nwifi > 0},
            {"Disconnect WiFi", T_DISCONNECT, conn},
            {"VNC connections  (F2)", T_VNC, true},
            {"Firmware updates  (U)", T_UPDATE, true},
            {"Brightness settings  (S)", T_SETTINGS, true},
        };
        for (size_t a = 0; a < sizeof(acts) / sizeof(acts[0]); a++) {
            if (!acts[a].show) continue;
            it = &items[n++];
            memset(it, 0, sizeof(*it));
            snprintf(it->text, sizeof(it->text), "%s", acts[a].t);
            it->tag = (int16_t)acts[a].tag;
        }
        if (sel < 0) sel = naps > 0 ? first_ap : first_ap + 1;
        (void)ip;

        static const int ex[] = {KEY_F2, 'r', 'R', 'd', 'D', KEY_DEL, 'u', 'U', 's', 'S', 0};
        int key = 0;
        int r = ui_list("WiFi", "Enter: join R: scan S: settings U: updates F2: VNC", items, n, &sel, ex, &key);
        if (r == -1) return SCREEN_VNC;
        if (r == -2) {
            if (key == KEY_F2) return SCREEN_VNC;
            if (key == 's' || key == 'S') {
                settings_screen();
                continue;
            }
            if (key == 'u' || key == 'U') {
                if (update_screen()) return SCREEN_VNC;
                continue;
            }
            if (key == 'r' || key == 'R') {
                if (wifi_available()) do_scan();
                continue;
            }
            // forget selected saved network
            if (items[sel].tag >= T_AP && items[sel].tag < T_RESCAN) {
                const char *ssid = aps[items[sel].tag].ssid;
                int k = config_find_wifi(ssid);
                if (k >= 0) {
                    char msg[80];
                    snprintf(msg, sizeof(msg), "Forget network '%s'?", ssid);
                    if (ui_confirm("Delete", msg)) {
                        config_wifi_delete(k);
                        save_config();
                    }
                }
            }
            continue;
        }
        int tag = items[r].tag;
        if (tag >= T_AP && tag < T_RESCAN) {
            scan_ap_t *a = &aps[tag];
            if (conn && !strcmp(wifi_ssid(), a->ssid)) return SCREEN_VNC;
            if (join_network(a->ssid, a->auth != 0, false)) return SCREEN_VNC;
        } else if (tag == T_RESCAN) {
            do_scan();
        } else if (tag == T_HIDDEN) {
            if (hidden_network_form()) return SCREEN_VNC;
        } else if (tag == T_SAVED) {
            if (saved_networks() != SAVED_BACK) return SCREEN_VNC;
        } else if (tag == T_DISCONNECT) {
            wifi_disconnect();
            ui_status_refresh();
        } else if (tag == T_VNC) {
            return SCREEN_VNC;
        } else if (tag == T_UPDATE) {
            if (update_screen()) return SCREEN_VNC;
        } else if (tag == T_SETTINGS) {
            settings_screen();
        }
    }
}

// ------------------------------------------------------------------ VNC screen

static const char *const depth_names[] = {"16-bit colour", "8-bit (faster)"};
static const char *const zoom_names[] = {"Fit to screen", "50%", "100%"};

// Returns: -1 cancelled, 0 saved, 1 save & connect
static int edit_vnc(vnc_entry_t *v, const char *title) {
    char port[8];
    snprintf(port, sizeof(port), "%u", v->port ? v->port : 5900);
    int depth = v->depth == 8 ? 1 : 0;
    int zoom = v->zoom % ZOOM_COUNT;
    ui_field_t f[] = {
        {.label = "Name", .type = F_TEXT, .buf = v->name, .cap = sizeof(v->name)},
        {.label = "Host", .type = F_TEXT, .buf = v->host, .cap = sizeof(v->host)},
        {.label = "Port", .type = F_NUMBER, .buf = port, .cap = sizeof(port)},
        {.label = "Password", .type = F_PASSWORD, .buf = v->pass, .cap = sizeof(v->pass)},
        {.label = "Colour", .type = F_CHOICE, .choices = depth_names, .nchoices = 2, .choice = &depth},
        {.label = "Zoom", .type = F_CHOICE, .choices = zoom_names, .nchoices = ZOOM_COUNT, .choice = &zoom},
        {.label = "Save & Connect", .type = F_BUTTON, .disabled = !wifi_connected()},
        {.label = "Save", .type = F_BUTTON},
        {.label = "Cancel", .type = F_BUTTON},
    };
    int focus = 0;
    for (;;) {
        f[6].disabled = !wifi_connected();
        int r = ui_form(title, f, 9, &focus);
        if (r < 0 || r == 8) return -1;
        // Accept "host:display" or "host:port" in the host field
        char *colon = strrchr(v->host, ':');
        if (colon && strchr(v->host, ':') == colon && colon[1]) {
            char *end;
            long p = strtol(colon + 1, &end, 10);
            if (!*end && p >= 0) {
                snprintf(port, sizeof(port), "%ld", p);
                *colon = 0;
            }
        }
        if (!v->host[0]) {
            ui_message(title, "Please enter the server host name or IP address.");
            focus = 1;
            continue;
        }
        long p = port[0] ? strtol(port, NULL, 10) : 5900;
        if (p < 100) p += 5900;  // VNC display number
        if (p > 65535) {
            ui_message(title, "Port must be 1-65535 (or a display number 0-99).");
            focus = 2;
            continue;
        }
        v->port = (uint16_t)p;
        v->depth = depth ? 8 : 16;
        v->zoom = (uint8_t)zoom;
        if (!v->name[0]) snprintf(v->name, sizeof(v->name), "%s", v->host);
        return r == 6 ? 1 : 0;
    }
}

static void connect_vnc(int idx) {
    if (!wifi_connected()) {
        ui_message("Not connected", "Connect to a WiFi network first (press F2).");
        return;
    }
    g_cfg.last_vnc = idx;
    uint8_t old_zoom = g_cfg.vnc[idx].zoom;
    char err[160];
    bool ok = vnc_session(&g_cfg.vnc[idx], err, sizeof(err));
    ui_init();
    save_config();
    (void)old_zoom;
    if (!ok) ui_message("VNC", err);
}

static int vnc_screen(void) {
    int sel = g_cfg.last_vnc;
    for (;;) {
        ui_help_ctx = HELP_VNCLIST;
        bool online = wifi_connected();
        int n = 0;
        for (int i = 0; i < g_cfg.nvnc; i++, n++) {
            ui_item_t *it = &items[n];
            memset(it, 0, sizeof(*it));
            snprintf(it->text, sizeof(it->text), "%s", g_cfg.vnc[i].name);
            snprintf(it->right, sizeof(it->right), "%.16s:%u", g_cfg.vnc[i].host, g_cfg.vnc[i].port);
            it->tag = (int16_t)i;
        }
        ui_item_t *it = &items[n++];
        memset(it, 0, sizeof(*it));
        snprintf(it->text, sizeof(it->text), "New Connection");
        it->color = C_ACCENT;
        it->tag = -1;

        static const int ex[] = {KEY_F2, 'e', 'E', 'd', 'D', KEY_DEL, 'u', 'U', 's', 'S', 0};
        int key = 0;
        const char *footer = online ? "E: edit D: del S: settings U: updates F2: WiFi"
                                    : "Offline S: settings U: updates F2: WiFi";
        int r = ui_list("VNC Connections", footer, items, n, &sel, ex, &key);
        if (r == -1) return SCREEN_WIFI;
        int idx = items[sel].tag;
        if (r == -2) {
            if (key == KEY_F2) return SCREEN_WIFI;
            if (key == 's' || key == 'S') {
                settings_screen();
                continue;
            }
            if (key == 'u' || key == 'U') {
                if (update_screen()) return SCREEN_WIFI;
                continue;
            }
            if (idx < 0) continue;
            if (key == 'e' || key == 'E') {
                vnc_entry_t v = g_cfg.vnc[idx];
                int res = edit_vnc(&v, "Edit connection");
                if (res >= 0) {
                    g_cfg.vnc[idx] = v;
                    save_config();
                    if (res == 1) connect_vnc(idx);
                }
            } else {
                char msg[80];
                snprintf(msg, sizeof(msg), "Delete connection '%s'?", g_cfg.vnc[idx].name);
                if (ui_confirm("Delete", msg)) {
                    config_vnc_delete(idx);
                    save_config();
                }
            }
            continue;
        }
        if (idx < 0) {
            if (g_cfg.nvnc >= MAX_VNC) {
                ui_message("New connection", "Too many saved connections. Delete one first.");
                continue;
            }
            vnc_entry_t v;
            memset(&v, 0, sizeof(v));
            v.port = 5900;
            v.depth = 16;
            v.zoom = ZOOM_FIT;
            int res = edit_vnc(&v, "New connection");
            if (res < 0) continue;
            g_cfg.vnc[g_cfg.nvnc++] = v;
            sel = g_cfg.nvnc - 1;
            g_cfg.last_vnc = sel;
            save_config();
            if (res == 1) connect_vnc(sel);
            continue;
        }
        static const char *const opts[] = {"Connect", "Edit", "Delete", "Cancel"};
        bool dis[4] = {!online, false, false, false};
        int a = ui_menu(g_cfg.vnc[idx].name, opts, dis, 4, online ? 0 : 1);
        if (a == 0) {
            connect_vnc(idx);
        } else if (a == 1) {
            vnc_entry_t v = g_cfg.vnc[idx];
            int res = edit_vnc(&v, "Edit connection");
            if (res >= 0) {
                g_cfg.vnc[idx] = v;
                save_config();
                if (res == 1) connect_vnc(idx);
            }
        } else if (a == 2) {
            char msg[80];
            snprintf(msg, sizeof(msg), "Delete connection '%s'?", g_cfg.vnc[idx].name);
            if (ui_confirm("Delete", msg)) {
                config_vnc_delete(idx);
                save_config();
                if (sel >= g_cfg.nvnc && sel > 0) sel--;
            }
        }
    }
}

// ------------------------------------------------------------------ main

int main(void) {
    stdio_init_all();
    lcd_init();
    ui_init();
    kbd_init();

    ui_clear("PicoCalc VNC", "F1: help");
    ui_progress_note("Starting", "Initialising WiFi and SD card...");

    bool storage = config_init();
    bool brightness_ok = settings_apply_saved();
    bool wifi_ok = wifi_init();
    ui_status_refresh();

    if (!storage)
        ui_message("No SD card",
                   "Could not read the SD card. Saved networks and connections will not be remembered.");
    if (!wifi_ok) ui_message("WiFi error", "The WiFi chip could not be initialised.");
    if (!brightness_ok) ui_message("Brightness error", "Could not apply saved brightness to the controller.");

    int screen = SCREEN_WIFI;
    if (auto_connect()) screen = SCREEN_VNC;
    else if (wifi_ok && naps < 0) do_scan();

    for (;;) screen = (screen == SCREEN_WIFI) ? wifi_screen() : vnc_screen();
}
