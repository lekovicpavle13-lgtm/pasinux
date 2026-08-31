#ifndef VGA_GFX_H
#define VGA_GFX_H

#include <stdint.h>

#define VGA_GFX_WIDTH  320
#define VGA_GFX_HEIGHT 200
#define VGA_GFX_PITCH  320
#define VGA_GFX_FB     0xA0000

void vga_gfx_init(void);
void vga_gfx_set_mode_13h(void);
void vga_gfx_set_mode_text(void);

void vga_gfx_put_pixel(uint16_t x, uint16_t y, uint8_t color);
uint8_t vga_gfx_get_pixel(uint16_t x, uint16_t y);

void vga_gfx_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);
void vga_gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);
void vga_gfx_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t color);
void vga_gfx_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t color);
void vga_gfx_fill_circle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t color);

void vga_gfx_clear(uint8_t color);
void vga_gfx_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* src, uint16_t src_pitch);

void vga_gfx_draw_char(uint16_t x, uint16_t y, char c, uint8_t fg, uint8_t bg);
void vga_gfx_draw_string(uint16_t x, uint16_t y, const char* s, uint8_t fg, uint8_t bg);
void vga_gfx_draw_string_len(uint16_t x, uint16_t y, const char* s, uint16_t len, uint8_t fg, uint8_t bg);

uint16_t vga_gfx_text_width(const char* s);
uint16_t vga_gfx_text_height(void);

void vga_gfx_wait_vsync(void);

#define VGA_PALETTE_INDEX  0x3C8
#define VGA_PALETTE_DATA   0x3C9
void vga_gfx_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void vga_gfx_get_palette(uint8_t index, uint8_t* r, uint8_t* g, uint8_t* b);
void vga_gfx_load_default_palette(void);

#endif