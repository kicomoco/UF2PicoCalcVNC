// PicoCalc keyboard (via STM32 south bridge). Polled on core 1 so that slow
// I2C transactions never stall WiFi/VNC processing on core 0.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define KEY_STATE_PRESSED  1
#define KEY_STATE_HOLD     2
#define KEY_STATE_RELEASED 3

#define KEY_BACKSPACE 0x08
#define KEY_TAB       0x09
#define KEY_ENTER     0x0A
#define KEY_RETURN    0x0D
#define KEY_ESC       0xB1
#define KEY_LEFT      0xB4
#define KEY_UP        0xB5
#define KEY_DOWN      0xB6
#define KEY_RIGHT     0xB7
#define KEY_BREAK     0xD0
#define KEY_INSERT    0xD1
#define KEY_HOME      0xD2
#define KEY_DEL       0xD4
#define KEY_END       0xD5
#define KEY_PGUP      0xD6
#define KEY_PGDN      0xD7
#define KEY_CAPSLOCK  0xC1
#define KEY_F1        0x81
#define KEY_F2        0x82
#define KEY_F3        0x83
#define KEY_F4        0x84
#define KEY_F5        0x85
#define KEY_F6        0x86
#define KEY_F7        0x87
#define KEY_F8        0x88
#define KEY_F9        0x89
#define KEY_F10       0x90
#define KEY_POWER     0x91

#define KEY_MOD_ALT   0xA1
#define KEY_MOD_SHL   0xA2
#define KEY_MOD_SHR   0xA3
#define KEY_MOD_SYM   0xA4
#define KEY_MOD_CTRL  0xA5

typedef struct {
    uint8_t key;
    uint8_t state;
} kev_t;

void kbd_init(void);
bool kbd_get(kev_t *ev);       // pop next raw event (updates modifier state)
void kbd_flush(void);
bool kbd_ctrl(void);
bool kbd_alt(void);
bool kbd_shift(void);
int  kbd_battery(void);        // percent (0-100), -1 if unknown
bool kbd_charging(void);
static inline bool kbd_is_mod(uint8_t k) { return k >= KEY_MOD_ALT && k <= KEY_MOD_CTRL; }
