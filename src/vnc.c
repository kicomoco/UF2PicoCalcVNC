// VNC (RFB 3.3/3.7/3.8) client session: handshake, decoders, scaling, input.
#include "vnc.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "lcd.h"
#include "kbd.h"
#include "ui.h"
#include "net.h"
#include "des.h"
#include "wifi.h"
#include "miniz.h"
#include "settings.h"

#define MAX_FB_DIM 4096
#define ERR_PROTO  (-1000)
#define ERR_USER   (-1001)

// Keysyms
#define XK_BackSpace 0xff08
#define XK_Tab       0xff09
#define XK_Return    0xff0d
#define XK_Escape    0xff1b
#define XK_Home      0xff50
#define XK_Left      0xff51
#define XK_Up        0xff52
#define XK_Right     0xff53
#define XK_Down      0xff54
#define XK_Page_Up   0xff55
#define XK_Page_Down 0xff56
#define XK_End       0xff57
#define XK_Print     0xff61
#define XK_Insert    0xff63
#define XK_Break     0xff6b
#define XK_F1        0xffbe
#define XK_Shift_L   0xffe1
#define XK_Control_L 0xffe3
#define XK_Alt_L     0xffe9
#define XK_Super_L   0xffeb
#define XK_Delete    0xffff

// ------------------------------------------------------------------ state

static uint16_t fb[LCD_W * LCD_H];          // what is on the LCD (minus cursor)
static uint16_t tile[64 * 64];
static uint8_t rowbuf[MAX_FB_DIM * 2];
static uint16_t repx[LCD_W], repy[LCD_H];   // local -> remote sample coordinate
static uint16_t mapx[MAX_FB_DIM + 1], mapy[MAX_FB_DIM + 1]; // remote -> first local
static uint16_t lut8[256];
static uint16_t line[LCD_W];

static jmp_buf jb;
static char errmsg[128];

static int fbw, fbh, bpp;
static int zoom;
static int vx, vy, vw, vh;      // viewport in remote coordinates
static int ox, oy, lw, lh;      // its location/size on the LCD

static int mx, my;              // remote pointer position
static uint8_t buttons;
static bool ptr_dirty;
static uint64_t last_ptr_us;
static int cur_x = -100, cur_y = -100;      // cursor hotspot on LCD

static int dx0, dy0, dx1, dy1;  // dirty rect (LCD coordinates)
static bool dirty;
static uint64_t last_flush_us;

static bool arrow_mouse = true;
static bool held[4];            // L, U, D, R
static uint64_t held_start[4], held_last[4];
static uint64_t last_move_us;
static bool ctrl_sent, alt_sent;

static bool want_help, want_menu, want_disc, want_zoom, want_refresh, want_pan;
static bool refresh_pending;
static int rf_x0, rf_y0, rf_x1, rf_y1;      // pending non-incremental region (remote)

// ------------------------------------------------------------------ errors / io

static void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errmsg, sizeof(errmsg), fmt, ap);
    va_end(ap);
    longjmp(jb, ERR_PROTO);
}

static void rd(void *b, size_t n) {
    int r = net_read(b, n);
    if (r) longjmp(jb, r);
}

static void skip(size_t n) {
    int r = net_skip(n);
    if (r) longjmp(jb, r);
}

static void wr(const void *b, size_t n) {
    int r = net_write(b, n);
    if (r) longjmp(jb, r);
}

static uint8_t rd8(void) {
    uint8_t v;
    rd(&v, 1);
    return v;
}

static uint16_t rd16(void) {
    uint8_t b[2];
    rd(b, 2);
    return (uint16_t)(b[0] << 8 | b[1]);
}

static uint32_t rd32(void) {
    uint8_t b[4];
    rd(b, 4);
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static inline uint16_t cpix(const uint8_t *p) {
    return bpp == 2 ? (uint16_t)(p[0] | p[1] << 8) : lut8[p[0]];
}

static uint16_t rdpix(void) {
    uint8_t b[2];
    rd(b, bpp);
    return cpix(b);
}

static void read_reason(const char *prefix) {
    uint32_t len = rd32();
    char reason[96];
    uint32_t take = len < sizeof(reason) - 1 ? len : sizeof(reason) - 1;
    rd(reason, take);
    reason[take] = 0;
    if (len > take) skip(len - take);
    fail("%s: %s", prefix, reason);
}

// ------------------------------------------------------------------ client messages

static void send_update_request(bool incremental, int x, int y, int w, int h) {
    uint8_t m[10] = {3, incremental, x >> 8, x, y >> 8, y, w >> 8, w, h >> 8, h};
    wr(m, sizeof(m));
}

static void send_key(uint32_t ks, bool down) {
    uint8_t m[8] = {4, down, 0, 0, ks >> 24, ks >> 16, ks >> 8, ks};
    wr(m, sizeof(m));
}

static void send_pointer_now(uint8_t mask) {
    uint8_t m[6] = {5, mask, mx >> 8, mx, my >> 8, my};
    wr(m, sizeof(m));
    ptr_dirty = false;
    last_ptr_us = time_us_64();
}

// ------------------------------------------------------------------ geometry

static void build_maps(void) {
    for (int lx = 0; lx < lw; lx++) repx[lx] = (uint16_t)(vx + ((uint32_t)lx * vw + vw / 2) / lw);
    for (int ly = 0; ly < lh; ly++) repy[ly] = (uint16_t)(vy + ((uint32_t)ly * vh + vh / 2) / lh);
    int l = 0;
    for (int r = 0; r <= fbw; r++) {
        while (l < lw && repx[l] < r) l++;
        mapx[r] = (uint16_t)l;
    }
    l = 0;
    for (int r = 0; r <= fbh; r++) {
        while (l < lh && repy[l] < r) l++;
        mapy[r] = (uint16_t)l;
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void center_viewport(void) {
    vx = clampi(mx - vw / 2, 0, fbw - vw);
    vy = clampi(my - vh / 2, 0, fbh - vh);
}

static void layout(void) {
    bool fit = zoom == ZOOM_FIT || (zoom == ZOOM_HALF && fbw <= 2 * LCD_W && fbh <= 2 * LCD_H) ||
               (fbw <= LCD_W && fbh <= LCD_H);
    if (fit) {
        vw = fbw;
        vh = fbh;
        if (fbw <= LCD_W && fbh <= LCD_H) {
            lw = fbw;
            lh = fbh;
        } else if (fbw >= fbh) {
            lw = LCD_W;
            lh = (int)((uint32_t)fbh * LCD_W / fbw);
        } else {
            lh = LCD_H;
            lw = (int)((uint32_t)fbw * LCD_H / fbh);
        }
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
    } else if (zoom == ZOOM_HALF) {
        vw = fbw < 2 * LCD_W ? fbw : 2 * LCD_W;
        vh = fbh < 2 * LCD_H ? fbh : 2 * LCD_H;
        lw = (vw + 1) / 2;
        lh = (vh + 1) / 2;
    } else {
        vw = fbw < LCD_W ? fbw : LCD_W;
        vh = fbh < LCD_H ? fbh : LCD_H;
        lw = vw;
        lh = vh;
    }
    ox = (LCD_W - lw) / 2;
    oy = (LCD_H - lh) / 2;
    center_viewport();
    build_maps();
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = 0;
}

// ------------------------------------------------------------------ LCD flush + cursor

#define CUR_W 12
#define CUR_H 19
static const char *const cursor_img[CUR_H] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ",
    "X....X      ", "X.....X     ", "X......X    ", "X.......X   ", "X........X  ",
    "X.........X ", "X......XXXXX", "X...X..X    ", "X..XX..X    ", "X.X  X..X   ",
    "XX   X..X   ", "X     X..X  ", "      X..X  ", "       XX   ",
};

static void flush_rect(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LCD_W) x1 = LCD_W;
    if (y1 > LCD_H) y1 = LCD_H;
    if (x0 >= x1 || y0 >= y1) return;
    int w = x1 - x0;
    lcd_begin(x0, y0, w, y1 - y0);
    for (int y = y0; y < y1; y++) {
        const uint16_t *src = &fb[y * LCD_W + x0];
        int cr = y - cur_y;
        if (cr >= 0 && cr < CUR_H && cur_x < x1 && cur_x + CUR_W > x0) {
            memcpy(line, src, w * 2);
            const char *row = cursor_img[cr];
            for (int c = 0; c < CUR_W; c++) {
                int x = cur_x + c;
                if (x < x0 || x >= x1 || row[c] == ' ') continue;
                line[x - x0] = row[c] == 'X' ? 0x0000 : 0xFFFF;
            }
            lcd_push(line, w);
        } else {
            lcd_push(src, w);
        }
    }
    lcd_end();
}

static void flush_dirty(void) {
    if (dirty) flush_rect(dx0, dy0, dx1, dy1);
    dirty = false;
    last_flush_us = time_us_64();
}

static void redraw_all(void) {
    dirty = false;
    flush_rect(0, 0, LCD_W, LCD_H);
}

static inline void mark_dirty(int x0, int y0, int x1, int y1) {
    if (!dirty) {
        dx0 = x0; dy0 = y0; dx1 = x1; dy1 = y1;
        dirty = true;
        return;
    }
    if (x0 < dx0) dx0 = x0;
    if (y0 < dy0) dy0 = y0;
    if (x1 > dx1) dx1 = x1;
    if (y1 > dy1) dy1 = y1;
}

static void update_cursor(void) {
    int lx = mapx[mx] < lw ? mapx[mx] : lw - 1;
    int ly = mapy[my] < lh ? mapy[my] : lh - 1;
    int nx = ox + lx, ny = oy + ly;
    if (nx == cur_x && ny == cur_y) return;
    int px = cur_x, py = cur_y;
    cur_x = nx;
    cur_y = ny;
    int x0 = px < nx ? px : nx, y0 = py < ny ? py : ny;
    int x1 = (px > nx ? px : nx) + CUR_W, y1 = (py > ny ? py : ny) + CUR_H;
    if (x1 - x0 > 3 * CUR_W || y1 - y0 > 3 * CUR_H) { // far apart: two small flushes
        flush_rect(px, py, px + CUR_W, py + CUR_H);
        flush_rect(nx, ny, nx + CUR_W, ny + CUR_H);
    } else {
        flush_rect(x0, y0, x1, y1);
    }
}

// ------------------------------------------------------------------ drawing into fb

static void fill_rect(int x, int y, int w, int h, uint16_t c) {
    int x0 = mapx[x], x1 = mapx[x + w], y0 = mapy[y], y1 = mapy[y + h];
    if (x0 >= x1 || y0 >= y1) return;
    for (int ly = y0; ly < y1; ly++) {
        uint16_t *p = &fb[(oy + ly) * LCD_W + ox];
        for (int lx = x0; lx < x1; lx++) p[lx] = c;
    }
    mark_dirty(ox + x0, oy + y0, ox + x1, oy + y1);
}

// Pixels already converted to RGB565, row stride in pixels
static void put_px16(int x, int y, int w, int h, const uint16_t *src, int stride) {
    int x0 = mapx[x], x1 = mapx[x + w], y0 = mapy[y], y1 = mapy[y + h];
    if (x0 >= x1 || y0 >= y1) return;
    for (int ly = y0; ly < y1; ly++) {
        const uint16_t *s = src + (repy[ly] - y) * stride - x;
        uint16_t *p = &fb[(oy + ly) * LCD_W + ox];
        for (int lx = x0; lx < x1; lx++) p[lx] = s[repx[lx]];
    }
    mark_dirty(ox + x0, oy + y0, ox + x1, oy + y1);
}

// Raw wire pixels, row stride in pixels
static void put_raw(int x, int y, int w, int h, const uint8_t *src, int stride) {
    int x0 = mapx[x], x1 = mapx[x + w], y0 = mapy[y], y1 = mapy[y + h];
    if (x0 >= x1 || y0 >= y1) return;
    for (int ly = y0; ly < y1; ly++) {
        const uint8_t *s = src + (repy[ly] - y) * stride * bpp;
        uint16_t *p = &fb[(oy + ly) * LCD_W + ox];
        if (bpp == 2) {
            for (int lx = x0; lx < x1; lx++) {
                const uint8_t *q = s + (repx[lx] - x) * 2;
                p[lx] = (uint16_t)(q[0] | q[1] << 8);
            }
        } else {
            for (int lx = x0; lx < x1; lx++) p[lx] = lut8[s[repx[lx] - x]];
        }
    }
    mark_dirty(ox + x0, oy + y0, ox + x1, oy + y1);
}

static void add_refresh(int x, int y, int w, int h) {
    if (!refresh_pending) {
        rf_x0 = x; rf_y0 = y; rf_x1 = x + w; rf_y1 = y + h;
        refresh_pending = true;
        return;
    }
    if (x < rf_x0) rf_x0 = x;
    if (y < rf_y0) rf_y0 = y;
    if (x + w > rf_x1) rf_x1 = x + w;
    if (y + h > rf_y1) rf_y1 = y + h;
}

// ------------------------------------------------------------------ decoders

static void dec_raw(int x, int y, int w, int h) {
    size_t rowbytes = (size_t)w * bpp;
    bool cols = mapx[x] < mapx[x + w];
    for (int r = 0; r < h; r++) {
        if (!cols || mapy[y + r] == mapy[y + r + 1]) {
            skip(rowbytes);
            continue;
        }
        rd(rowbuf, rowbytes);
        put_raw(x, y + r, w, 1, rowbuf, w);
    }
}

static void dec_copyrect(int x, int y, int w, int h) {
    int sx = rd16(), sy = rd16();
    if (sx + w > fbw || sy + h > fbh) fail("CopyRect source out of range");
    int x0 = mapx[x], x1 = mapx[x + w], y0 = mapy[y], y1 = mapy[y + h];
    if (x0 >= x1 || y0 >= y1) return;
    if (sx < vx || sy < vy || sx + w > vx + vw || sy + h > vy + vh) {
        add_refresh(x, y, w, h);  // source not on screen
        return;
    }
    int ddx = x - sx, ddy = y - sy;
    int step = ddy > 0 ? -1 : 1;
    int ly = step > 0 ? y0 : y1 - 1;
    for (; ly >= y0 && ly < y1; ly += step) {
        int sly = mapy[repy[ly] - ddy];
        if (sly >= lh) sly = lh - 1;
        const uint16_t *srow = &fb[(oy + sly) * LCD_W + ox];
        for (int lx = x0; lx < x1; lx++) {
            int slx = mapx[repx[lx] - ddx];
            line[lx] = srow[slx < lw ? slx : lw - 1];
        }
        memcpy(&fb[(oy + ly) * LCD_W + ox + x0], &line[x0], (x1 - x0) * 2);
    }
    mark_dirty(ox + x0, oy + y0, ox + x1, oy + y1);
}

static void dec_rre(int x, int y, int w, int h) {
    uint32_t n = rd32();
    fill_rect(x, y, w, h, rdpix());
    while (n--) {
        uint16_t c = rdpix();
        int sx = rd16(), sy = rd16(), sw = rd16(), sh = rd16();
        if (sx + sw > w || sy + sh > h) fail("RRE subrect out of range");
        fill_rect(x + sx, y + sy, sw, sh, c);
    }
}

static void dec_hextile(int x, int y, int w, int h) {
    uint16_t bg = 0, fg = 0;
    for (int ty = y; ty < y + h; ty += 16) {
        int th = y + h - ty < 16 ? y + h - ty : 16;
        for (int tx = x; tx < x + w; tx += 16) {
            int tw = x + w - tx < 16 ? x + w - tx : 16;
            uint8_t sub = rd8();
            if (sub & 1) {
                rd(rowbuf, (size_t)tw * th * bpp);
                put_raw(tx, ty, tw, th, rowbuf, tw);
                continue;
            }
            if (sub & 2) bg = rdpix();
            if (sub & 4) fg = rdpix();
            fill_rect(tx, ty, tw, th, bg);
            if (!(sub & 8)) continue;
            int n = rd8();
            bool coloured = sub & 16;
            int sz = coloured ? bpp + 2 : 2;
            rd(rowbuf, (size_t)n * sz);
            const uint8_t *p = rowbuf;
            for (int i = 0; i < n; i++, p += sz) {
                uint16_t c = fg;
                if (coloured) c = cpix(p);
                const uint8_t *q = coloured ? p + bpp : p;
                int sx = q[0] >> 4, sy = q[0] & 15, sw = (q[1] >> 4) + 1, sh = (q[1] & 15) + 1;
                if (sx + sw > tw) sw = tw - sx;
                if (sy + sh > th) sh = th - sy;
                if (sw > 0 && sh > 0) fill_rect(tx + sx, ty + sy, sw, sh, c);
            }
        }
    }
}

// --- ZRLE (zlib stream persists for the whole connection)

static tinfl_decompressor zinf;
static uint8_t zdict[TINFL_LZ_DICT_SIZE];
static uint8_t zin[2048];
static size_t zin_pos, zin_len, zdict_ofs, zrd, zavail;
static uint32_t zremain;
static int zhdr_skip;

static void zreset(void) {
    tinfl_init(&zinf);
    zin_pos = zin_len = zdict_ofs = zrd = zavail = 0;
    zremain = 0;
    zhdr_skip = 2;  // raw inflate: skip the zlib header manually
}

static void zfill(void) {
    size_t n = zremain < sizeof(zin) ? zremain : sizeof(zin);
    rd(zin, n);
    zremain -= n;
    zin_pos = 0;
    zin_len = n;
    while (zhdr_skip && zin_pos < zin_len) {
        zin_pos++;
        zhdr_skip--;
    }
}

// Produce more output. Returns false if the rect's input is exhausted.
static bool zinflate(void) {
    for (int guard = 0; guard < 1000000; guard++) {
        if (zin_pos == zin_len && zremain) zfill();
        size_t in = zin_len - zin_pos;
        size_t out = TINFL_LZ_DICT_SIZE - zdict_ofs;
        tinfl_status st = tinfl_decompress(&zinf, zin + zin_pos, &in, zdict, zdict + zdict_ofs, &out,
                                           TINFL_FLAG_HAS_MORE_INPUT);
        zin_pos += in;
        if (st < 0) fail("ZRLE decompression error (%d)", (int)st);
        if (out) {
            zrd = zdict_ofs;
            zavail = out;
            zdict_ofs = (zdict_ofs + out) & (TINFL_LZ_DICT_SIZE - 1);
            return true;
        }
        if (zin_pos == zin_len && !zremain) return false;
        if (st == TINFL_STATUS_DONE) fail("ZRLE stream ended unexpectedly");
        if (in == 0 && st != TINFL_STATUS_NEEDS_MORE_INPUT) fail("ZRLE decoder stalled");
    }
    fail("ZRLE decoder stalled");
    return false;
}

static void zread(uint8_t *dst, size_t n) {
    while (n) {
        if (!zavail && !zinflate()) fail("ZRLE data truncated");
        size_t take = n < zavail ? n : zavail;
        memcpy(dst, zdict + zrd, take);
        zrd += take;
        zavail -= take;
        dst += take;
        n -= take;
    }
}

static inline uint8_t zb(void) {
    if (!zavail && !zinflate()) fail("ZRLE data truncated");
    zavail--;
    return zdict[zrd++];
}

static inline uint16_t zpix(void) {
    if (bpp == 1) return lut8[zb()];
    uint8_t lo = zb();
    return (uint16_t)(lo | zb() << 8);
}

static void zdrain(void) {
    zavail = 0;
    while (zin_pos < zin_len || zremain) {
        if (!zinflate()) break;
        zavail = 0;  // discard (should not happen with a well-formed stream)
    }
}

static void dec_zrle(int x, int y, int w, int h) {
    zremain = rd32();
    zin_pos = zin_len = 0;
    zavail = 0;
    uint16_t pal[128];
    for (int ty = y; ty < y + h; ty += 64) {
        int th = y + h - ty < 64 ? y + h - ty : 64;
        for (int tx = x; tx < x + w; tx += 64) {
            int tw = x + w - tx < 64 ? x + w - tx : 64;
            int total = tw * th;
            uint8_t sub = zb();
            if (sub == 0) {
                zread(rowbuf, (size_t)total * bpp);
                put_raw(tx, ty, tw, th, rowbuf, tw);
            } else if (sub == 1) {
                fill_rect(tx, ty, tw, th, zpix());
            } else if (sub <= 16) {
                int p = sub;
                for (int i = 0; i < p; i++) pal[i] = zpix();
                int bits = p == 2 ? 1 : (p <= 4 ? 2 : 4);
                int rowbytes = (tw * bits + 7) / 8;
                uint8_t mask = (uint8_t)((1 << bits) - 1);
                for (int r = 0; r < th; r++) {
                    uint8_t packed[32];
                    zread(packed, rowbytes);
                    for (int c = 0; c < tw; c++) {
                        int bit = c * bits;
                        int idx = (packed[bit >> 3] >> (8 - bits - (bit & 7))) & mask;
                        tile[r * tw + c] = pal[idx < p ? idx : 0];
                    }
                }
                put_px16(tx, ty, tw, th, tile, tw);
            } else if (sub == 128 || sub >= 130) {
                int p = sub == 128 ? 0 : sub - 128;
                for (int i = 0; i < p; i++) pal[i] = zpix();
                int i = 0;
                while (i < total) {
                    uint16_t c;
                    int len = 1;
                    if (p == 0) {
                        c = zpix();
                        uint8_t b;
                        do {
                            b = zb();
                            len += b;
                        } while (b == 255);
                    } else {
                        uint8_t idx = zb();
                        c = pal[(idx & 127) < p ? (idx & 127) : 0];
                        if (idx & 128) {
                            uint8_t b;
                            do {
                                b = zb();
                                len += b;
                            } while (b == 255);
                        }
                    }
                    if (len > total - i) fail("ZRLE run overflow");
                    for (int k = 0; k < len; k++) tile[i + k] = c;
                    i += len;
                }
                put_px16(tx, ty, tw, th, tile, tw);
            } else {
                fail("Bad ZRLE subencoding %d", sub);
            }
        }
    }
    zdrain();
}

// ------------------------------------------------------------------ server messages

static void handle_fb_update(void) {
    rd8();
    int n = rd16();
    bool full = false;
    for (int i = 0; i < n; i++) {
        int x = rd16(), y = rd16(), w = rd16(), h = rd16();
        int32_t enc = (int32_t)rd32();
        if (enc == -224) break;  // LastRect
        if (enc == -223) {       // DesktopSize
            if (w < 1 || h < 1 || w > MAX_FB_DIM || h > MAX_FB_DIM) fail("Desktop size %dx%d unsupported", w, h);
            fbw = w;
            fbh = h;
            mx = clampi(mx, 0, fbw - 1);
            my = clampi(my, 0, fbh - 1);
            layout();
            cur_x = cur_y = -100;
            update_cursor();
            redraw_all();
            full = true;
            continue;
        }
        if (x + w > fbw || y + h > fbh) fail("Rectangle outside framebuffer");
        switch (enc) {
        case 0: dec_raw(x, y, w, h); break;
        case 1: dec_copyrect(x, y, w, h); break;
        case 2: dec_rre(x, y, w, h); break;
        case 5: dec_hextile(x, y, w, h); break;
        case 16: dec_zrle(x, y, w, h); break;
        default: fail("Unsupported encoding %ld", (long)enc);
        }
    }
    flush_dirty();
    if (full) send_update_request(false, vx, vy, vw, vh);
    send_update_request(true, vx, vy, vw, vh);
}

// ------------------------------------------------------------------ input

static void sync_mods(void) {
    if (kbd_ctrl() != ctrl_sent) {
        ctrl_sent = !ctrl_sent;
        send_key(XK_Control_L, ctrl_sent);
    }
    if (kbd_alt() != alt_sent) {
        alt_sent = !alt_sent;
        send_key(XK_Alt_L, alt_sent);
    }
}

static void release_all(void) {
    if (buttons) {
        buttons = 0;
        send_pointer_now(0);
    }
    if (ctrl_sent) send_key(XK_Control_L, false);
    if (alt_sent) send_key(XK_Alt_L, false);
    ctrl_sent = alt_sent = false;
    memset(held, 0, sizeof(held));
}

static void move_mouse(int dlx, int dly) {
    // convert LCD pixels to remote pixels
    int rdx = dlx * vw / lw, rdy = dly * vh / lh;
    if (dlx && !rdx) rdx = dlx > 0 ? 1 : -1;
    if (dly && !rdy) rdy = dly > 0 ? 1 : -1;
    int nx = clampi(mx + rdx, 0, fbw - 1), ny = clampi(my + rdy, 0, fbh - 1);
    if (nx == mx && ny == my) return;
    mx = nx;
    my = ny;
    ptr_dirty = true;
    update_cursor();
    if (vw < fbw || vh < fbh) want_pan = true;
}

static const int8_t dir_dx[4] = {-1, 0, 0, 1}, dir_dy[4] = {0, -1, 1, 0};

static int arrow_dir(uint8_t k) {
    switch (k) {
    case KEY_LEFT: return 0;
    case KEY_UP: return 1;
    case KEY_DOWN: return 2;
    case KEY_RIGHT: return 3;
    default: return -1;
    }
}

static uint32_t map_key(uint8_t k) {
    if (k >= 0x20 && k < 0x7F) {
        if ((kbd_ctrl() || kbd_alt()) && k >= 'A' && k <= 'Z') return k + 32;
        return k;
    }
    switch (k) {
    case KEY_ENTER:
    case KEY_RETURN: return XK_Return;
    case KEY_BACKSPACE: return XK_BackSpace;
    case KEY_TAB: return XK_Tab;
    case KEY_ESC: return XK_Escape;
    case KEY_DEL: return XK_Delete;
    case KEY_INSERT: return XK_Insert;
    case KEY_HOME: return XK_Home;
    case KEY_END: return XK_End;
    case KEY_PGUP: return XK_Page_Up;
    case KEY_PGDN: return XK_Page_Down;
    case KEY_BREAK: return XK_Break;
    case KEY_LEFT: return XK_Left;
    case KEY_UP: return XK_Up;
    case KEY_DOWN: return XK_Down;
    case KEY_RIGHT: return XK_Right;
    default: return 0;
    }
}

static void button(uint8_t mask, uint8_t state) {
    if (state == KEY_STATE_PRESSED && !(buttons & mask)) {
        sync_mods();
        buttons |= mask;
        send_pointer_now(buttons);
    } else if (state == KEY_STATE_RELEASED && (buttons & mask)) {
        buttons &= ~mask;
        send_pointer_now(buttons);
    }
}

static void wheel(uint8_t mask) {
    sync_mods();
    send_pointer_now(buttons | mask);
    send_pointer_now(buttons);
}

static void handle_key(const kev_t *e) {
    uint8_t k = e->key, st = e->state;
    uint64_t now = time_us_64();
    if (kbd_is_mod(k)) {
        if (st == KEY_STATE_RELEASED) {
            if (k == KEY_MOD_CTRL && ctrl_sent) { send_key(XK_Control_L, false); ctrl_sent = false; }
            if (k == KEY_MOD_ALT && alt_sent) { send_key(XK_Alt_L, false); alt_sent = false; }
        }
        return;
    }
    int d = arrow_dir(k);
    if (d >= 0 && arrow_mouse) {
        if (st == KEY_STATE_RELEASED) {
            held[d] = false;
        } else {
            if (!held[d]) {
                held[d] = true;
                held_start[d] = now;
                move_mouse(dir_dx[d], dir_dy[d]);
            }
            held_last[d] = now;
        }
        return;
    }
    switch (k) {
    case KEY_F4: button(1, st); return;
    case KEY_F5: button(4, st); return;
    default: break;
    }
    if (st != KEY_STATE_PRESSED) return;
    switch (k) {
    case KEY_F1: want_help = true; return;
    case KEY_F2: want_menu = true; return;
    case KEY_F3: want_disc = true; return;
    case KEY_F6:
        arrow_mouse = !arrow_mouse;
        memset(held, 0, sizeof(held));
        return;
    case KEY_F7: want_zoom = true; return;
    case KEY_F8: want_refresh = true; return;
    case KEY_F9: wheel(8); return;
    case KEY_F10: wheel(16); return;
    default: break;
    }
    uint32_t ks = map_key(k);
    if (!ks) return;
    sync_mods();
    send_key(ks, true);
    send_key(ks, false);
}

static void mouse_tick(void) {
    uint64_t now = time_us_64();
    if (now - last_move_us < 20000) return;
    last_move_us = now;
    int dx = 0, dy = 0;
    for (int d = 0; d < 4; d++) {
        if (!held[d]) continue;
        if (now - held_last[d] > 3000000) { // lost release event
            held[d] = false;
            continue;
        }
        uint64_t t = now - held_start[d];
        if (t < 200000) continue;
        int step = 1 + (int)((t - 200000) / 150000);
        if (step > 6) step = 6;
        dx += dir_dx[d] * step;
        dy += dir_dy[d] * step;
    }
    if (dx || dy) move_mouse(dx, dy);
}

static bool session_idle(void) {
    kev_t e;
    while (kbd_get(&e)) handle_key(&e);
    mouse_tick();
    uint64_t now = time_us_64();
    if (ptr_dirty && now - last_ptr_us >= 25000) send_pointer_now(buttons);
    if (dirty && now - last_flush_us >= 120000) flush_dirty();
    return true;
}

static bool connect_idle(void) {
    return !ui_cancel_requested();
}

// ------------------------------------------------------------------ deferred actions

static void do_pan(void) {
    want_pan = false;
    int mgx = vw / 10, mgy = vh / 10;
    int nvx = vx, nvy = vy;
    if (mx < vx + mgx) nvx = mx - vw / 3;
    else if (mx >= vx + vw - mgx) nvx = mx - vw * 2 / 3;
    if (my < vy + mgy) nvy = my - vh / 3;
    else if (my >= vy + vh - mgy) nvy = my - vh * 2 / 3;
    nvx = clampi(nvx, 0, fbw - vw);
    nvy = clampi(nvy, 0, fbh - vh);
    if (nvx == vx && nvy == vy) return;
    // Shift existing content for immediate feedback, then request the new view.
    int sdx = (vx - nvx) * lw / vw, sdy = (vy - nvy) * lh / vh;
    for (int i = 0; i < lh; i++) {
        int ly = sdy > 0 ? lh - 1 - i : i;
        int sy = ly - sdy;
        uint16_t *dst = &fb[(oy + ly) * LCD_W + ox];
        if (sy < 0 || sy >= lh) {
            for (int x = 0; x < lw; x++) dst[x] = 0x2104;
            continue;
        }
        const uint16_t *src = &fb[(oy + sy) * LCD_W + ox];
        for (int x = 0; x < lw; x++) {
            int sx = x - sdx;
            line[x] = (sx >= 0 && sx < lw) ? src[sx] : 0x2104;
        }
        memcpy(dst, line, lw * 2);
    }
    vx = nvx;
    vy = nvy;
    build_maps();
    cur_x = cur_y = -100;
    update_cursor();
    flush_rect(ox, oy, ox + lw, oy + lh);
    refresh_pending = false;
    send_update_request(false, vx, vy, vw, vh);
}

static void after_modal(void) {
    kbd_flush();
    ui_suspend_status();
    redraw_all();
}

static void set_zoom(int z) {
    zoom = z;
    layout();
    cur_x = cur_y = -100;
    update_cursor();
    redraw_all();
    refresh_pending = false;
    send_update_request(false, vx, vy, vw, vh);
}

typedef struct {
    const char *label;
    uint32_t ks[3];
} combo_t;

static const combo_t combos[] = {
    {"Ctrl+Alt+Del", {XK_Control_L, XK_Alt_L, XK_Delete}},
    {"Ctrl+Shift+Esc", {XK_Control_L, XK_Shift_L, XK_Escape}},
    {"Alt+Tab", {XK_Alt_L, XK_Tab}},
    {"Alt+F4", {XK_Alt_L, XK_F1 + 3}},
    {"Ctrl+Esc", {XK_Control_L, XK_Escape}},
    {"Windows / Super", {XK_Super_L}},
    {"Esc", {XK_Escape}},
    {"Tab", {XK_Tab}},
    {"Print Screen", {XK_Print}},
    {"F1", {XK_F1}}, {"F2", {XK_F1 + 1}}, {"F3", {XK_F1 + 2}}, {"F4", {XK_F1 + 3}},
    {"F5", {XK_F1 + 4}}, {"F6", {XK_F1 + 5}}, {"F7", {XK_F1 + 6}}, {"F8", {XK_F1 + 7}},
    {"F9", {XK_F1 + 8}}, {"F10", {XK_F1 + 9}}, {"F11", {XK_F1 + 10}}, {"F12", {XK_F1 + 11}},
};
#define NCOMBO ((int)(sizeof(combos) / sizeof(combos[0])))

static const char *const zoom_names[ZOOM_COUNT] = {"Fit", "50%", "100%"};

// Returns true if the user chose to disconnect
static bool special_menu(void) {
    static int last = 0;
    enum { A_MIDDLE = NCOMBO, A_WUP, A_WDOWN, A_ARROWS, A_ZOOM, A_REFRESH, A_SETTINGS, A_DISC, A_N };
    const char *items[A_N];
    char arrows[40], zoomlbl[40];
    for (int i = 0; i < NCOMBO; i++) items[i] = combos[i].label;
    items[A_MIDDLE] = "Middle click";
    items[A_WUP] = "Scroll wheel up";
    items[A_WDOWN] = "Scroll wheel down";
    snprintf(arrows, sizeof(arrows), "Arrow keys: %s", arrow_mouse ? "mouse -> keys" : "keys -> mouse");
    items[A_ARROWS] = arrows;
    snprintf(zoomlbl, sizeof(zoomlbl), "Zoom: %s -> %s", zoom_names[zoom], zoom_names[(zoom + 1) % ZOOM_COUNT]);
    items[A_ZOOM] = zoomlbl;
    items[A_REFRESH] = "Refresh screen";
    items[A_SETTINGS] = "Brightness settings";
    items[A_DISC] = "Disconnect";

    release_all();
    int r = ui_menu("Send special keys", items, NULL, A_N, last);
    after_modal();
    if (r < 0) return false;
    last = r;
    if (r < NCOMBO) {
        const combo_t *c = &combos[r];
        int n = 0;
        while (n < 3 && c->ks[n]) n++;
        for (int i = 0; i < n; i++) send_key(c->ks[i], true);
        for (int i = n - 1; i >= 0; i--) send_key(c->ks[i], false);
        return false;
    }
    switch (r) {
    case A_MIDDLE:
        send_pointer_now(2);
        send_pointer_now(0);
        break;
    case A_WUP: wheel(8); break;
    case A_WDOWN: wheel(16); break;
    case A_ARROWS: arrow_mouse = !arrow_mouse; break;
    case A_ZOOM: set_zoom((zoom + 1) % ZOOM_COUNT); break;
    case A_REFRESH: send_update_request(false, vx, vy, vw, vh); break;
    case A_SETTINGS:
        settings_screen();
        after_modal();
        break;
    case A_DISC: return true;
    }
    return false;
}

// Returns true when the session should end
static bool run_deferred(const vnc_entry_t *e) {
    if (want_help) {
        want_help = false;
        release_all();
        ui_help(HELP_SESSION);
        after_modal();
    }
    if (want_menu) {
        want_menu = false;
        if (special_menu()) return true;
    }
    if (want_disc) {
        want_disc = false;
        release_all();
        char msg[96];
        snprintf(msg, sizeof(msg), "Disconnect from %s?", e->name);
        bool yes = ui_confirm("Disconnect", msg);
        after_modal();
        if (yes) return true;
    }
    if (want_zoom) {
        want_zoom = false;
        set_zoom((zoom + 1) % ZOOM_COUNT);
    }
    if (want_refresh) {
        want_refresh = false;
        send_update_request(false, vx, vy, vw, vh);
    }
    if (want_pan) do_pan();
    if (refresh_pending) {
        refresh_pending = false;
        int x0 = clampi(rf_x0, vx, vx + vw), x1 = clampi(rf_x1, vx, vx + vw);
        int y0 = clampi(rf_y0, vy, vy + vh), y1 = clampi(rf_y1, vy, vy + vh);
        if (x1 > x0 && y1 > y0) send_update_request(false, x0, y0, x1 - x0, y1 - y0);
    }
    return false;
}

// ------------------------------------------------------------------ handshake

static void handshake(const vnc_entry_t *e) {
    char ver[13];
    rd(ver, 12);
    ver[12] = 0;
    int major = 0, minor = 0;
    if (sscanf(ver, "RFB %03d.%03d", &major, &minor) != 2 || major != 3)
        fail("Not a VNC server (bad version)");
    if (minor >= 8) minor = 8;
    else if (minor >= 7) minor = 7;
    else minor = 3;
    char reply[13];
    snprintf(reply, sizeof(reply), "RFB 003.%03d\n", minor);
    wr(reply, 12);

    uint32_t sec = 0;
    if (minor == 3) {
        sec = rd32();
        if (sec == 0) read_reason("Connection refused");
        if (sec != 1 && sec != 2) fail("Unsupported security type %lu", (unsigned long)sec);
    } else {
        int n = rd8();
        if (n == 0) read_reason("Connection refused");
        uint8_t types[255];
        rd(types, n);
        bool has1 = false, has2 = false;
        char list[64] = "";
        for (int i = 0; i < n; i++) {
            if (types[i] == 1) has1 = true;
            if (types[i] == 2) has2 = true;
            size_t l = strlen(list);
            if (l < sizeof(list) - 6) snprintf(list + l, sizeof(list) - l, "%s%d", i ? "," : "", types[i]);
        }
        if (has1) sec = 1;
        else if (has2) sec = 2;
        else fail("Server requires an unsupported security type (%s). Enable 'VNC password' or 'None' "
                  "authentication on the server.", list);
        uint8_t s = (uint8_t)sec;
        wr(&s, 1);
    }

    if (sec == 2) {
        uint8_t ch[16], resp[16];
        rd(ch, 16);
        char pw[64];
        snprintf(pw, sizeof(pw), "%s", e->pass);
        if (!pw[0]) {
            if (!ui_input("VNC password", "Password", pw, sizeof(pw), true)) longjmp(jb, ERR_USER);
            ui_progress("Connecting", "Authenticating...");
        }
        vnc_des_response(pw, ch, resp);
        wr(resp, 16);
    }
    if (sec == 2 || minor == 8) {
        uint32_t res = rd32();
        if (res != 0) {
            if (minor == 8) read_reason("Authentication failed");
            fail("Authentication failed (wrong password?)");
        }
    }

    uint8_t shared = 1;
    wr(&shared, 1);
    fbw = rd16();
    fbh = rd16();
    skip(16);  // server pixel format
    uint32_t nlen = rd32();
    skip(nlen);
    if (fbw < 1 || fbh < 1 || fbw > MAX_FB_DIM || fbh > MAX_FB_DIM)
        fail("Desktop size %dx%d unsupported", fbw, fbh);

    // SetPixelFormat
    uint8_t pf[20] = {0, 0, 0, 0};
    if (bpp == 2) {
        const uint8_t f[16] = {16, 16, 0, 1, 0, 31, 0, 63, 0, 31, 11, 5, 0, 0, 0, 0};
        memcpy(pf + 4, f, 16);
    } else {
        const uint8_t f[16] = {8, 8, 0, 1, 0, 7, 0, 7, 0, 3, 5, 2, 0, 0, 0, 0};
        memcpy(pf + 4, f, 16);
    }
    wr(pf, sizeof(pf));

    // SetEncodings
    static const int32_t encs[] = {16, 5, 1, 2, 0, -223, -224};
    uint8_t m[4 + sizeof(encs)];
    m[0] = 2;
    m[1] = 0;
    m[2] = 0;
    m[3] = sizeof(encs) / 4;
    for (size_t i = 0; i < sizeof(encs) / 4; i++) {
        uint32_t v = (uint32_t)encs[i];
        m[4 + i * 4] = v >> 24;
        m[5 + i * 4] = v >> 16;
        m[6 + i * 4] = v >> 8;
        m[7 + i * 4] = v;
    }
    wr(m, sizeof(m));
}

// ------------------------------------------------------------------ session

bool vnc_session(vnc_entry_t *e, char *err, int errlen) {
    static volatile bool connected;
    connected = false;
    err[0] = 0;
    errmsg[0] = 0;
    bpp = e->depth == 8 ? 1 : 2;
    zoom = e->zoom % ZOOM_COUNT;
    for (int i = 0; i < 256; i++) {
        int r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        lut8[i] = RGB565(r * 255 / 7, g * 255 / 7, b * 255 / 3);
    }
    buttons = 0;
    ptr_dirty = dirty = false;
    ctrl_sent = alt_sent = false;
    memset(held, 0, sizeof(held));
    want_help = want_menu = want_disc = want_zoom = want_refresh = want_pan = false;
    refresh_pending = false;
    arrow_mouse = true;
    zreset();

    char msg[120];
    snprintf(msg, sizeof(msg), "Connecting to %s:%u ...", e->host, e->port);
    ui_progress("Connecting", msg);

    int code = setjmp(jb);
    if (code == 0) {
        int r = net_connect(e->host, e->port, connect_idle);
        if (r) longjmp(jb, r);
        handshake(e);
        connected = true;

        ui_progress_end();
        net_set_idle(session_idle);
        ui_suspend_status();
        kbd_flush();
        mx = fbw / 2;
        my = fbh / 2;
        layout();
        cur_x = cur_y = -100;
        update_cursor();
        redraw_all();
        send_update_request(false, vx, vy, vw, vh);

        for (;;) {
            if (run_deferred(e)) break;
            if (!net_available()) {
                if (!net_is_open()) longjmp(jb, NET_ERR_CLOSED);
                if (!wifi_connected()) fail("WiFi connection lost");
                session_idle();
                sys_poll();
                continue;
            }
            uint8_t type = rd8();
            switch (type) {
            case 0: handle_fb_update(); break;
            case 1: {
                uint8_t b[5];
                rd(b, 5);
                skip((size_t)(b[3] << 8 | b[4]) * 6);
                break;
            }
            case 2: break;  // Bell
            case 3: {
                uint8_t b[3];
                rd(b, 3);
                uint32_t len = rd32();
                if (len & 0x80000000u) len = (uint32_t)(-(int32_t)len);
                skip(len);
                break;
            }
            default: fail("Unknown server message %d", type);
            }
        }
        code = 0;
    }

    ui_progress_end();
    net_set_idle(NULL);
    if (code == 0 || code == ERR_USER || code == NET_ERR_ABORTED) {
        if (connected) {
            int dummy = setjmp(jb);
            if (dummy == 0) release_all();
        }
        net_close();
        e->zoom = (uint8_t)zoom;
        kbd_flush();
        return true;
    }
    net_close();
    e->zoom = (uint8_t)zoom;
    kbd_flush();
    if (code == ERR_PROTO) snprintf(err, errlen, "%s", errmsg);
    else snprintf(err, errlen, "%s%s", net_strerror(code), connected ? "" : " (while connecting)");
    return false;
}
