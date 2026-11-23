#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool update_version(const char *text, uint32_t version[3]);
int update_compare(const uint32_t a[3], const uint32_t b[3]);
bool update_checksum(const char *text, const char *filename, uint8_t digest[32]);
bool update_uf2_block(const uint8_t block[512], uint32_t index, uint32_t *count);
