#ifndef ANVIL_FB_H
#define ANVIL_FB_H

#include "types.h"
#include "bootinfo.h"

#define RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

bool     fb_init(const boot_info_t* bi);
bool     fb_is_active(void);
uint32_t fb_width(void);
uint32_t fb_height(void);

void fb_put_pixel(int32_t x, int32_t y, uint32_t color);
void fb_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void fb_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void fb_fill_gradient_v(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t top_col, uint32_t bot_col);
void fb_draw_char(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg, bool transparent_bg);
void fb_draw_string(int32_t x, int32_t y, const char* s, uint32_t fg, uint32_t bg, bool transparent_bg);
void fb_draw_bitmap_16x16(int32_t x, int32_t y, const uint16_t* rows, uint32_t fg, int scale);
void fb_draw_argb_icon(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t* argb);
void fb_blit_rect(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t* src, int32_t src_stride);
void fb_present(void);

#endif
