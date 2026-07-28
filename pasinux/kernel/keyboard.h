#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEYBUF_SIZE 256u

#define KEY_NONE    0u
#define KEY_BACKSP  0x08u
#define KEY_TAB     0x09u
#define KEY_ENTER   0x0Du
#define KEY_ESC     0x1Bu


void keyboard_init(void);

void keyboard_irq(void);

uint8_t keyboard_getc(void);

uint8_t keyboard_read(void);

uint16_t keyboard_readline(char* buf, uint16_t len);

int keyboard_available(void);

#endif