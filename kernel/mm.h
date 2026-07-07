
#pragma once

#include <stdint.h>


#define BRINCH_SIZE 4096
#define PAGETABLE_SIZE 4096
#define KERNEL_STACK_SIZE 4096


extern uint64_t g_allocations, g_freed, g_failed;


extern uint8_t heap_start[], heap_end[];