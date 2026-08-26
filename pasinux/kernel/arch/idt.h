#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init(void);
void pic_unmask_irq(uint8_t irq);

#endif
