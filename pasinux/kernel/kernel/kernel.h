#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

#include "scheduler.h"


void kernel_run_demo(void);



void kernel_init_all(void);
void kernel_reset(void);



process_t* kernel_spawn_process(const char* name, uint8_t priority);


void kernel_main(void);

#endif 
