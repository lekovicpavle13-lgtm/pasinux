#include "timer.h"

#include "io.h"
#include "sched_fs.h"

static volatile uint32_t g_ticks;

void timer_init(uint32_t hz) {
    if (hz == 0u) {
        hz = 100u;
    }

    uint32_t divisor = 1193182u / hz;
    outb(0x43, 0x36); 
    outb(0x40, (uint8_t)(divisor & 0xFFu));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFFu));
    g_ticks = 0;
}

void timer_irq(void) {
    ++g_ticks;
    sched_fs_on_tick();
}

uint32_t timer_ticks(void) {
    return g_ticks;
}
