
#include "timer.h"
#include "idt.h"
#include "port_io.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182u  

static volatile uint32_t tick_count = 0;

static void timer_callback(struct registers *regs) {
    (void)regs;
    tick_count++;
}

void timer_init(uint32_t frequency) {
    register_interrupt_handler(32, timer_callback);  

    if (frequency == 0) {
        frequency = 1;   
    }

    uint32_t divisor = PIT_BASE_FREQ / frequency;

    outb(PIT_COMMAND, 0x36);             
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

uint32_t timer_get_ticks(void) {
    return tick_count;
}
