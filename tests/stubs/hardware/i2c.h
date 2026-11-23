#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define i2c1 1
void i2c_init(int bus, unsigned baud);
int i2c_write_timeout_us(int bus, uint8_t address, const uint8_t *data,
                         size_t size, bool nostop, unsigned timeout);
int i2c_read_timeout_us(int bus, uint8_t address, uint8_t *data,
                        size_t size, bool nostop, unsigned timeout);
