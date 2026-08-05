#include "keyboard.h"

#include "io.h"
#include "serial.h"
#include "vga.h"

static const uint8_t scancode_ascii[128] = {
    0, KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', KEY_BACKSP, KEY_TAB,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
    KEY_ENTER, 0,          
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,                         
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,                          
    '*', 0, ' ',               
    0,                         
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0,                          
    0,                         
    '7', '8', '9', '-',
    '4', '5', '6', '+',
    '1', '2', '3', '0', '.',
    0, 0, 0,
    0,                          
    0,                         
};


static const uint8_t scancode_shifted[128] = {
    0, KEY_ESC, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', KEY_BACKSP, KEY_TAB,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    KEY_ENTER, 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,
    '*', 0, ' ',
    0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    '7', '8', '9', '-',
    '4', '5', '6', '+',
    '1', '2', '3', '0', '.',
    0, 0, 0,
    0, 0,
};



static volatile uint8_t g_keybuf[KEYBUF_SIZE];
static volatile uint16_t g_keybuf_head;
static volatile uint16_t g_keybuf_tail;

static volatile uint8_t g_shift;    
static volatile uint8_t g_alt;      
static volatile uint8_t g_ctrl;     




static int buf_full(void) {
    uint16_t next = (uint16_t)((g_keybuf_head + 1u) % KEYBUF_SIZE);
    return next == g_keybuf_tail;
}

static int buf_empty(void) {
    return g_keybuf_head == g_keybuf_tail;
}

static void buf_put(uint8_t c) {
    if (!buf_full()) {
        g_keybuf[g_keybuf_head] = c;
        g_keybuf_head = (uint16_t)((g_keybuf_head + 1u) % KEYBUF_SIZE);
    }
}

static uint8_t buf_get(void) {
    if (buf_empty()) {
        return KEY_NONE;
    }
    uint8_t c = g_keybuf[g_keybuf_tail];
    g_keybuf_tail = (uint16_t)((g_keybuf_tail + 1u) % KEYBUF_SIZE);
    return c;
}


void keyboard_init(void) {
    g_keybuf_head = 0;
    g_keybuf_tail = 0;
    g_shift = 0;
    g_alt = 0;
    g_ctrl = 0;

    serial_puts("[KBD] PS/2 keyboard initialised\n");
}

void keyboard_irq(void) {
    uint8_t status = inb(0x64);
    if ((status & 0x01u) == 0u) {
        return;               
    }

    uint8_t sc = inb(0x60);    
    uint8_t is_break = (sc & 0x80u) != 0u;
    uint8_t code = sc & 0x7Fu; 
   
    switch (code) {
    case 0x2A: case 0x36:       
        g_shift = (uint8_t)(is_break ? 0u : 1u);
        return;
    case 0x1D:                  
        g_ctrl = (uint8_t)(is_break ? 0u : 1u);
        return;
    case 0x38:                  
        g_alt = (uint8_t)(is_break ? 0u : 1u);
        return;
    default:
        break;
    }

    if (is_break) {
        return;
    }

    if (code >= 128u) {
        return;                  
    }

    uint8_t ascii;
    if (g_shift) {
        ascii = scancode_shifted[code];
    } else {
        ascii = scancode_ascii[code];
    }

   
    if (g_ctrl && ascii >= 'a' && ascii <= 'z') {
        ascii = (uint8_t)(ascii - 'a' + 1u);
    }

    if (ascii != 0u) {
        if (ascii != KEY_BACKSP) { buf_put(ascii); }
    }
}

uint8_t keyboard_getc(void) {
    return buf_get();
}

uint8_t keyboard_read(void) {
    uint8_t c;
    while ((c = buf_get()) == KEY_NONE) {
        __asm__ volatile ("pause; hlt");
    }
    return c;
}

uint16_t keyboard_readline(char* buf, uint16_t len) {
    uint16_t i = 0;
    if (!buf || len == 0u) {
        return 0u;
    }

    for (;;) {
        uint8_t c;
        while ((c = buf_get()) == KEY_NONE) {
            __asm__ volatile ("pause; hlt");
        }

        switch (c) {
        case KEY_ENTER:
            buf[i] = '\0';
            serial_putc('\n');
            return i;

        case KEY_BACKSP:
            if (i > 0u) {
                --i;
                serial_puts("\b \b");
            }
            break;

        case KEY_TAB:
            break;

        case KEY_ESC:
            buf[0] = '\0';
            return 0u;

        default:
            if (i < len - 1u) {
                buf[i++] = c;
                serial_putc((char)c);
            }
            break;
        }
    }
}

uint16_t keyboard_readline_vga(char* buf, uint16_t len) {
    uint16_t i = 0;
    if (!buf || len == 0u) {
        return 0u;
    }

    for (;;) {
        uint8_t c;
        while ((c = buf_get()) == KEY_NONE) {
            __asm__ volatile ("pause; hlt");
        }

        switch (c) {
        case KEY_ENTER:
            buf[i] = '\0';
            vga_putc('\n');
            return i;

        case KEY_BACKSP:
            if (i > 0u) {
                --i;
                vga_putc('\b');
            }
            break;

        case KEY_TAB:
            break;

        case KEY_ESC:
            buf[0] = '\0';
            return 0u;

        default:
            if (i < len - 1u) {
                buf[i++] = c;
                vga_putc((char)c);
            }
            break;
        }
    }
}