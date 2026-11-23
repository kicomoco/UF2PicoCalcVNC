#pragma once
#include <stdbool.h>
#include <stdint.h>

bool sd_init(void);
bool sd_ready(void);
bool sd_read(uint32_t block, uint8_t *buf, uint32_t count);
bool sd_write(uint32_t block, const uint8_t *buf, uint32_t count);
