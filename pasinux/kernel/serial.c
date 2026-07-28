#include "serial.h"

#include <stddef.h>
#include "io.h"

void serial_init(void) {
    outb(SERIAL_PORT_COM1 + 1, 0x00); 
    outb(SERIAL_PORT_COM1 + 3, 0x80);  
    outb(SERIAL_PORT_COM1 + 0, 0x01);  
    outb(SERIAL_PORT_COM1 + 1, 0x00);  
    outb(SERIAL_PORT_COM1 + 3, 0x03);  
    outb(SERIAL_PORT_COM1 + 2, 0xC7);  
    outb(SERIAL_PORT_COM1 + 4, 0x0B);  
    (void)inb(SERIAL_PORT_COM1);       
}

static int serial_is_transmit_empty(void) {
    return (int)(inb(SERIAL_PORT_COM1 + 5) & 0x20u);
}

void serial_putc(char c) {
    while (!serial_is_transmit_empty()) {
        __asm__ volatile ("pause");
    }
    outb(SERIAL_PORT_COM1, (uint8_t)c);
    if (c == '\n') {
        while (!serial_is_transmit_empty()) {
            __asm__ volatile ("pause");
        }
        outb(SERIAL_PORT_COM1, '\r');
    }
}

void serial_puts(const char* str) {
    if (!str) {
        return;
    }
    for (; *str != '\0'; ++str) {
        serial_putc(*str);
    }
}

void serial_put_u32(uint32_t value) {
    char buf[12];
    size_t n = 0;

    if (value == 0u) {
        buf[n++] = '0';
    } else {
        char digits[10];
        size_t d = 0;
        while (value > 0u && d < sizeof(digits)) {
            digits[d++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (d > 0u) {
            buf[n++] = digits[--d];
        }
    }
    buf[n] = '\0';
    serial_puts(buf);
}