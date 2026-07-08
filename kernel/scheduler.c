#include "scheduler.h"

#include "mm.h"

#include <stdio.h>
#include <string.h>

#define PROCESS_STACK_SIZE 4096u

static process_t idle_process;
static unsigned char idle_stack[PROCESS_STACK_SIZE];
static process_t* current_process;
static process_t* ready_queue;
static process_t* sleep_queue;
static uint64_t next_pid = 1;
static scheduler_stats_t scheduler_stats;
static scheduler_config_t scheduler_config = {
    .time_slice = 4,
    .scheduling_policy = 0,
    .preemptive = true,
};

static void copy_name(char* dst, const char* src) {
    if (!src || !src[0]) {
        src = "process";
    }

    size_t i = 0;
    for (; src[i] && i < 31u; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void idle_entry(void) {
}

// The ready queue is a circular doubly-linked list. 
static void ready_insert_tail(process_t* process) {
    if (process->next || process->prev) {
        return; // already enqueued
    }
    if (!ready_queue) {
        process->next = process;
        process->prev = process;
        ready_queue = process;
        return;
    }
    process_t* tail = ready_queue->prev;
    process->next = ready_queue;
    process->prev = tail;
    tail->next = process;
    ready_queue->prev = process;
}

static void ready_unlink(process_t* process) {
    if (!process->next || !process->prev) {
        return; // not enqueued
    }
    if (process->next == process) {
        ready_queue = NULL;
    } else {
        process->prev->next = process->next;
        process->next->prev = process->prev;
        if (ready_queue == process) {
            ready_queue = process->next;
        }
    }
    process->next = NULL;
    process->prev = NULL;
}

static bool in_ready_queue(process_t* process) {
    return process && process->next && process->prev;
}

static void sleep_unlink(process_t* process) {
    process_t** cursor = &sleep_queue;
    while (*cursor && *cursor != process) {
        cursor = &(*cursor)->sleep_next;
    }
    if (*cursor == process) {
        *cursor = process->sleep_next;
    }
    process->sleep_next = NULL;
}

process_t* scheduler_get_current(void) {
    return current_process;
}

void scheduler_init(void) {
    memset(&scheduler_stats, 0, sizeof(scheduler_stats));
    memset(&idle_process, 0, sizeof(idle_process));

    idle_process.pid = 0;
    idle_process.state = PROC_STATE_READY;
    idle_process.priority = 0;
    idle_process.entry_point = idle_entry;
    idle_process.stack_base = idle_stack;
    idle_process.stack_ptr = idle_stack + sizeof(idle_stack);
    copy_name(idle_process.name, "idle");

    current_process = &idle_process;
    ready_queue = NULL;
    sleep_queue = NULL;

    printf("[SCHED] scheduler ready\n");
}

process_t* create_process(process_entry_t entry_point, const char* name, uint8_t priority) {
    if (!entry_point) {
        return NULL;
    }

    process_t* process = (process_t*)kcalloc(1, sizeof(process_t));
    if (!process) {
        return NULL;
    }

    process->stack_base = kmalloc(PROCESS_STACK_SIZE);
    if (!process->stack_base) {
        kfree(process);
        return NULL;
    }

    process->pid = next_pid++;
    process->ppid = current_process && current_process != &idle_process ? current_process->pid : 0;
    process->state = PROC_STATE_READY;
    process->priority = priority;
    process->entry_point = entry_point;
    process->stack_ptr = (unsigned char*)process->stack_base + PROCESS_STACK_SIZE;
    copy_name(process->name, name);

    scheduler_add_process(process);
    scheduler_stats.processes_created++;

    printf("[SCHED] created %s pid=%llu priority=%u\n",
           process->name,
           (unsigned long long)process->pid,
           process->priority);

    return process;
}

void destroy_process(process_t* process) {
    if (!process || process == &idle_process) {
        return;
    }

    scheduler_remove_process(process);
    sleep_unlink(process);
    if (process->stack_base) {
        kfree(process->stack_base);
        process->stack_base = NULL;
    }

    printf("[SCHED] destroyed %s pid=%llu\n",
           process->name,
           (unsigned long long)process->pid);

    process->state = PROC_STATE_ZOMBIE;
    kfree(process);
    scheduler_stats.processes_terminated++;
}

void scheduler_add_process(process_t* process) {
    if (!process || process == &idle_process || in_ready_queue(process)) {
        return;
    }
    process->state = PROC_STATE_READY;
    ready_insert_tail(process);
}

void scheduler_remove_process(process_t* process) {
    if (!process || process == &idle_process) {
        return;
    }
    ready_unlink(process);
}

static process_t* pick_next(void) {
    if (!ready_queue) {
        return &idle_process;
    }

    process_t* best = ready_queue;
    if (scheduler_config.scheduling_policy == 1) {
        for (process_t* cursor = ready_queue->next; cursor != ready_queue; cursor = cursor->next) {
            // Prefer higher priority; break ties by lower accumulated cpu_time.
            if (cursor->priority > best->priority ||
                (cursor->priority == best->priority && cursor->cpu_time < best->cpu_time)) {
                best = cursor;
            }
        }
    } else {
        // Round-robin: advance the head so the same entry isn't chosen twice in a row.
        ready_queue = ready_queue->next;
    }
    return best;
}

void scheduler_yield(void) {
    if (!current_process || current_process == &idle_process) {
        return;
    }
    if (current_process->state == PROC_STATE_ZOMBIE) {
        return;
    }
    current_process->state = PROC_STATE_READY;
    scheduler_add_process(current_process);
}

void process_exit(void) {
    if (!current_process || current_process == &idle_process) {
        return;
    }
    current_process->state = PROC_STATE_ZOMBIE;
    ready_unlink(current_process);
}

void scheduler_sleep(uint64_t ticks) {
    if (!current_process || current_process == &idle_process) {
        return;
    }
    if (current_process->state == PROC_STATE_ZOMBIE) {
        return;
    }

    scheduler_remove_process(current_process);
    current_process->state = PROC_STATE_SLEEPING;
    current_process->wake_tick = scheduler_stats.scheduler_ticks + ticks;
    current_process->sleep_next = sleep_queue;
    sleep_queue = current_process;
}

void scheduler_wakeup(process_t* process) {
    if (!process || process->state != PROC_STATE_SLEEPING) {
        return;
    }
    sleep_unlink(process);
    scheduler_add_process(process);
}

void scheduler_tick(void) {
    scheduler_stats.scheduler_ticks++;

    process_t* sleep = sleep_queue;
    while (sleep) {
        process_t* next = sleep->sleep_next;
        if (sleep->wake_tick <= scheduler_stats.scheduler_ticks) {
            scheduler_wakeup(sleep);
        }
        sleep = next;
    }
}

void scheduler_run(uint64_t ticks) {
    uint64_t slice_used = 0;

    for (uint64_t i = 0; i < ticks; i++) {
        scheduler_tick();

      
        if (scheduler_config.preemptive &&
            current_process && current_process != &idle_process &&
            current_process->state == PROC_STATE_RUNNING &&
            slice_used >= scheduler_config.time_slice) {
            current_process->state = PROC_STATE_READY;
            scheduler_add_process(current_process);
            slice_used = 0;
        }

        process_t* next = pick_next();
        process_t* old = current_process;
        current_process = next;
        current_process->state = PROC_STATE_RUNNING;
        current_process->cpu_time++;
        slice_used++;

        if (current_process == &idle_process) {
            scheduler_stats.idle_time++;
        } else {
            scheduler_stats.total_process_time++;
        }

        if (old != current_process) {
            scheduler_stats.context_switches++;
            printf("[SCHED] switch %s -> %s\n",
                   old ? old->name : "none",
                   current_process->name);
            slice_used = 1;
        }

        if (current_process->entry_point) {
            current_process->entry_point();
        }

        if (current_process != &idle_process && current_process->state == PROC_STATE_RUNNING) {
           
            current_process->state = PROC_STATE_READY;
            if (!in_ready_queue(current_process)) {
                ready_insert_tail(current_process);
            }
        }
    }
}

void scheduler_dump_state(void) {
    printf("[SCHED] ticks=%llu switches=%llu created=%llu terminated=%llu idle=%llu work=%llu\n",
           (unsigned long long)scheduler_stats.scheduler_ticks,
           (unsigned long long)scheduler_stats.context_switches,
           (unsigned long long)scheduler_stats.processes_created,
           (unsigned long long)scheduler_stats.processes_terminated,
           (unsigned long long)scheduler_stats.idle_time,
           (unsigned long long)scheduler_stats.total_process_time);

    printf("[SCHED] ready queue:");
    if (!ready_queue) {
        printf(" empty\n");
        return;
    }

    process_t* cursor = ready_queue;
    do {
        printf(" %s(pid=%llu,pri=%u,state=%u)",
               cursor->name,
               (unsigned long long)cursor->pid,
               cursor->priority,
               cursor->state);
        cursor = cursor->next;
    } while (cursor != ready_queue);

    printf("\n");
}

scheduler_stats_t* get_scheduler_stats(void) {
    return &scheduler_stats;
}
