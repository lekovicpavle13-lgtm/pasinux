#ifndef SCHED_FS_H
#define SCHED_FS_H

#include <stdint.h>

void sched_fs_init(void);
void sched_fs_on_tick(void);

uint32_t sched_fs_maybe_switch(uint32_t current_esp);

uint32_t sched_fs_ticks(void);
uint32_t sched_fs_switches(void);
const char* sched_fs_current_name(void);

void sched_fs_create_process(const char* name, void (*entry)(void), uint8_t priority);

/* Suspend/resume preemption while a foreground ring-3 program runs. */
void sched_fs_preempt_enable(int enable);

/* Reload CR3 with the kernel page directory (after leaving user mode). */
void sched_fs_restore_kernel_cr3(void);

void sched_fs_run(uint32_t ticks);

#endif
