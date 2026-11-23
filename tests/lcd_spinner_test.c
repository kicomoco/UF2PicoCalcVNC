#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

#include "../src/lcd.c"

static atomic_uint_fast64_t now;
static bool selected, dc;
static unsigned high_delay, bits;
static int frames;
static uint16_t captured[72];
static atomic_bool stress_done;

void mutex_init(mutex_t *mutex) { assert(!pthread_mutex_init(mutex, NULL)); }
void mutex_enter_blocking(mutex_t *mutex) { assert(!pthread_mutex_lock(mutex)); }
bool mutex_try_enter(mutex_t *mutex, uint32_t *owner) {
    (void)owner;
    return pthread_mutex_trylock(mutex) == 0;
}
void mutex_exit(mutex_t *mutex) { assert(!pthread_mutex_unlock(mutex)); }
uint64_t time_us_64(void) { return atomic_load(&now); }
void sleep_ms(uint32_t ms) { high_delay += ms * 1000; }
void busy_wait_us(uint32_t us) { high_delay += us; }
void gpio_init(unsigned pin) { (void)pin; }
void gpio_set_function(unsigned pin, unsigned function) { (void)pin; (void)function; }
void gpio_set_dir(unsigned pin, bool out) { (void)pin; (void)out; }
void gpio_put(unsigned pin, bool value) {
    if (pin == LCD_PIN_CS) {
        if (!value) assert(!selected && high_delay >= 1);
        selected = !value;
        if (value) high_delay = 0;
    } else if (pin == LCD_PIN_DC) dc = value;
}
void spi_init(int spi, unsigned baud) { assert(spi == LCD_SPI && baud == LCD_BAUD); }
void spi_set_format(int spi, unsigned width, int cpol, int cpha, int order) {
    (void)cpol; (void)cpha; (void)order;
    assert(spi == LCD_SPI);
    bits = width;
}
int spi_write_blocking(int spi, const uint8_t *data, size_t count) {
    (void)data;
    assert(spi == LCD_SPI && selected && bits == 8);
    return (int)count;
}
int spi_write16_blocking(int spi, const uint16_t *data, size_t count) {
    assert(spi == LCD_SPI && selected && dc && bits == 16);
    if (count == 72) {
        memcpy(captured, data, sizeof(captured));
        frames++;
    }
    return (int)count;
}

static void check_frame(char character) {
    const uint8_t *glyph = font_glyph(&font_6x12, character);
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 6; x++)
            assert(captured[y * 6 + x] == ((glyph[y] & (0x80 >> x)) ? 0x1234 : 0x4321));
}
static void *worker(void *unused) {
    (void)unused;
    lcd_spinner_service();
    return NULL;
}
static void service_on_worker(void) {
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker, NULL));
    assert(!pthread_join(thread, NULL));
}
static void *stress_worker(void *unused) {
    (void)unused;
    while (!atomic_load(&stress_done)) lcd_spinner_service();
    return NULL;
}

int main(void) {
    lcd_init();
    lcd_spinner_start(200, 150, 0x1234, 0x4321);
    service_on_worker();
    assert(frames == 0);
    const char sequence[] = "/-\\|";
    for (int i = 0; i < 4; i++) {
        atomic_store(&now, (uint64_t)(i + 1) * 200000);
        service_on_worker();
        assert(frames == i + 1);
        check_frame(sequence[i]);
    }
    mutex_enter_blocking(&lcd_mutex);
    atomic_store(&now, 1000000);
    service_on_worker();
    assert(frames == 4);
    mutex_exit(&lcd_mutex);
    service_on_worker();
    assert(frames == 5);
    check_frame('/');
    lcd_spinner_stop();
    atomic_store(&now, 1200000);
    service_on_worker();
    assert(frames == 5);
    lcd_fill(318, 319, 10, 10, 0x1234);
    lcd_spinner_start(210, 160, 0x1234, 0x4321);
    atomic_store(&now, 1400000);
    service_on_worker();
    assert(frames == 6);
    check_frame('\\');
    lcd_spinner_stop();

    pthread_t thread;
    assert(!pthread_create(&thread, NULL, stress_worker, NULL));
    for (int i = 0; i < 1000; i++) {
        lcd_spinner_start(200, 150, 0x1234, 0x4321);
        atomic_fetch_add(&now, 200000);
        lcd_fill(0, 0, 8, 8, 0x1234);
        lcd_spinner_stop();
    }
    atomic_store(&stress_done, true);
    assert(!pthread_join(thread, NULL));
    int last_frame_count = frames;
    atomic_fetch_add(&now, 200000);
    service_on_worker();
    assert(frames == last_frame_count);
    puts("Independent LCD spinner and shared display tests passed.");
    return 0;
}
