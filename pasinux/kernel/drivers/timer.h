#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(uint32_t hz);
void timer_irq(void);
uint32_t timer_ticks(void);

#endif
