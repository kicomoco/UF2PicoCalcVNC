// Strip-based drawing: compose a horizontal strip in RAM, then blit it to the
// LCD in one go (flicker-free UI rows without a full-screen framebuffer).
#pragma once
#include <stdint.h>
#include "font.h"
#include "lcd.h"

#define CV_MAX_H 32

void cv_begin(int h, uint16_t bg);
void cv_rect(int x, int y, int w, int h, uint16_t c);
void cv_frame(int x, int y, int w, int h, uint16_t c);
int  cv_text(int x, int y, const char *s, const font_t *f, uint16_t fg);
// Draws text, truncating with ".." if it exceeds maxw pixels. Returns end x.
int  cv_text_clip(int x, int y, const char *s, const font_t *f, uint16_t fg, int maxw);
void cv_end(int y);                       // blit whole strip width at screen row y
void cv_end_part(int x, int y, int w);    // blit columns [x, x+w) of the strip

int text_width(const char *s, const font_t *f);
