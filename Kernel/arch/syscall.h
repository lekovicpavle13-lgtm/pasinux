#ifndef SYSCALL_H
#define SYSCALL_H
#include "io.h"

#define SYS_PRINT    0u
#define SYS_EXIT     1u
#define SYS_GETTIME  2u

extern volatile uint32_t g_syscall_ret_esp;
extern volatile uint32_t g_syscall_ret_eip;
extern volatile uint32_t g_syscall_did_exit;

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx,
                         uint32_t edx, uint32_t esi);

#endif 