#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t absolute_time_t;
#define GPIO_FUNC_I2C 3
absolute_time_t get_absolute_time(void);
uint64_t time_us_64(void);
absolute_time_t make_timeout_time_ms(uint32_t ms);
bool time_reached(absolute_time_t deadline);
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);
void gpio_set_function(unsigned pin, unsigned function);
void gpio_pull_up(unsigned pin);
