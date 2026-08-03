#include "syscall.h"
#include "serial.h"
#include "timer.h"
#include "io.h"

volatile uint32_t g_syscall_ret_esp;
volatile uint32_t g_syscall_ret_eip;
volatile uint32_t g_syscall_did_exit;

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx,
                         uint32_t edx, uint32_t esi)
{
    (void)ecx;
    (void)edx;
    (void)esi;

    switch (eax) {
    case SYS_PRINT:
        serial_puts((const char*)(uintptr_t)ebx);
        return 0u;

    case SYS_EXIT:
        serial_puts("[SYS] returning to kernel\n");
        g_syscall_did_exit = 1u;
        return 0u;

    case SYS_GETTIME:
        return timer_ticks();

    default:
        serial_puts("[SYS] unknown syscall: ");
        serial_put_u32(eax);
        serial_puts("\n");
        return 0xFFFFFFFFu;
    }
}