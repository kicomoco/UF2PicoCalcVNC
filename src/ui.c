#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "gfx.h"
#include "kbd.h"
#include "wifi.h"

#define HDR_H   20
#define BODY_Y  22
#define ROW_H   18
#define FOOT_H  14
#define FOOT_Y  (LCD_H - FOOT_H)
#define ROWS    ((FOOT_Y - BODY_Y - 2) / ROW_H)

int ui_help_ctx = HELP_GENERAL;

extern const char *help_text(int ctx);

static char hdr_title[40];
static uint64_t last_status_us;
static bool in_help;

void ui_init(void) {
    lcd_fill(0, 0, LCD_W, LCD_H, C_BG);
}

// ---------------------------------------------------------------- header/status

static void draw_status(void) {
    char st[48];
    int bat = kbd_battery();
    const char *ssid = wifi_connected() ? wifi_ssid() : "No WiFi";
    char ss[16];
    snprintf(ss, sizeof(ss), "%.12s", ssid);
    if (bat >= 0) snprintf(st, sizeof(st), "%s %s%d%%", ss, kbd_charging() ? "+" : "", bat);
    else snprintf(st, sizeof(st), "%s", ss);
    int w = text_width(st, &font_6x12);
    int x = LCD_W - w - 4;
    cv_begin(HDR_H, C_HDR);
    cv_text(x, 4, st, &font_6x12, wifi_connected() ? C_TEXT : C_WARN);
    cv_end_part(x - 4 < 160 ? 160 : x - 4, 0, LCD_W - (x - 4 < 160 ? 160 : x - 4));
}

void ui_header(const char *title) {
    snprintf(hdr_title, sizeof(hdr_title), "%s", title);
    cv_begin(HDR_H, C_HDR);
    cv_text_clip(4, 2, hdr_title, &font_8x16, C_TEXT, 156);
    cv_end(0);
    draw_status();
    last_status_us = time_us_64();
}

void ui_status_refresh(void) {
    draw_status();
    last_status_us = time_us_64();
}

void ui_suspend_status(void) { hdr_title[0] = 0; }

void ui_footer(const char *text) {
    cv_begin(FOOT_H, C_HDR);
    cv_text_clip(3, 1, text ? text : "", &font_6x12, C_TEXT, LCD_W - 6);
    cv_end(FOOT_Y);
}

void ui_clear(const char *title, const char *footer) {
    ui_header(title);
    lcd_fill(0, HDR_H, LCD_W, FOOT_Y - HDR_H, C_BG);
    ui_footer(footer);
}

// ---------------------------------------------------------------- keys

void ui_poll(void) {
    sys_poll();
    if (!in_help && hdr_title[0] && time_us_64() - last_status_us > 1000000) ui_status_refresh();
}

int ui_getkey(void) {
    ui_poll();
    kev_t e;
    while (kbd_get(&e)) {
        if (kbd_is_mod(e.key)) continue;
        if (e.state != KEY_STATE_PRESSED) continue;
        if (e.key == KEY_F1 && !in_help) {
            ui_help(ui_help_ctx);
            return UI_KEY_REDRAW;
        }
        return e.key;
    }
    return 0;
}

int ui_waitkey(void) {
    for (;;) {
        int k = ui_getkey();
        if (k) return k;
        sleep_ms(2);
    }
}

// ---------------------------------------------------------------- text wrap

// Splits s into lines of at most maxc chars. Returns line count.
static int wrap(const char *s, int maxc, const char **st, int *ln, int maxl) {
    int n = 0;
    while (*s && n < maxl) {
        const char *nl = strchr(s, '\n');
        int len = nl ? (int)(nl - s) : (int)strlen(s);
        if (len > maxc) {
            int cut = maxc;
            while (cut > 0 && s[cut] != ' ') cut--;
            if (cut == 0) cut = maxc;
            st[n] = s;
            ln[n++] = cut;
            s += cut;
            while (*s == ' ') s++;
            continue;
        }
        st[n] = s;
        ln[n++] = len;
        s += len;
        if (*s == '\n') s++;
    }
    return n;
}

// ---------------------------------------------------------------- help

void ui_help(int ctx) {
    if (in_help) return;
    in_help = true;
    const char *txt = help_text(ctx);
    static const char *st[200];
    static int ln[200];
    const font_t *f = &font_6x12;
    int maxc = (LCD_W - 8) / f->w;
    int n = wrap(txt, maxc, st, ln, 200);
    int vis = (FOOT_Y - HDR_H - 4) / f->h;
    int top = 0;
    char saved_title[40];
    memcpy(saved_title, hdr_title, sizeof(saved_title));
    ui_header("Help");
    ui_footer("Up/Down scroll   Esc/F1/Enter close");
    bool redraw = true;
    for (;;) {
        if (redraw) {
            for (int r = 0; r < vis; r += 2) {
                cv_begin(2 * f->h, C_BG);
                for (int k = 0; k < 2; k++) {
                    int i = top + r + k;
                    if (i < n && r + k < vis) {
                        char line[64];
                        int l = ln[i] < 63 ? ln[i] : 63;
                        memcpy(line, st[i], l);
                        line[l] = 0;
                        uint16_t col = (line[0] && line[0] != ' ' && line[l - 1] == ':') ? C_ACCENT : C_TEXT;
                        cv_text(4, k * f->h, line, f, col);
                    }
                }
                cv_end(HDR_H + 2 + r * f->h);
            }
            int used = HDR_H + 2 + ((vis + 1) / 2) * 2 * f->h;
            if (used < FOOT_Y) lcd_fill(0, used, LCD_W, FOOT_Y - used, C_BG);
            redraw = false;
        }
        int k = ui_waitkey();
        if (k == KEY_ESC || k == KEY_F1 || k == KEY_ENTER || k == KEY_RETURN || k == KEY_BACKSPACE) break;
        int ntop = top;
        if (k == KEY_DOWN) ntop++;
        else if (k == KEY_UP) ntop--;
        else if (k == KEY_PGDN || k == ' ') ntop += vis - 1;
        else if (k == KEY_PGUP) ntop -= vis - 1;
        if (ntop > n - vis) ntop = n - vis;
        if (ntop < 0) ntop = 0;
        if (ntop != top) {
            top = ntop;
            redraw = true;
        }
    }
    kbd_flush();
    in_help = false;
    memcpy(hdr_title, saved_title, sizeof(hdr_title));
}

// ---------------------------------------------------------------- list

static void draw_list_row(int y, const ui_item_t *it, bool selected) {
    uint16_t bg = selected ? C_SEL : C_BG;
    cv_begin(ROW_H, bg);
    if (it) {
        if (it->flags & UI_ITEM_SEP) {
            cv_rect(0, 0, LCD_W, ROW_H, C_BG);
            cv_rect(4, ROW_H / 2, LCD_W - 8, 1, C_DIM);
            if (it->text[0]) {
                int w = text_width(it->text, &font_6x12);
                cv_rect(10, 2, w + 8, ROW_H - 4, C_BG);
                cv_text(14, 3, it->text, &font_6x12, C_DIM);
            }
        } else {
            uint16_t fg = it->color ? it->color : C_TEXT;
            if (it->flags & UI_ITEM_DISABLED) fg = C_DIM;
            if (selected && !it->color) fg = C_TEXT;
            int rw = it->right[0] ? text_width(it->right, &font_6x12) + 6 : 0;
            cv_text_clip(6, 1, it->text, &font_8x16, fg, LCD_W - 12 - rw);
            if (rw) cv_text(LCD_W - rw, 4, it->right, &font_6x12, selected ? C_TEXT : C_DIM);
        }
    }
    cv_end(y);
}

static bool selectable(const ui_item_t *items, int i) {
    return !(items[i].flags & UI_ITEM_SEP);
}

int ui_list(const char *title, const char *footer, ui_item_t *items, int n, int *sel,
            const int *exit_keys, int *exit_key) {
    int s = sel ? *sel : 0;
    if (s >= n) s = n - 1;
    if (s < 0) s = 0;
    while (s < n - 1 && !selectable(items, s)) s++;
    while (s > 0 && !selectable(items, s)) s--;
    int top = 0;
    bool full = true;
    int prev_s = -1, prev_top = -1;
    for (;;) {
        if (s < top) top = s;
        if (s >= top + ROWS) top = s - ROWS + 1;
        // keep separator headings visible above the selection
        if (top > 0 && top == s && !selectable(items, s - 1)) top--;
        if (full) {
            ui_header(title);
            ui_footer(footer);
        }
        if (full || top != prev_top) {
            for (int r = 0; r < ROWS; r++) {
                int i = top + r;
                draw_list_row(BODY_Y + r * ROW_H, i < n ? &items[i] : NULL, i == s);
            }
            int used = BODY_Y + ROWS * ROW_H;
            lcd_fill(0, HDR_H, LCD_W, BODY_Y - HDR_H, C_BG);
            if (used < FOOT_Y) lcd_fill(0, used, LCD_W, FOOT_Y - used, C_BG);
            // scroll indicator
            if (n > ROWS) {
                int h = (ROWS * ROW_H) * ROWS / n;
                int y = BODY_Y + (ROWS * ROW_H - h) * top / (n - ROWS);
                lcd_fill(LCD_W - 2, BODY_Y, 2, ROWS * ROW_H, C_BG);
                lcd_fill(LCD_W - 2, y, 2, h, C_DIM);
            }
        } else if (s != prev_s) {
            if (prev_s >= top && prev_s < top + ROWS)
                draw_list_row(BODY_Y + (prev_s - top) * ROW_H, &items[prev_s], false);
            draw_list_row(BODY_Y + (s - top) * ROW_H, &items[s], true);
        }
        full = false;
        prev_s = s;
        prev_top = top;

        int k = ui_waitkey();
        if (k == UI_KEY_REDRAW) {
            full = true;
            continue;
        }
        if (exit_keys) {
            for (const int *e = exit_keys; *e; e++) {
                if (*e == k) {
                    if (sel) *sel = s;
                    if (exit_key) *exit_key = k;
                    return -2;
                }
            }
        }
        int ns = s;
        if (k == KEY_UP) {
            do ns--; while (ns >= 0 && !selectable(items, ns));
            if (ns < 0) { // wrap to bottom
                ns = n - 1;
                while (ns > 0 && !selectable(items, ns)) ns--;
            }
        } else if (k == KEY_DOWN) {
            do ns++; while (ns < n && !selectable(items, ns));
            if (ns >= n) {
                ns = 0;
                while (ns < n - 1 && !selectable(items, ns)) ns++;
            }
        } else if (k == KEY_PGDN) {
            ns = s + ROWS - 1;
            if (ns >= n) ns = n - 1;
            while (ns > s && !selectable(items, ns)) ns--;
        } else if (k == KEY_PGUP) {
            ns = s - (ROWS - 1);
            if (ns < 0) ns = 0;
            while (ns < s && !selectable(items, ns)) ns++;
        } else if (k == KEY_ENTER || k == KEY_RETURN) {
            if (n > 0 && selectable(items, s)) {
                if (sel) *sel = s;
                return s;
            }
        } else if (k == KEY_ESC) {
            if (sel) *sel = s;
            return -1;
        }
        s = ns;
    }
}

// ---------------------------------------------------------------- popups

#define BOX_X 16
#define BOX_W (LCD_W - 2 * BOX_X)

static void box_strip_begin(int h) {
    cv_begin(h, C_BG);
    cv_rect(BOX_X, 0, BOX_W, h, C_BOX);
    cv_rect(BOX_X, 0, 1, h, C_SEL);
    cv_rect(BOX_X + BOX_W - 1, 0, 1, h, C_SEL);
}

static void box_strip_end(int y) { cv_end_part(BOX_X, y, BOX_W); }

static void box_title(int y, const char *title) {
    cv_begin(ROW_H, C_BG);
    cv_rect(BOX_X, 0, BOX_W, ROW_H, C_SEL);
    cv_text_clip(BOX_X + 6, 1, title, &font_8x16, C_TEXT, BOX_W - 12);
    box_strip_end(y);
}

static void box_bottom(int y) {
    box_strip_begin(4);
    cv_rect(BOX_X, 3, BOX_W, 1, C_SEL);
    box_strip_end(y);
}

int ui_menu(const char *title, const char *const *items, const bool *disabled, int n, int sel) {
    const int maxrows = 12;
    int rows = n < maxrows ? n : maxrows;
    int h = ROW_H + rows * ROW_H + 4;
    int y0 = (LCD_H - h) / 2;
    int s = sel < 0 ? 0 : (sel >= n ? n - 1 : sel);
    int top = 0;
    bool full = true;
    for (;;) {
        if (s < top) top = s;
        if (s >= top + rows) top = s - rows + 1;
        if (full) box_title(y0, title);
        for (int r = 0; r < rows; r++) {
            int i = top + r;
            box_strip_begin(ROW_H);
            bool dis = disabled && disabled[i];
            if (i == s) cv_rect(BOX_X + 2, 0, BOX_W - 4, ROW_H, C_SEL);
            cv_text_clip(BOX_X + 10, 1, items[i], &font_8x16, dis ? C_DIM : C_TEXT, BOX_W - 20);
            if (r == 0 && top > 0) cv_text(BOX_X + BOX_W - 10, 3, "^", &font_6x12, C_DIM);
            if (r == rows - 1 && top + rows < n) cv_text(BOX_X + BOX_W - 10, 3, "v", &font_6x12, C_DIM);
            box_strip_end(y0 + ROW_H + r * ROW_H);
        }
        if (full) box_bottom(y0 + ROW_H + rows * ROW_H);
        full = false;
        int k = ui_waitkey();
        if (k == UI_KEY_REDRAW) {
            full = true;
            lcd_fill(0, HDR_H, LCD_W, FOOT_Y - HDR_H, C_BG);
            ui_header(hdr_title);
            continue;
        }
        if (k == KEY_UP) s = (s + n - 1) % n;
        else if (k == KEY_DOWN) s = (s + 1) % n;
        else if (k == KEY_PGUP) s = s - rows < 0 ? 0 : s - rows;
        else if (k == KEY_PGDN) s = s + rows >= n ? n - 1 : s + rows;
        else if (k == KEY_ESC || k == KEY_F2) return -1;
        else if (k == KEY_ENTER || k == KEY_RETURN) {
            if (!(disabled && disabled[s])) return s;
        }
    }
}

static int draw_textbox(const char *title, const char *msg, const char *hint, uint16_t col) {
    const char *st[16];
    int ln[16];
    const font_t *f = &font_6x12;
    int maxc = (BOX_W - 16) / f->w;
    int n = wrap(msg, maxc, st, ln, 16);
    int body = n * f->h + 8 + (hint ? f->h + 6 : 0);
    int h = ROW_H + body + 4;
    int y0 = (LCD_H - h) / 2;
    box_title(y0, title);
    int y = y0 + ROW_H;
    // first strip: padding
    box_strip_begin(4);
    box_strip_end(y);
    y += 4;
    for (int i = 0; i < n; i += 2) {
        int cnt = (n - i) >= 2 ? 2 : 1;
        box_strip_begin(cnt * f->h);
        for (int k = 0; k < cnt; k++) {
            char line[64];
            int l = ln[i + k] < 63 ? ln[i + k] : 63;
            memcpy(line, st[i + k], l);
            line[l] = 0;
            cv_text(BOX_X + 8, k * f->h, line, f, col);
        }
        box_strip_end(y);
        y += cnt * f->h;
    }
    box_strip_begin(4);
    box_strip_end(y);
    y += 4;
    if (hint) {
        box_strip_begin(f->h + 6);
        int w = text_width(hint, f);
        cv_text(BOX_X + (BOX_W - w) / 2, 3, hint, f, C_DIM);
        box_strip_end(y);
        y += f->h + 6;
    }
    box_bottom(y);
    return y0;
}

void ui_message(const char *title, const char *msg) {
    kbd_flush();
    for (;;) {
        draw_textbox(title, msg, "Press Enter", C_TEXT);
        int k = ui_waitkey();
        if (k == UI_KEY_REDRAW) {
            lcd_fill(0, HDR_H, LCD_W, FOOT_Y - HDR_H, C_BG);
            ui_header(hdr_title);
            continue;
        }
        if (k == KEY_ENTER || k == KEY_RETURN || k == KEY_ESC || k == ' ') return;
    }
}

bool ui_confirm(const char *title, const char *msg) {
    kbd_flush();
    for (;;) {
        draw_textbox(title, msg, "Y / Enter = Yes    N / Esc = No", C_TEXT);
        int k = ui_waitkey();
        if (k == UI_KEY_REDRAW) {
            lcd_fill(0, HDR_H, LCD_W, FOOT_Y - HDR_H, C_BG);
            ui_header(hdr_title);
            continue;
        }
        if (k == 'y' || k == 'Y' || k == KEY_ENTER || k == KEY_RETURN) return true;
        if (k == 'n' || k == 'N' || k == KEY_ESC) return false;
    }
}

void ui_progress(const char *title, const char *msg) {
    draw_textbox(title, msg, "Esc = cancel", C_TEXT);
}

bool ui_cancel_requested(void) {
    ui_poll();
    kev_t e;
    while (kbd_get(&e)) {
        if (e.state == KEY_STATE_PRESSED && e.key == KEY_ESC) return true;
    }
    return false;
}

// ---------------------------------------------------------------- forms

#define FORM_ROW 22
#define LABEL_W  92

static void draw_field(int y, const ui_field_t *f, bool focus, bool show_pw) {
    cv_begin(FORM_ROW, C_BG);
    if (f->type == F_BUTTON) {
        int w = text_width(f->label, &font_8x16) + 24;
        int x = (LCD_W - w) / 2;
        cv_rect(x, 2, w, FORM_ROW - 4, focus ? C_SEL : C_BOX);
        cv_frame(x, 2, w, FORM_ROW - 4, focus ? C_TEXT : C_DIM);
        cv_text(x + 12, 3, f->label, &font_8x16, f->disabled ? C_DIM : C_TEXT);
        cv_end(y);
        return;
    }
    cv_text_clip(4, 3, f->label, &font_8x16, focus ? C_ACCENT : C_TEXT, LABEL_W - 6);
    int bx = LABEL_W, bw = LCD_W - LABEL_W - 4;
    cv_rect(bx, 1, bw, FORM_ROW - 2, C_EDIT);
    cv_frame(bx, 1, bw, FORM_ROW - 2, focus ? C_SEL : C_DIM);
    int maxc = (bw - 10) / font_8x16.w;
    char shown[80];
    if (f->type == F_CHOICE) {
        const char *c = f->choices[*f->choice];
        snprintf(shown, sizeof(shown), "%s%s%s", focus ? "< " : "", c, focus ? " >" : "");
        cv_text_clip(bx + 5, 3, shown, &font_8x16, C_TEXT, bw - 10);
    } else {
        int len = (int)strlen(f->buf);
        int start = len + 1 > maxc ? len + 1 - maxc : 0;   // show the tail when editing
        int j = 0;
        for (int i = start; i < len && j < (int)sizeof(shown) - 1; i++)
            shown[j++] = (f->type == F_PASSWORD && !show_pw) ? '*' : f->buf[i];
        shown[j] = 0;
        int ex = cv_text(bx + 5, 3, shown, &font_8x16, C_TEXT);
        if (focus) cv_rect(ex, 3, 2, 16, C_ACCENT);
    }
    cv_end(y);
}

int ui_form(const char *title, ui_field_t *f, int n, int *focus) {
    int fo = focus ? *focus : 0;
    if (fo < 0 || fo >= n) fo = 0;
    bool show_pw = false;
    bool full = true;
    const int y0 = BODY_Y + 4;
    int old_ctx = ui_help_ctx;
    ui_help_ctx = HELP_FORM;
    kbd_flush();
    int result = -1;
    for (;;) {
        if (full) {
            ui_clear(title, "Up/Down: field  Enter: next/select  Esc: back");
            for (int i = 0; i < n; i++) draw_field(y0 + i * (FORM_ROW + 2), &f[i], i == fo, show_pw);
            full = false;
        }
        int k = ui_waitkey();
        if (k == UI_KEY_REDRAW) {
            full = true;
            continue;
        }
        ui_field_t *cf = &f[fo];
        int nfo = fo;
        bool changed = false;
        if (k == KEY_ESC) {
            result = -1;
            break;
        } else if (k == KEY_UP) {
            nfo = (fo + n - 1) % n;
        } else if (k == KEY_DOWN || k == KEY_TAB) {
            if (k == KEY_TAB && cf->type == F_PASSWORD) {
                show_pw = !show_pw;
                changed = true;
            } else {
                nfo = (fo + 1) % n;
            }
        } else if (k == KEY_ENTER || k == KEY_RETURN) {
            if (cf->type == F_BUTTON) {
                if (!cf->disabled) {
                    result = fo;
                    break;
                }
            } else {
                int inputs = 0;
                for (int i = 0; i < n; i++) inputs += f[i].type != F_BUTTON;
                nfo = fo + 1;
                if (nfo >= n) nfo = n - 1;
                // Single-field dialogs submit on Enter
                if (inputs == 1 && f[nfo].type == F_BUTTON && !f[nfo].disabled) {
                    fo = nfo;
                    result = fo;
                    break;
                }
            }
        } else if (cf->type == F_CHOICE && (k == KEY_LEFT || k == KEY_RIGHT || k == ' ')) {
            *cf->choice = (*cf->choice + (k == KEY_LEFT ? cf->nchoices - 1 : 1)) % cf->nchoices;
            changed = true;
        } else if (cf->type == F_TEXT || cf->type == F_PASSWORD || cf->type == F_NUMBER) {
            int len = (int)strlen(cf->buf);
            if (k == KEY_BACKSPACE) {
                if (len) cf->buf[len - 1] = 0;
                changed = true;
            } else if (k == KEY_DEL) {
                cf->buf[0] = 0;
                changed = true;
            } else if (k >= 32 && k < 127 && len < cf->cap - 1) {
                if (cf->type != F_NUMBER || (k >= '0' && k <= '9')) {
                    cf->buf[len] = (char)k;
                    cf->buf[len + 1] = 0;
                    changed = true;
                }
            }
        }
        if (nfo != fo) {
            draw_field(y0 + fo * (FORM_ROW + 2), &f[fo], false, show_pw);
            fo = nfo;
            draw_field(y0 + fo * (FORM_ROW + 2), &f[fo], true, show_pw);
        } else if (changed) {
            draw_field(y0 + fo * (FORM_ROW + 2), cf, true, show_pw);
        }
    }
    if (focus) *focus = fo;
    ui_help_ctx = old_ctx;
    return result;
}

bool ui_input(const char *title, const char *label, char *buf, int cap, bool password) {
    ui_field_t f[2] = {
        {.label = label, .type = password ? F_PASSWORD : F_TEXT, .buf = buf, .cap = cap},
        {.label = "OK", .type = F_BUTTON},
    };
    int focus = 0;
    for (;;) {
        int r = ui_form(title, f, 2, &focus);
        if (r < 0) return false;
        if (r == 1) return true;
    }
}
