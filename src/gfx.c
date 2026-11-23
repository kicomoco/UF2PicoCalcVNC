#include "gfx.h"
#include <string.h>

static uint16_t cv[LCD_W * CV_MAX_H];
static int cv_h;

void cv_begin(int h, uint16_t bg) {
    if (h > CV_MAX_H) h = CV_MAX_H;
    cv_h = h;
    for (int i = 0; i < LCD_W * h; i++) cv[i] = bg;
}

void cv_rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > cv_h) h = cv_h - y;
    for (int r = 0; r < h; r++) {
        uint16_t *p = &cv[(y + r) * LCD_W + x];
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

void cv_frame(int x, int y, int w, int h, uint16_t c) {
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

static void cv_char(int x, int y, char ch, const font_t *f, uint16_t fg) {
    const uint8_t *g = font_glyph(f, ch);
    for (int r = 0; r < f->h; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= cv_h) continue;
        uint8_t bits = g[r];
        uint16_t *p = &cv[yy * LCD_W];
        for (int c = 0; c < f->w; c++) {
            int xx = x + c;
            if ((bits & (0x80 >> c)) && xx >= 0 && xx < LCD_W) p[xx] = fg;
        }
    }
}

int cv_text(int x, int y, const char *s, const font_t *f, uint16_t fg) {
    for (; *s; s++) {
        if (x >= LCD_W) break;
        cv_char(x, y, *s, f, fg);
        x += f->w;
    }
    return x;
}

int cv_text_clip(int x, int y, const char *s, const font_t *f, uint16_t fg, int maxw) {
    int maxc = maxw / f->w;
    int len = (int)strlen(s);
    if (len <= maxc) return cv_text(x, y, s, f, fg);
    for (int i = 0; i < maxc - 2 && i < len; i++) {
        cv_char(x, y, s[i], f, fg);
        x += f->w;
    }
    return cv_text(x, y, "..", f, fg);
}

void cv_end(int y) {
    lcd_blit(cv, LCD_W, 0, y, LCD_W, cv_h);
}

void cv_end_part(int x, int y, int w) {
    lcd_blit(cv + x, LCD_W, x, y, w, cv_h);
}

int text_width(const char *s, const font_t *f) {
    return (int)strlen(s) * f->w;
}
