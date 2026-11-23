#pragma once
#include <stdint.h>
#include <stddef.h>

#define LCD_W 320
#define LCD_H 320

#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

void lcd_init(void);

// Streaming pixel writes: begin a window, push w*h pixels, end.
void lcd_begin(int x, int y, int w, int h);
void lcd_push(const uint16_t *px, size_t n);
void lcd_end(void);

void lcd_blit(const uint16_t *px, int stride, int x, int y, int w, int h);
void lcd_fill(int x, int y, int w, int h, uint16_t c);
