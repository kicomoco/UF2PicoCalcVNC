#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t absolute_time_t;
#define GPIO_FUNC_I2C 3
#define GPIO_FUNC_SPI 1
#define GPIO_OUT 1
absolute_time_t get_absolute_time(void);
uint64_t time_us_64(void);
absolute_time_t make_timeout_time_ms(uint32_t ms);
bool time_reached(absolute_time_t deadline);
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);
void gpio_set_function(unsigned pin, unsigned function);
void gpio_pull_up(unsigned pin);
void gpio_init(unsigned pin);
void gpio_put(unsigned pin, bool value);
void gpio_set_dir(unsigned pin, bool out);
void busy_wait_us(uint32_t us);
void tight_loop_contents(void);
