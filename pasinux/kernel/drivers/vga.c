#include "vga.h"

#include "io.h"
#include <string.h>

#define VGA_COLOR 0x0Fu

static volatile uint16_t* const vga_mem = (volatile uint16_t*)0xC00B8000;
static size_t g_vga_row;
static size_t g_vga_col;

static void vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(g_vga_row * VGA_WIDTH + g_vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFFu));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFFu));
}

static void vga_scroll(void) {
    for (size_t i = 0; i < (VGA_HEIGHT - 1u) * VGA_WIDTH; ++i) {
        vga_mem[i] = vga_mem[i + VGA_WIDTH];
    }
    for (size_t i = 0; i < VGA_WIDTH; ++i) {
        vga_mem[(VGA_HEIGHT - 1u) * VGA_WIDTH + i] = (uint16_t)((VGA_COLOR << 8) | ' ');
    }
}

void vga_clear(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga_mem[i] = (uint16_t)((VGA_COLOR << 8) | ' ');
    }
    g_vga_row = 0;
    g_vga_col = 0;
    vga_update_cursor();
}

void vga_putc(char c) {
    switch (c) {
    case '\n':
        g_vga_row++;
        g_vga_col = 0;
        break;
    case '\r':
        g_vga_col = 0;
        break;
    case '\b':
        if (g_vga_col > 0u) {
            g_vga_col--;
            vga_mem[g_vga_row * VGA_WIDTH + g_vga_col] = (uint16_t)((VGA_COLOR << 8) | ' ');
        }
        break;
    case '\t':
        break;
    default:
        if (g_vga_row < VGA_HEIGHT && g_vga_col < VGA_WIDTH) {
            vga_mem[g_vga_row * VGA_WIDTH + g_vga_col] = (uint16_t)((VGA_COLOR << 8) | (uint8_t)c);
            g_vga_col++;
        }
        break;
    }
    if (g_vga_col >= VGA_WIDTH) {
        g_vga_col = 0;
        g_vga_row++;
    }
    while (g_vga_row >= VGA_HEIGHT) {
        vga_scroll();
        if (g_vga_row > 0u) g_vga_row--;
    }
    vga_update_cursor();
}

void vga_puts(const char* s) {
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        vga_putc(s[i]);
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

void vga_enable_cursor(void) {
    outb(0x3D4, 0x0A);
    uint8_t start = inb(0x3D5);
    outb(0x3D4, 0x0A);
    outb(0x3D5, (uint8_t)(start & ~0x20u));
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0Fu);
    vga_update_cursor();
}

void vga_disable_cursor(void) {
    outb(0x3D4, 0x0A);
    uint8_t start = inb(0x3D5);
    outb(0x3D4, 0x0A);
    outb(0x3D5, (uint8_t)(start | 0x20u));
}

void vga_set_cursor(size_t row, size_t col) {
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1u;
    if (col >= VGA_WIDTH)  col = VGA_WIDTH  - 1u;
    g_vga_row = row;
    g_vga_col = col;
    vga_update_cursor();
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

void vga_putc_attr(char c, uint8_t attr) {
    if (g_vga_row < VGA_HEIGHT && g_vga_col < VGA_WIDTH) {
        vga_mem[g_vga_row * VGA_WIDTH + g_vga_col] = (uint16_t)((attr << 8) | (uint8_t)c);
        g_vga_col++;
    }
    if (g_vga_col >= VGA_WIDTH) {
        g_vga_col = 0;
        g_vga_row++;
    }
    while (g_vga_row >= VGA_HEIGHT) {
        vga_scroll();
        if (g_vga_row > 0u) g_vga_row--;
    }
    vga_update_cursor();
}

void vga_cell(size_t row, size_t col, char c, uint8_t attr) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) {
        return;
    }
    vga_mem[row * VGA_WIDTH + col] = (uint16_t)((attr << 8) | (uint8_t)c);
}