#ifndef VGA_H
#define VGA_H

#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH  80u
#define VGA_HEIGHT 25u

void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char* s);
void vga_enable_cursor(void);
void vga_disable_cursor(void);
void vga_set_cursor(size_t row, size_t col);
void vga_write(size_t row, size_t col, const char* text);
void vga_write_u32(size_t row, size_t col, const char* prefix, uint32_t value);

void vga_putc_attr(char c, uint8_t attr);
void vga_cell(size_t row, size_t col, char c, uint8_t attr);

#endif
