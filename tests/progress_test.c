#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/ui.c"

const font_t font_6x12 = {6, 12, NULL};
const font_t font_8x16 = {8, 16, NULL};
static uint64_t now;
static int strips, last_y, hint_draws, text_draws, spinner_x, spinner_y;
static bool spinner_active;
static char last_hint[32];
static kev_t event;
static bool has_event;

uint64_t time_us_64(void) { return now; }
void sys_poll(void) {}
bool wifi_connected(void) { return false; }
const char *wifi_ssid(void) { return ""; }
int kbd_battery(void) { return -1; }
bool kbd_charging(void) { return false; }
bool kbd_get(kev_t *ev) {
    if (!has_event) return false;
    *ev = event;
    has_event = false;
    return true;
}
void cv_begin(int h, uint16_t bg) { (void)h; (void)bg; }
void cv_rect(int x, int y, int w, int h, uint16_t c) {
    (void)x; (void)y; (void)w; (void)h; (void)c;
}
int cv_text(int x, int y, const char *s, const font_t *f, uint16_t color) {
    (void)x; (void)y; (void)f; (void)color;
    text_draws++;
    if (!strncmp(s, "Esc = cancel   ", 15)) {
        hint_draws++;
        snprintf(last_hint, sizeof(last_hint), "%s", s);
    }
    return 0;
}
int cv_text_clip(int x, int y, const char *s, const font_t *f, uint16_t color, int maxw) {
    (void)x; (void)y; (void)s; (void)f; (void)color; (void)maxw;
    return 0;
}
int text_width(const char *s, const font_t *f) { return (int)strlen(s) * f->w; }
void cv_end(int y) { (void)y; }
void cv_end_part(int x, int y, int w) {
    (void)x; (void)w;
    strips++;
    last_y = y;
}
void lcd_spinner_start(int x, int y, uint16_t fg, uint16_t bg) {
    assert(!spinner_active && fg == C_DIM && bg == C_BOX);
    spinner_active = true;
    spinner_x = x;
    spinner_y = y;
}
void lcd_spinner_stop(void) { spinner_active = false; }

int main(void) {
    now = 0;
    ui_progress("WiFi", "Scanning...");
    assert(!strcmp(last_hint, "Esc = cancel   |"));
    assert(spinner_active && spinner_x == BOX_X + (BOX_W - 16 * 6) / 2 + 15 * 6);
    assert(spinner_y == last_y - 18 + 3);

    now = 200000;
    ui_progress("Updates", "A longer message that wraps onto multiple lines while updating.");
    assert(spinner_active && !strcmp(last_hint, "Esc = cancel   /"));
    assert(spinner_y == last_y - 18 + 3);
    event.state = KEY_STATE_PRESSED;
    event.key = KEY_ESC;
    has_event = true;
    assert(ui_cancel_requested());
    ui_progress_end();
    assert(!spinner_active);

    ui_progress("Updates", "Still working...");
    ui_progress_note("Starting", "Initialising...");
    assert(!spinner_active);
    puts("Progress spinner tests passed.");
    return 0;
}
