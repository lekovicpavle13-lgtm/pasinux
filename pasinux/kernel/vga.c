#include "vga.h"

#define VGA_COLOR 0x0Fu

static volatile uint16_t* const vga_mem = (volatile uint16_t*)0xB8000;

void vga_clear(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga_mem[i] = (uint16_t)((VGA_COLOR << 8) | ' ');
    }
}

void vga_write(size_t row, size_t col, const char* text) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH || !text) {
        return;
    }

    size_t pos = row * VGA_WIDTH + col;
    for (size_t i = 0; text[i] != '\0' && pos < VGA_WIDTH * VGA_HEIGHT; ++i, ++pos) {
        vga_mem[pos] = (uint16_t)((VGA_COLOR << 8) | (uint8_t)text[i]);
    }
}

void vga_write_u32(size_t row, size_t col, const char* prefix, uint32_t value) {
    char buf[48];
    size_t n = 0;

    if (prefix) {
        for (; prefix[n] != '\0' && n < sizeof(buf) - 12u; ++n) {
            buf[n] = prefix[n];
        }
    }

    char digits[10];
    size_t d = 0;
    if (value == 0u) {
        digits[d++] = '0';
    } else {
        while (value > 0u && d < sizeof(digits)) {
            digits[d++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }

    while (d > 0u && n < sizeof(buf) - 1u) {
        buf[n++] = digits[--d];
    }
    buf[n] = '\0';
    vga_write(row, col, buf);
}
