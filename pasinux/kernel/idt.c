#include "idt.h"

#include "io.h"
#include "timer.h"

#include <stdint.h>

#define IDT_ENTRIES 256u
#define IDT_FLAG_INTERRUPT_GATE 0x8Eu
#define KERNEL_CODE_SEL 0x08u

typedef struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

extern void* isr_stub_table[48];

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idtr;

static void idt_set_gate(uint8_t vector, void* handler) {
    uint32_t addr = (uint32_t)handler;
    idt[vector].offset_low = (uint16_t)(addr & 0xFFFFu);
    idt[vector].selector = KERNEL_CODE_SEL;
    idt[vector].zero = 0;
    idt[vector].type_attr = IDT_FLAG_INTERRUPT_GATE;
    idt[vector].offset_high = (uint16_t)((addr >> 16) & 0xFFFFu);
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20); 
    io_wait();
    outb(0xA1, 0x28); 
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void idt_init(void) {
    for (uint32_t i = 0; i < IDT_ENTRIES; ++i) {
        idt_set_gate((uint8_t)i, isr_stub_table[0]);
    }
    for (uint32_t i = 0; i < 48u; ++i) {
        idt_set_gate((uint8_t)i, isr_stub_table[i]);
    }

    idtr.limit = (uint16_t)(sizeof(idt) - 1u);
    idtr.base = (uint32_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr));

    pic_remap();
    timer_init(100);
    sti();
}
