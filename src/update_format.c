#include "update_format.h"
#include <string.h>

bool update_version(const char *s, uint32_t v[3]) {
    if (*s == 'v') ++s;
    for (int i = 0; i < 3; ++i) {
        if (*s < '0' || *s > '9') return false;
        uint32_t n = 0;
        do {
            unsigned d = *s++ - '0';
            if (n > (UINT32_MAX - d) / 10) return false;
            n = n * 10 + d;
        } while (*s >= '0' && *s <= '9');
        v[i] = n;
        if (i < 2 && *s++ != '.') return false;
    }
    return *s == 0;
}

int update_compare(const uint32_t a[3], const uint32_t b[3]) {
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool update_checksum(const char *text, const char *filename, uint8_t digest[32]) {
    size_t n = strlen(text);
    if (n < 66 || text[64] != ' ' || (text[65] != ' ' && text[65] != '*')) return false;
    for (int i = 0; i < 32; ++i) {
        int hi = hex(text[i * 2]), lo = hex(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        digest[i] = (uint8_t)(hi * 16 + lo);
    }
    size_t fn = strlen(filename);
    if (n < 66 + fn || memcmp(text + 66, filename, fn)) return false;
    const char *end = text + 66 + fn;
    if (*end == '\r') ++end;
    if (*end == '\n') ++end;
    return !*end;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool update_uf2_block(const uint8_t b[512], uint32_t index, uint32_t *count) {
    if (le32(b) != 0x0a324655 || le32(b + 4) != 0x9e5d5157 ||
        le32(b + 508) != 0x0ab16f30) return false;
    uint32_t flags = le32(b + 8), addr = le32(b + 12), size = le32(b + 16);
    // picotool prepends an RP2350 "ignore this block" family marker.
    if (!index) {
        *count = 0;
        return flags == 0xa000 && addr == 0x10ffff00 && size == 256 &&
               le32(b + 20) == 0 && le32(b + 24) == 2 && le32(b + 28) == 0xe48bff57;
    }
    // RP2350 ARM secure family, flash payloads only; reject RAM/no-flash images.
    if (flags != 0x2000 || le32(b + 28) != 0xe48bff59 ||
        size != 256 || addr < 0x10000000 || addr > 0x103fff00 || (addr & 255))
        return false;
    if (le32(b + 20) != index - 1 || !le32(b + 24)) return false;
    if (index == 1) *count = le32(b + 24);
    return le32(b + 24) == *count && index <= *count;
}
