// Simple full-screen UI toolkit for the PicoCalc (lists, menus, forms, dialogs)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lcd.h"

// Palette
#define C_BG        RGB565(12, 16, 28)
#define C_HDR       RGB565(0, 70, 150)
#define C_SEL       RGB565(40, 110, 210)
#define C_TEXT      RGB565(235, 235, 235)
#define C_DIM       RGB565(130, 135, 150)
#define C_ACCENT    RGB565(90, 220, 120)
#define C_WARN      RGB565(250, 190, 60)
#define C_ERR       RGB565(250, 90, 80)
#define C_BOX       RGB565(30, 36, 56)
#define C_EDIT      RGB565(5, 8, 16)

// Special key code returned when the screen must be redrawn (e.g. after help)
#define UI_KEY_REDRAW 0x01

enum { HELP_GENERAL, HELP_WIFI, HELP_VNCLIST, HELP_FORM, HELP_SESSION, HELP_UPDATE };
extern int ui_help_ctx;

void ui_init(void);
void ui_poll(void);                     // service network + refresh status bar
int  ui_getkey(void);                   // non-blocking: 0 if none; handles F1 help
int  ui_waitkey(void);                  // blocking
void ui_help(int ctx);

void ui_clear(const char *title, const char *footer);
void ui_header(const char *title);
void ui_footer(const char *text);
void ui_status_refresh(void);
void ui_suspend_status(void);           // stop periodic header refresh (full-screen views)

// --- List ---
#define UI_ITEM_SEP      1   // non-selectable separator/heading
#define UI_ITEM_DISABLED 2   // greyed out (still selectable)

typedef struct {
    char text[44];
    char right[24];
    uint16_t color;          // 0 = default
    uint8_t flags;
    int16_t tag;             // caller data
} ui_item_t;

// Returns selected index on Enter, -1 on Esc, or -2 when one of exit_keys
// (0-terminated) is pressed (the key is stored in *exit_key).
int ui_list(const char *title, const char *footer, ui_item_t *items, int n, int *sel,
            const int *exit_keys, int *exit_key);

// --- Popup menu --- returns chosen index or -1
int ui_menu(const char *title, const char *const *items, const bool *disabled, int n, int sel);

// --- Dialogs ---
void ui_message(const char *title, const char *msg);
bool ui_confirm(const char *title, const char *msg);
void ui_progress(const char *title, const char *msg);   // cancellable, animated when polled
void ui_progress_note(const char *title, const char *msg); // non-cancellable, static
void ui_progress_tick(void);
void ui_progress_end(void);
bool ui_cancel_requested(void);                          // true if Esc was pressed

// --- Forms ---
enum { F_TEXT, F_PASSWORD, F_NUMBER, F_CHOICE, F_BUTTON };

typedef struct {
    const char *label;
    uint8_t type;
    bool disabled;
    char *buf;                     // TEXT/PASSWORD/NUMBER
    int cap;
    const char *const *choices;    // CHOICE
    int nchoices;
    int *choice;
} ui_field_t;

// Returns index of activated button, or -1 on Esc.
int  ui_form(const char *title, ui_field_t *f, int n, int *focus);
bool ui_input(const char *title, const char *label, char *buf, int cap, bool password);
