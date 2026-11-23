// ST7365P LCD driver for the PicoCalc (RGB565 over 4-wire SPI)
#include "lcd.h"
#include <stdatomic.h>
#include "board.h"
#include "font.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/spi.h"

static mutex_t lcd_mutex;
// SPI transactions and spinner state share this mutex across the two cores.
static atomic_bool spinner_enabled;
static int spinner_x, spinner_y;
static uint16_t spinner_fg, spinner_bg;
static uint64_t spinner_frame;

static void lcd_deselect(void) {
    gpio_put(LCD_PIN_CS, 1);
    // The controller requires a minimum CS-high pulse between transactions.
    busy_wait_us(1);
}

static void lcd_cmd(uint8_t c) {
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI, &c, 1);
    lcd_deselect();
}

static void lcd_data(const uint8_t *d, size_t n) {
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI, d, n);
    lcd_deselect();
}

static void lcd_cmd_data(uint8_t c, const uint8_t *d, size_t n) {
    lcd_cmd(c);
    if (n) lcd_data(d, n);
}

void lcd_init(void) {
    mutex_init(&lcd_mutex);
    spi_init(LCD_SPI, LCD_BAUD);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_SPI);

    gpio_init(LCD_PIN_CS);
    gpio_init(LCD_PIN_DC);
    gpio_init(LCD_PIN_RST);
    gpio_put(LCD_PIN_CS, 1);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_RST, 1);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);

    sleep_ms(5);
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(10);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(120);

    lcd_cmd(0x01);                              // SWRESET
    sleep_ms(120);
    lcd_cmd_data(0x3A, (const uint8_t[]){0x55}, 1);  // COLMOD: 16 bpp
    lcd_cmd_data(0x36, (const uint8_t[]){0x48}, 1);  // MADCTL: MX | BGR
    lcd_cmd(0x21);                              // INVON (IPS panel)
    lcd_cmd_data(0xB7, (const uint8_t[]){0xC6}, 1);  // entry mode
    lcd_cmd_data(0x33, (const uint8_t[]){0, 0, 1, 0x40, 0, 0}, 6);
    lcd_cmd_data(0x37, (const uint8_t[]){0, 0}, 2);
    lcd_cmd(0x11);                              // SLPOUT
    sleep_ms(120);
    lcd_fill(0, 0, LCD_W, LCD_H, 0);
    lcd_cmd(0x29);                              // DISPON
    sleep_ms(20);
}

static void lcd_begin_locked(int x, int y, int w, int h) {
    int x1 = x + w - 1, y1 = y + h - 1;
    lcd_cmd_data(0x2A, (const uint8_t[]){x >> 8, x & 0xFF, x1 >> 8, x1 & 0xFF}, 4);
    lcd_cmd_data(0x2B, (const uint8_t[]){y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF}, 4);
    lcd_cmd(0x2C);
    spi_set_format(LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
}

void lcd_begin(int x, int y, int w, int h) {
    mutex_enter_blocking(&lcd_mutex);
    lcd_begin_locked(x, y, w, h);
}

void lcd_push(const uint16_t *px, size_t n) {
    spi_write16_blocking(LCD_SPI, px, n);
}

static void lcd_end_locked(void) {
    lcd_deselect();
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

void lcd_end(void) {
    lcd_end_locked();
    mutex_exit(&lcd_mutex);
}

void lcd_spinner_start(int x, int y, uint16_t fg, uint16_t bg) {
    mutex_enter_blocking(&lcd_mutex);
    spinner_x = x;
    spinner_y = y;
    spinner_fg = fg;
    spinner_bg = bg;
    spinner_frame = time_us_64() / 200000;
    atomic_store_explicit(&spinner_enabled, true, memory_order_release);
    mutex_exit(&lcd_mutex);
}

void lcd_spinner_stop(void) {
    if (!atomic_load_explicit(&spinner_enabled, memory_order_acquire)) return;
    mutex_enter_blocking(&lcd_mutex);
    atomic_store_explicit(&spinner_enabled, false, memory_order_release);
    mutex_exit(&lcd_mutex);
}

void lcd_spinner_service(void) {
    if (!atomic_load_explicit(&spinner_enabled, memory_order_acquire)) return;
    uint64_t frame = time_us_64() / 200000;
    if (!mutex_try_enter(&lcd_mutex, NULL)) return;
    if (atomic_load_explicit(&spinner_enabled, memory_order_relaxed) && frame != spinner_frame) {
        spinner_frame = frame;
        const uint8_t *glyph = font_glyph(&font_6x12, "|/-\\"[frame % 4]);
        uint16_t pixels[6 * 12];
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 6; x++)
                pixels[y * 6 + x] = (glyph[y] & (0x80 >> x)) ? spinner_fg : spinner_bg;
        lcd_begin_locked(spinner_x, spinner_y, 6, 12);
        lcd_push(pixels, 6 * 12);
        lcd_end_locked();
    }
    mutex_exit(&lcd_mutex);
}

static bool clip(int *x, int *y, int *w, int *h) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > LCD_W) *w = LCD_W - *x;
    if (*y + *h > LCD_H) *h = LCD_H - *y;
    return *w > 0 && *h > 0;
}

void lcd_blit(const uint16_t *px, int stride, int x, int y, int w, int h) {
    int ox = x, oy = y;
    if (!clip(&x, &y, &w, &h)) return;
    px += (y - oy) * stride + (x - ox);
    lcd_begin(x, y, w, h);
    for (int r = 0; r < h; r++) lcd_push(px + r * stride, w);
    lcd_end();
}

void lcd_fill(int x, int y, int w, int h, uint16_t c) {
    static uint16_t line[LCD_W];
    if (!clip(&x, &y, &w, &h)) return;
    for (int i = 0; i < w; i++) line[i] = c;
    lcd_begin(x, y, w, h);
    for (int r = 0; r < h; r++) lcd_push(line, w);
    lcd_end();
}
