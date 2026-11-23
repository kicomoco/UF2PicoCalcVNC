#include "kbd.h"
#include "board.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"

#define SB_REG_FIF 0x09
#define SB_REG_BAT 0x0B
#define QSIZE 64

// Single-producer (core 1) / single-consumer (core 0) ring buffer.
static volatile uint16_t q[QSIZE];
static volatile uint32_t q_head, q_tail;
static volatile int battery_raw = -1;

static bool mod_ctrl, mod_alt, mod_shl, mod_shr;

static bool sb_read_reg(uint8_t reg, uint8_t out[2]) {
    if (i2c_write_timeout_us(SB_I2C, SB_ADDR, &reg, 1, false, 20000) != 1) return false;
    sleep_us(500);
    return i2c_read_timeout_us(SB_I2C, SB_ADDR, out, 2, false, 20000) == 2;
}

static void core1_main(void) {
    i2c_init(SB_I2C, SB_BAUD);
    gpio_set_function(SB_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SB_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SB_PIN_SDA);
    gpio_pull_up(SB_PIN_SCL);

    absolute_time_t next_bat = get_absolute_time();
    for (;;) {
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
