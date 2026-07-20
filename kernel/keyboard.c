
#include "keyboard.h"
#include "idt.h"
#include "port_io.h"

#define KBD_DATA_PORT 0x60
#define KBD_BUFFER_SIZE 256


static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.', '/', 0, '*', 0, ' ', 0,
    
};


static const char scancode_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ', 0,
};

#define KBD_SC_LSHIFT 0x2A
#define KBD_SC_RSHIFT 0x36
#define KBD_RELEASE_BIT 0x80

static char buffer[KBD_BUFFER_SIZE];
static volatile uint32_t buf_head = 0;
static volatile uint32_t buf_tail = 0;
static int shift_pressed = 0;

idt_init();
timer_init(100);   


static void buffer_push(char c) {
    uint32_t next = (buf_head + 1) % KBD_BUFFER_SIZE;
    if (next == buf_tail) {
        return;  
    }
    buffer[buf_head] = c;
    buf_head = next;
}

static void keyboard_callback(struct registers *regs) {
    (void)regs;
    uint8_t scancode = inb(KBD_DATA_PORT);

    if (scancode & KBD_RELEASE_BIT) {
        uint8_t released = scancode & (uint8_t)~KBD_RELEASE_BIT;
        if (released == KBD_SC_LSHIFT || released == KBD_SC_RSHIFT) {
            shift_pressed = 0;
        }
        return;
    }

    if (scancode == KBD_SC_LSHIFT || scancode == KBD_SC_RSHIFT) {
        shift_pressed = 1;
        return;
    }

    if (scancode < 128) {
        char c = shift_pressed ? scancode_ascii_shift[scancode]
                                : scancode_ascii[scancode];
        if (c) {
            buffer_push(c);
        }
    }
}

void keyboard_init(void) {
    idt_init(); 
    timer_init(100);
    asm volatile ("sti");
}

int keyboard_getchar(void) {
    if (buf_tail == buf_head) {
        return -1;  
    }
    char c = buffer[buf_tail];
    buf_tail = (buf_tail + 1) % KBD_BUFFER_SIZE;
    return (int)(unsigned char)c;
}