
#include "idt.h"
#include "port_io.h"

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;
static isr_t interrupt_handlers[IDT_ENTRIES];

extern void idt_flush(uint32_t idt_ptr_addr);

/* CPU exception stubs (0-31), defined in isr.asm */
extern void isr0();  extern void isr1();  extern void isr2();  extern void isr3();
extern void isr4();  extern void isr5();  extern void isr6();  extern void isr7();
extern void isr8();  extern void isr9();  extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();

/* Hardware IRQ stubs (remapped to vectors 32-47), defined in isr.asm */
extern void irq0();  extern void irq1();  extern void irq2();  extern void irq3();
extern void irq4();  extern void irq5();  extern void irq6();  extern void irq7();
extern void irq8();  extern void irq9();  extern void irq10(); extern void irq11();
extern void irq12(); extern void irq13(); extern void irq14(); extern void irq15();

static void (*isr_stub_table[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static void (*irq_stub_table[16])(void) = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15,
};

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}


static void pic_remap(void) {
    outb(0x20, 0x11); io_wait();   
    outb(0xA0, 0x11); io_wait();   

    outb(0x21, 0x20); io_wait();   
    outb(0xA1, 0x28); io_wait();   

    outb(0x21, 0x04); io_wait();  
    outb(0xA1, 0x02); io_wait();   

    outb(0x21, 0x01); io_wait();  
    outb(0xA1, 0x01); io_wait();

    outb(0x21, 0x00); io_wait();   
                                    
    outb(0xA1, 0x00); io_wait();
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
        interrupt_handlers[i] = 0;
    }

    pic_remap();

   
    for (int i = 0; i < 32; i++) {
        idt_set_gate((uint8_t)i, (uint32_t)isr_stub_table[i], 0x08, 0x8E);
    }
    for (int i = 0; i < 16; i++) {
        idt_set_gate((uint8_t)(32 + i), (uint32_t)irq_stub_table[i], 0x08, 0x8E);
    }

    idt_flush((uint32_t)&idtp);
}


void isr_handler(struct registers regs) {
    if (interrupt_handlers[regs.int_no]) {
        interrupt_handlers[regs.int_no](&regs);
    }
}


void irq_handler(struct registers regs) {
    if (regs.int_no >= 40) {
        outb(0xA0, 0x20);   
    }
    outb(0x20, 0x20);       

    if (interrupt_handlers[regs.int_no]) {
        interrupt_handlers[regs.int_no](&regs);
    }
}
