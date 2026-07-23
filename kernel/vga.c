#include "vga.h"
static unsigned int vga_row;
static unsigned int vga_column;
static unsigned char vga_color;
void vga_init(void) {
vga_row = 0;
vga_column = 0;
vga_color = VGA_LIGHT_GREY | (VGA_BLACK << 4);
vga_clear();
}
void vga_clear(void) {
unsigned int y, x;
for (y = 0; y < VGA_HEIGHT; y++) {
for (x = 0; x < VGA_WIDTH; x++) {
unsigned int index = y * VGA_WIDTH + x;
VGA_MEMORY[index] = ' ' | ((unsigned short)vga_color << 8);
}
}
}
void vga_putchar(char c, unsigned char color) {
if (c == '\n') {
vga_column = 0;
if (++vga_row == VGA_HEIGHT) {
vga_row = VGA_HEIGHT - 1;
vga_clear();
}
return;
}
unsigned int index = vga_row * VGA_WIDTH + vga_column;
VGA_MEMORY[index] = (unsigned short)c | ((unsigned short)color << 8);
if (++vga_column == VGA_WIDTH) {
vga_column = 0;
if (++vga_row == VGA_HEIGHT) {
vga_row = VGA_HEIGHT - 1;
vga_clear();
}
}
}
void vga_puts(const char *str, unsigned char color) {
while (*str) {
vga_putchar(*str++, color);
}
}