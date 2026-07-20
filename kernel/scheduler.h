#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#define PROC_STATE_READY    0u
#define PROC_STATE_RUNNING  1u
#define PROC_STATE_SLEEPING 2u
#define PROC_STATE_ZOMBIE   3u

#define SCHED_PRIORITY_LOW     1u
#define SCHED_PRIORITY_NORMAL  5u
#define SCHED_PRIORITY_HIGH    10u

typedef void (*process_entry_t)(void);

typedef struct process {
    uint64_t pid;
    uint64_t ppid;
    uint8_t state;
    uint8_t priority;
    uint64_t cpu_time;
    uint64_t wake_tick;
    process_entry_t entry_point;
    void* stack_base;
    void* stack_ptr;
    struct process* next;
    struct process* prev;
    struct process* sleep_next;
    char name[32];
} process_t;

typedef struct {
    uint64_t context_switches;
    uint64_t scheduler_ticks;
    uint64_t processes_created;
    uint64_t processes_terminated;
    uint64_t idle_time;
    uint64_t total_process_time;
} scheduler_stats_t;

typedef struct {
    uint64_t time_slice;
    uint8_t scheduling_policy;
    bool preemptive;
} scheduler_config_t;

void scheduler_init(void);
process_t* create_process(process_entry_t entry_point, const char* name, uint8_t priority);
void destroy_process(process_t* process);
void scheduler_add_process(process_t* process);
void scheduler_remove_process(process_t* process);
void scheduler_yield(void);
void process_exit(void);
void scheduler_sleep(uint64_t ticks);
void scheduler_wakeup(process_t* process);
void scheduler_tick(void);
void scheduler_run(uint64_t ticks);
void scheduler_dump_state(void);
process_t* scheduler_get_current(void);
scheduler_stats_t* get_scheduler_stats(void);

#endif
