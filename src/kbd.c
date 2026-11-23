#include "kbd.h"
#include <stdatomic.h>
#include "board.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"

#define SB_REG_FIF 0x09
#define SB_REG_BAT 0x0B
#define SB_REG_BKL 0x05
#define SB_REG_BK2 0x0A
#define QSIZE 64

// Single-producer (core 1) / single-consumer (core 0) ring buffer.
static volatile uint16_t q[QSIZE];
static volatile uint32_t q_head, q_tail;
static volatile int battery_raw = -1;
static atomic_uint backlight_request;
static uint32_t backlight_response;

static bool mod_ctrl, mod_alt, mod_shl, mod_shr;

static bool sb_read_reg(uint8_t reg, uint8_t out[2]) {
    if (i2c_write_timeout_us(SB_I2C, SB_ADDR, &reg, 1, false, 20000) != 1) return false;
    sleep_us(500);
    return i2c_read_timeout_us(SB_I2C, SB_ADDR, out, 2, false, 20000) == 2;
}

static bool sb_write_backlight(uint8_t reg, uint8_t value) {
    uint8_t command[2] = {reg | 0x80, value}, reply[2];
    if (i2c_write_timeout_us(SB_I2C, SB_ADDR, command, sizeof(command), false, 20000) != 2)
        return false;
    sleep_us(500);
    return i2c_read_timeout_us(SB_I2C, SB_ADDR, reply, sizeof(reply), false, 20000) == 2 &&
           reply[0] == reg && reply[1] == value;
}

static void service_backlights(void) {
    unsigned request = atomic_load_explicit(&backlight_request, memory_order_acquire);
    if (!request) return;
    uint8_t screen[2], keyboard[2];
    bool ok;
    if ((request & 0xFF) == 1) {
        ok = sb_read_reg(SB_REG_BKL, screen) && screen[0] == SB_REG_BKL &&
             sb_read_reg(SB_REG_BK2, keyboard) && keyboard[0] == SB_REG_BK2;
        backlight_response = ok ? 0x10000u | (uint32_t)screen[1] << 8 | keyboard[1] : 0;
    } else {
        ok = true;
        if (request & 0x1000000)
            ok = sb_write_backlight(SB_REG_BKL, (uint8_t)(request >> 8));
        if (ok && (request & 0x2000000))
            ok = sb_write_backlight(SB_REG_BK2, (uint8_t)(request >> 16));
        backlight_response = ok ? 0x10000 : 0;
    }
    atomic_store_explicit(&backlight_request, 0, memory_order_release);
}

static void core1_main(void) {
    i2c_init(SB_I2C, SB_BAUD);
    gpio_set_function(SB_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SB_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SB_PIN_SDA);
    gpio_pull_up(SB_PIN_SCL);

    absolute_time_t next_bat = get_absolute_time();
    for (;;) {
        service_backlights();
        uint8_t b[2];
        if (sb_read_reg(SB_REG_FIF, b) && b[0] != 0 && b[1] != 0) {
            uint32_t h = q_head;
            if (h - q_tail < QSIZE) {
                q[h % QSIZE] = (uint16_t)(b[0] << 8 | b[1]);
                __dmb();
                q_head = h + 1;
            }
            continue; // drain the FIFO quickly
        }
        if (time_reached(next_bat)) {
            if (sb_read_reg(SB_REG_BAT, b)) battery_raw = b[1];
            next_bat = make_timeout_time_ms(5000);
        }
        sleep_ms(8);
    }
}

void kbd_init(void) {
    multicore_launch_core1(core1_main);
}

static bool backlight_transfer(unsigned request, uint32_t *response) {
    if (atomic_load_explicit(&backlight_request, memory_order_acquire)) return false;
    atomic_store_explicit(&backlight_request, request, memory_order_release);
    absolute_time_t deadline = make_timeout_time_ms(500);
    while (atomic_load_explicit(&backlight_request, memory_order_acquire)) {
        if (time_reached(deadline)) return false;
        sleep_ms(1);
    }
    *response = backlight_response;
    return (*response & 0x10000) != 0;
}

bool kbd_get_brightness(uint8_t *screen, uint8_t *keyboard) {
    uint32_t response;
    if (!backlight_transfer(1, &response)) return false;
    unsigned s = ((response >> 8) & 0xFF) * 10u;
    unsigned k = (response & 0xFF) * 10u;
    s = (s + 120) / 240;
    k = (k + 112) / 224;
    *screen = (uint8_t)((s < 1 ? 1 : s > 10 ? 10 : s) * 10);
    *keyboard = (uint8_t)((k > 10 ? 10 : k) * 10);
    return true;
}

bool kbd_set_brightness(int screen, int keyboard) {
    if ((screen != -1 && (screen < 10 || screen > 100)) || keyboard < -1 || keyboard > 100)
        return false;
    if (screen == -1 && keyboard == -1) return true;
    // Stock firmware uses 16-step LCD and 32-step keyboard PWM, capped at 240.
    unsigned request = 2;
    if (screen >= 0) {
        unsigned value = ((unsigned)screen * 240 + 800) / 1600 * 16;
        request |= 0x1000000 | value << 8;
    }
    if (keyboard >= 0) {
        unsigned value = ((unsigned)keyboard * 224 + 1600) / 3200 * 32;
        if (keyboard && !value) value = 32;
        request |= 0x2000000 | value << 16;
    }
    uint32_t response;
    return backlight_transfer(request, &response);
}

bool kbd_get(kev_t *ev) {
    uint32_t t = q_tail;
    if (t == q_head) return false;
    __dmb();
    uint16_t v = q[t % QSIZE];
    q_tail = t + 1;
    ev->state = v >> 8;
    ev->key = v & 0xFF;
    bool down = ev->state != KEY_STATE_RELEASED;
    switch (ev->key) {
    case KEY_MOD_CTRL: mod_ctrl = down; break;
    case KEY_MOD_ALT: mod_alt = down; break;
    case KEY_MOD_SHL: mod_shl = down; break;
    case KEY_MOD_SHR: mod_shr = down; break;
    default: break;
    }
    return true;
}

void kbd_flush(void) {
    kev_t e;
    while (kbd_get(&e)) {}
}

bool kbd_ctrl(void) { return mod_ctrl; }
bool kbd_alt(void) { return mod_alt; }
bool kbd_shift(void) { return mod_shl || mod_shr; }

int kbd_battery(void) {
    int b = battery_raw;
    return b < 0 ? -1 : (b & 0x7F);
}

bool kbd_charging(void) {
    int b = battery_raw;
    return b >= 0 && (b & 0x80);
}
