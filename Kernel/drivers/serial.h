#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define SERIAL_PORT_COM1 0x3F8u

void serial_init(void);

void serial_putc(char c);

void serial_puts(const char* str);

void serial_put_u32(uint32_t value);

void serial_put_i32(int32_t value);

#endif