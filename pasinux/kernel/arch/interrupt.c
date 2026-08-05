#include "interrupt.h"

#include "io.h"
#include "keyboard.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"

static irq_handler_t g_irq_handlers[16];

void irq_init_handlers(void) {
    for (uint32_t i = 0u; i < 16u; ++i) {
        g_irq_handlers[i] = (irq_handler_t)0;
    }
}

int irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq >= 16u || handler == (irq_handler_t)0) {
        return -1;
    }
    g_irq_handlers[irq] = handler;
    return 0;
}

int irq_unregister(uint8_t irq) {
    if (irq >= 16u) {
        return -1;
    }
    g_irq_handlers[irq] = (irq_handler_t)0;
    return 0;
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8u) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

void interrupt_dispatch(interrupt_frame_t* frame) {
    if (!frame) {
        return;
    }

    if (frame->int_no >= 32u && frame->int_no <= 47u) {
        uint8_t irq = (uint8_t)(frame->int_no - 32u);
        if (irq == 0u) {
            timer_irq();
        } else if (irq == 1u) {
            keyboard_irq();
        } else if (g_irq_handlers[irq] != (irq_handler_t)0) {
            g_irq_handlers[irq]();
        }
        pic_send_eoi(irq);
        return;
    }

    serial_puts("[PANIC] CPU exception vector=");
    serial_put_u32(frame->int_no);
    serial_puts(" eip=");
    serial_put_u32(frame->eip);
    serial_puts(" cs=");
    serial_put_u32(frame->cs);
    serial_puts(" eflags=");
    serial_put_u32(frame->eflags);
    serial_puts("\n");

    vga_write(22, 0, "EXCEPTION vector=");
    vga_write_u32(22, 18, "", frame->int_no);
    cli();
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
