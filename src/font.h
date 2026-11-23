#pragma once
#include <stdint.h>

// Fixed-width bitmap font, glyphs for ASCII 32..126, one byte per row (MSB = left).
typedef struct {
    uint8_t w, h;
    const uint8_t *data;
} font_t;

extern const font_t font_6x12;
extern const font_t font_8x16;

static inline const uint8_t *font_glyph(const font_t *f, char c) {
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || uc > 126) uc = '?';
    return f->data + (uc - 32) * f->h;
}
