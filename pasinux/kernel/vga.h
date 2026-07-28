#ifndef VGA_H
#define VGA_H

#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH  80u
#define VGA_HEIGHT 25u

void vga_clear(void);
void vga_write(size_t row, size_t col, const char* text);
void vga_write_u32(size_t row, size_t col, const char* prefix, uint32_t value);

#endif
