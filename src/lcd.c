// ST7365P LCD driver for the PicoCalc (RGB565 over 4-wire SPI)
#include "lcd.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"

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

void lcd_begin(int x, int y, int w, int h) {
    int x1 = x + w - 1, y1 = y + h - 1;
    lcd_cmd_data(0x2A, (const uint8_t[]){x >> 8, x & 0xFF, x1 >> 8, x1 & 0xFF}, 4);
    lcd_cmd_data(0x2B, (const uint8_t[]){y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF}, 4);
    lcd_cmd(0x2C);
    spi_set_format(LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
}

void lcd_push(const uint16_t *px, size_t n) {
    spi_write16_blocking(LCD_SPI, px, n);
}

void lcd_end(void) {
    lcd_deselect();
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
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
