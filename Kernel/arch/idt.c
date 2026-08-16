#include "idt.h"

#include "io.h"
#include "timer.h"

#include <stdint.h>

#define IDT_ENTRIES 256u
#define IDT_FLAG_INTERRUPT_GATE 0x8Eu
#define IDT_FLAG_TRAP_GATE     0x8Fu
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
extern char isr_syscall_stub[];

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idtr;

static void idt_set_gate(uint8_t vector, void* handler, uint8_t dpl, uint8_t is_trap) {
    uint32_t addr = (uint32_t)handler;
    uint8_t gate_type = is_trap ? IDT_FLAG_TRAP_GATE : IDT_FLAG_INTERRUPT_GATE;
    uint8_t type_attr = (uint8_t)(gate_type | ((dpl & 3u) << 5));
    idt[vector].offset_low = (uint16_t)(addr & 0xFFFFu);
    idt[vector].selector = KERNEL_CODE_SEL;
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
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
        idt_set_gate((uint8_t)i, isr_stub_table[0], 0u, 0);
    }
    for (uint32_t i = 0; i < 48u; ++i) {
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0u, 0);
    }

    /* Syscall gate 0x80 — trap gate (0xEF) so ring-3 int 0x80 doesn't clear IF.
     * DPL=3 so user code can call it. */
    idt_set_gate(0x80u, isr_syscall_stub, 3u, 1);

    idtr.limit = (uint16_t)(sizeof(idt) - 1u);
    idtr.base = (uint32_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idtr));

    pic_remap();
    timer_init(100);
    sti();
}
