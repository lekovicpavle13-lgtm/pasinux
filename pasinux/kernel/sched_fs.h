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

void sched_fs_run(uint32_t ticks);

#endif
