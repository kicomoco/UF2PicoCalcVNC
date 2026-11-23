#include "settings.h"
#include "config.h"
#include "kbd.h"
#include "ui.h"

bool settings_apply_saved(void) {
    return kbd_set_brightness(g_cfg.screen_brightness, g_cfg.keyboard_brightness);
}

void settings_screen(void) {
    uint8_t screen, keyboard;
    if (!kbd_get_brightness(&screen, &keyboard)) {
        ui_message("Settings error", "Could not read the screen and keyboard brightness from the controller.");
        return;
    }
    static const char *const levels[] = {
        "Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"
    };
    int screen_choice = screen / 10 - 1, keyboard_choice = keyboard / 10;
    ui_field_t fields[] = {
        {.label = "Screen", .type = F_CHOICE, .choices = levels + 1, .nchoices = 10, .choice = &screen_choice},
        {.label = "Keyboard", .type = F_CHOICE, .choices = levels, .nchoices = 11, .choice = &keyboard_choice},
        {.label = "Apply & Save", .type = F_BUTTON},
        {.label = "Cancel", .type = F_BUTTON},
    };
    int focus = 0;
    int result = ui_form("Brightness settings", fields, 4, &focus);
    if (result != 2) return;
    screen = (uint8_t)((screen_choice + 1) * 10);
    keyboard = (uint8_t)(keyboard_choice * 10);
    if (!kbd_set_brightness(screen, keyboard)) {
        ui_message("Settings error",
                   "Could not apply both brightness levels. Reopen Settings to check the controller.");
        return;
    }
    g_cfg.screen_brightness = screen;
    g_cfg.keyboard_brightness = keyboard;
    if (!config_storage_ok())
        ui_message("Brightness applied", "No SD card: brightness is applied for this session only.");
    else if (!config_save())
        ui_message("Storage error", "Brightness applied, but could not save it to the SD card.");
}
