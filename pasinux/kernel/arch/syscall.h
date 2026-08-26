#ifndef SYSCALL_H
#define SYSCALL_H

#include "io.h"
#include "fat12.h"

#define SYS_PRINT 0u
#define SYS_EXIT 1u
#define SYS_GETTIME 2u
#define SYS_OPEN 3u
#define SYS_READ 4u
#define SYS_WRITE 5u
#define SYS_CLOSE 6u
#define SYS_SEEK 7u
#define SYS_BRK 8u
#define SYS_EXEC 9u
#define SYS_FSTAT 10u

extern volatile uint32_t g_syscall_ret_esp;
extern volatile uint32_t g_syscall_ret_eip;
extern volatile uint32_t g_syscall_did_exit;

/* Bind the FAT12 volume used by the file syscalls (call after mount). */
void syscall_set_fs(fat12_fs_t* fs);

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx,
                         uint32_t edx, uint32_t esi);

// Forward declarations for syscall implementations (defined in syscall.c)
uint32_t sys_open(const char* path, uint32_t flags);
uint32_t sys_read(uint32_t fd, void* buf, uint32_t count);
uint32_t sys_write(uint32_t fd, const void* buf, uint32_t count);
uint32_t sys_close(uint32_t fd);
uint32_t sys_seek(uint32_t fd, uint32_t offset, uint32_t whence);

#endif