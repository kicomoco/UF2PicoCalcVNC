#include <assert.h>
#include <stdio.h>
#include <string.h>

// Include private worker/parser entry points to exercise the production code.
#include "../src/kbd.c"
#include "../src/config.c"
#include "../src/settings.c"

static uint64_t clock_us;
static bool worker_running = true, bad_reply, no_sd, fail_save;
static int fail_write, writes, reads;
static uint8_t registers[128], selected_reg;
static int form_result = -1, form_screen, form_keyboard, messages;
static char last_message[192];
static struct {
    bool exists;
    char text[12288];
} files[3];

absolute_time_t get_absolute_time(void) { return clock_us; }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return clock_us + ms * 1000u; }
bool time_reached(absolute_time_t deadline) { return clock_us >= deadline; }
void sleep_us(uint64_t us) { clock_us += us; }
void sleep_ms(uint32_t ms) {
    clock_us += ms * 1000u;
    if (worker_running) service_backlights();
}
void gpio_set_function(unsigned pin, unsigned function) { (void)pin; (void)function; }
void gpio_pull_up(unsigned pin) { (void)pin; }
void multicore_launch_core1(void (*entry)(void)) { (void)entry; }
void i2c_init(int bus, unsigned baud) { (void)bus; (void)baud; }

int i2c_write_timeout_us(int bus, uint8_t address, const uint8_t *data,
                         size_t size, bool nostop, unsigned timeout) {
    assert(bus == SB_I2C && address == SB_ADDR && !nostop && timeout == 20000);
    if (++writes == fail_write) return -1;
    assert(size == 1 || size == 2);
    selected_reg = data[0] & 0x7F;
    assert(selected_reg == SB_REG_BKL || selected_reg == SB_REG_BK2);
    if (size == 2) {
        assert(data[0] & 0x80);
        unsigned value = data[1];
        if (selected_reg == SB_REG_BKL) {
            value = value / 16 * 16;
            if (value < 16) value = 16;
            if (value > 240) value = 240;
        } else {
            value = value / 32 * 32;
            if (value > 240) value = 0;
        }
        registers[selected_reg] = (uint8_t)value;
    }
    return (int)size;
}

int i2c_read_timeout_us(int bus, uint8_t address, uint8_t *data,
                        size_t size, bool nostop, unsigned timeout) {
    assert(bus == SB_I2C && address == SB_ADDR && size == 2 && !nostop && timeout == 20000);
    reads++;
    data[0] = bad_reply ? 0x7F : selected_reg;
    data[1] = registers[selected_reg];
    return 2;
}

static int file_index(const char *path) {
    if (!strcmp(path, CONFIG_PATH)) return 0;
    if (!strcmp(path, "/picocalc-vnc.tmp")) return 1;
    assert(!strcmp(path, CONFIG_BAK));
    return 2;
}
FRESULT f_mount(FATFS *volume, const TCHAR *path, BYTE opt) {
    (void)volume; (void)path; (void)opt;
    return no_sd ? FR_NOT_READY : FR_OK;
}
FRESULT f_open(FIL *file, const TCHAR *path, BYTE mode) {
    int i = file_index(path);
    if (mode & FA_CREATE_ALWAYS) {
        if (fail_save) return FR_DISK_ERR;
        files[i].exists = true;
        files[i].text[0] = 0;
    } else if (!files[i].exists) return FR_NO_FILE;
    file->obj.id = (WORD)i;
    return FR_OK;
}
FRESULT f_close(FIL *file) { (void)file; return FR_OK; }
FRESULT f_read(FIL *file, void *data, UINT size, UINT *count) {
    size_t len = strlen(files[file->obj.id].text);
    *count = (UINT)(len < size ? len : size);
    memcpy(data, files[file->obj.id].text, *count);
    return FR_OK;
}
FRESULT f_write(FIL *file, const void *data, UINT size, UINT *count) {
    assert(size < sizeof(files[0].text));
    memcpy(files[file->obj.id].text, data, size);
    files[file->obj.id].text[size] = 0;
    *count = size;
    return FR_OK;
}
FRESULT f_unlink(const TCHAR *path) {
    int i = file_index(path);
    if (!files[i].exists) return FR_NO_FILE;
    files[i].exists = false;
    return FR_OK;
}
FRESULT f_rename(const TCHAR *from, const TCHAR *to) {
    int source = file_index(from), dest = file_index(to);
    if (!files[source].exists) return FR_NO_FILE;
    assert(!files[dest].exists);
    files[dest] = files[source];
    files[source].exists = false;
    return FR_OK;
}
int ui_form(const char *title, ui_field_t *fields, int count, int *focus) {
    (void)title; (void)focus;
    assert(count == 4 && fields[0].nchoices == 10 && fields[1].nchoices == 11);
    *fields[0].choice = form_screen;
    *fields[1].choice = form_keyboard;
    return form_result;
}
void ui_message(const char *title, const char *text) {
    (void)title;
    messages++;
    snprintf(last_message, sizeof(last_message), "%s", text);
}

static void test_controller(void) {
    static const uint8_t screen_pwm[] = {32, 48, 80, 96, 128, 144, 176, 192, 224, 240};
    static const uint8_t keyboard_pwm[] = {32, 32, 64, 96, 128, 128, 160, 192, 192, 224};
    for (int i = 0; i < 10; i++) {
        assert(kbd_set_brightness((i + 1) * 10, (i + 1) * 10));
        assert(registers[SB_REG_BKL] == screen_pwm[i]);
        assert(registers[SB_REG_BK2] == keyboard_pwm[i]);
    }
    uint8_t screen, keyboard;
    assert(kbd_get_brightness(&screen, &keyboard));
    assert(screen == 100 && keyboard == 100);
    assert(kbd_set_brightness(-1, 0));
    assert(registers[SB_REG_BKL] == 240 && registers[SB_REG_BK2] == 0);
    assert(kbd_get_brightness(&screen, &keyboard) && keyboard == 0);
    assert(kbd_set_brightness(10, -1) && registers[SB_REG_BK2] == 0);
    int before = writes;
    assert(!kbd_set_brightness(0, 100) && !kbd_set_brightness(101, 100));
    assert(!kbd_set_brightness(10, -2) && !kbd_set_brightness(10, 101));
    assert(kbd_set_brightness(-1, -1) && writes == before);

    bad_reply = true;
    assert(!kbd_get_brightness(&screen, &keyboard));
    assert(!kbd_set_brightness(100, 100));
    bad_reply = false;
    fail_write = writes + 2;
    assert(!kbd_set_brightness(20, 100));
    assert(registers[SB_REG_BKL] == 48 && registers[SB_REG_BK2] == 0);
    fail_write = 0;

    worker_running = false;
    uint64_t start = clock_us;
    assert(!kbd_set_brightness(100, 100));
    assert(clock_us - start == 500000);
    unsigned pending = atomic_load(&backlight_request);
    assert(pending && !kbd_set_brightness(10, 0));
    assert(atomic_load(&backlight_request) == pending);
    worker_running = true;
    service_backlights();
    assert(kbd_get_brightness(&screen, &keyboard) && screen == 100 && keyboard == 100);
}

static void test_configuration(void) {
    files[0].exists = true;
    strcpy(files[0].text, "[general]\nlast_vnc=0\n[vnc]\nhost=example.test\n");
    assert(config_init() && g_cfg.screen_brightness == -1 && g_cfg.keyboard_brightness == -1);
    int before = writes;
    assert(settings_apply_saved() && writes == before);
    assert(config_save() && !strstr(files[0].text, "_brightness="));
    g_cfg.screen_brightness = 10;
    g_cfg.keyboard_brightness = 0;
    assert(config_save());
    assert(config_init() && g_cfg.screen_brightness == 10 && g_cfg.keyboard_brightness == 0);
    assert(g_cfg.nvnc == 1 && !strcmp(g_cfg.vnc[0].host, "example.test"));
    assert(settings_apply_saved() && registers[SB_REG_BKL] == 32 && registers[SB_REG_BK2] == 0);
    g_cfg.screen_brightness = -1;
    g_cfg.keyboard_brightness = 100;
    assert(settings_apply_saved() && registers[SB_REG_BKL] == 32 && registers[SB_REG_BK2] == 224);
    strcpy(files[0].text, "[general]\nscreen_brightness=0\nkeyboard_brightness=101\n");
    assert(config_init() && g_cfg.screen_brightness == -1 && g_cfg.keyboard_brightness == -1);
    strcpy(files[0].text, "[general]\nscreen_brightness=50junk\nkeyboard_brightness=\n");
    assert(config_init() && g_cfg.screen_brightness == -1 && g_cfg.keyboard_brightness == -1);
    no_sd = true;
    assert(!config_init() && g_cfg.screen_brightness == -1 && g_cfg.keyboard_brightness == -1);
    no_sd = false;
}

static void test_settings(void) {
    files[0].exists = false;
    assert(config_init());
    form_screen = 4;
    form_keyboard = 0;
    form_result = -1;
    int before = writes;
    settings_screen();
    assert(writes == before + 2 && g_cfg.screen_brightness == -1);
    form_result = 2;
    bad_reply = true;
    settings_screen();
    assert(g_cfg.screen_brightness == -1 && messages == 1);
    bad_reply = false;
    fail_write = writes + 4; // Two reads, then the second backlight write fails.
    settings_screen();
    assert(g_cfg.screen_brightness == -1 && messages == 2);
    assert(!files[0].exists);
    fail_write = 0;
    settings_screen();
    assert(config_init() && g_cfg.screen_brightness == 50 && g_cfg.keyboard_brightness == 0);
    fail_save = true;
    settings_screen();
    assert(strstr(last_message, "could not save"));
    fail_save = false;
    no_sd = true;
    assert(!config_init());
    settings_screen();
    assert(strstr(last_message, "this session only"));
    no_sd = false;
}

int main(void) {
    test_controller();
    test_configuration();
    test_settings();
    puts("Brightness controller, persistence and settings tests passed.");
    return 0;
}
