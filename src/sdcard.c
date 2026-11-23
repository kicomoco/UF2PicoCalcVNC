// Minimal SD card driver (SPI mode, SDv1/SDv2/SDHC/SDXC)
#include "sdcard.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"

static bool ready;
static bool block_addr; // SDHC/SDXC use block addressing

static uint8_t xfer(uint8_t b) {
    uint8_t r;
    spi_write_read_blocking(SD_SPI, &b, &r, 1);
    return r;
}

static void deselect(void) {
    gpio_put(SD_PIN_CS, 1);
    xfer(0xFF);
}

static bool wait_ready(uint32_t ms) {
    absolute_time_t to = make_timeout_time_ms(ms);
    do {
        if (xfer(0xFF) == 0xFF) return true;
    } while (!time_reached(to));
    return false;
}

static bool select(void) {
    gpio_put(SD_PIN_CS, 0);
    xfer(0xFF);
    if (wait_ready(500)) return true;
    deselect();
    return false;
}

static uint8_t cmd(uint8_t c, uint32_t arg) {
    if (c & 0x80) { // ACMD
        c &= 0x7F;
        uint8_t r = cmd(55, 0);
        if (r > 1) return r;
    }
    if (c != 12) {
        deselect();
        if (!select()) return 0xFF;
    }
    uint8_t pkt[6] = {0x40 | c, arg >> 24, arg >> 16, arg >> 8, arg, 0x01};
    if (c == 0) pkt[5] = 0x95;
    if (c == 8) pkt[5] = 0x87;
    spi_write_blocking(SD_SPI, pkt, 6);
    if (c == 12) xfer(0xFF);
    uint8_t r;
    int n = 10;
    do {
        r = xfer(0xFF);
    } while ((r & 0x80) && --n);
    return r;
}

#define ACMD41 (0x80 | 41)

bool sd_init(void) {
    ready = false;
    spi_init(SD_SPI, SD_INIT_BAUD);
    spi_set_format(SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_pull_up(SD_PIN_MISO);
    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS, 1);

    for (int i = 0; i < 10; i++) xfer(0xFF);

    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && r != 1; i++) r = cmd(0, 0);
    if (r != 1) { deselect(); return false; }

    bool v2 = false;
    absolute_time_t to = make_timeout_time_ms(1500);
    if (cmd(8, 0x1AA) == 1) {
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
        if (ocr[2] != 0x01 || ocr[3] != 0xAA) { deselect(); return false; }
        v2 = true;
        while (cmd(ACMD41, 1UL << 30) != 0) {
            if (time_reached(to)) { deselect(); return false; }
        }
        if (cmd(58, 0) != 0) { deselect(); return false; }
        for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
        block_addr = (ocr[0] & 0x40) != 0;
    } else {
        uint8_t c = (cmd(ACMD41, 0) <= 1) ? ACMD41 : 1;
        while (cmd(c, 0) != 0) {
            if (time_reached(to)) { deselect(); return false; }
        }
        block_addr = false;
    }
    if (!block_addr && cmd(16, 512) != 0) { deselect(); return false; }
    (void)v2;
    deselect();
    spi_set_baudrate(SD_SPI, SD_BAUD);
    ready = true;
    return true;
}

bool sd_ready(void) { return ready; }

static bool rx_block(uint8_t *buf) {
    absolute_time_t to = make_timeout_time_ms(200);
    uint8_t t;
    do {
        t = xfer(0xFF);
    } while (t == 0xFF && !time_reached(to));
    if (t != 0xFE) return false;
    spi_read_blocking(SD_SPI, 0xFF, buf, 512);
    xfer(0xFF);
    xfer(0xFF);
    return true;
}

static bool tx_block(const uint8_t *buf, uint8_t token) {
    if (!wait_ready(500)) return false;
    xfer(token);
    if (token == 0xFD) return true;
    spi_write_blocking(SD_SPI, buf, 512);
    xfer(0xFF);
    xfer(0xFF);
    return (xfer(0xFF) & 0x1F) == 0x05;
}

bool sd_read(uint32_t block, uint8_t *buf, uint32_t count) {
    if (!ready) return false;
    bool ok = true;
    for (uint32_t i = 0; i < count && ok; i++) {
        uint32_t a = block + i;
        if (!block_addr) a *= 512;
        ok = cmd(17, a) == 0 && rx_block(buf + i * 512);
    }
    deselect();
    return ok;
}

bool sd_write(uint32_t block, const uint8_t *buf, uint32_t count) {
    if (!ready) return false;
    bool ok = true;
    for (uint32_t i = 0; i < count && ok; i++) {
        uint32_t a = block + i;
        if (!block_addr) a *= 512;
        ok = cmd(24, a) == 0 && tx_block(buf + i * 512, 0xFE);
    }
    if (ok) ok = wait_ready(500);
    deselect();
    return ok;
}
