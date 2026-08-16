#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>

typedef struct interrupt_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} interrupt_frame_t;

typedef void (*irq_handler_t)(void);

int  irq_register(uint8_t irq, irq_handler_t handler);
int  irq_unregister(uint8_t irq);
void irq_init_handlers(void);

void interrupt_dispatch(interrupt_frame_t* frame);
void pic_send_eoi(uint8_t irq);

#endif
