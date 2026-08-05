#include "sched_fs.h"
#include "io.h"
#include "paging.h"
#include "serial.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>



#define TIME_SLICE   10u
#define MAX_PROCS    4u   
#define STACK_SIZE   4096u

typedef void (*process_entry_t)(void);

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} switch_frame_t;

typedef struct process {
    uint32_t esp;
    uint8_t* stack_base;
    process_entry_t entry;
    char name[16];
    int has_frame;
    uint32_t cr3;                    
} process_t;

static process_t g_procs[MAX_PROCS];
static uint8_t g_stacks[MAX_PROCS][STACK_SIZE];
static process_t* g_current;
static uint32_t g_nprocs;
static volatile uint32_t g_sched_ticks;
static volatile uint32_t g_switches;
static volatile uint32_t g_slice_left;
static volatile int g_need_switch;
static uint32_t g_kernel_cr3;

static void copy_name(char* dst, const char* src) {
    size_t i = 0;
    if (!src) {
        src = "?";
    }
    for (; src[i] != '\0' && i < 15u; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void setup_frame(process_t* proc) {
    uint32_t top = (uint32_t)proc->stack_base + STACK_SIZE;
    top &= ~0xFu;
    switch_frame_t* frame = (switch_frame_t*)(top - sizeof(switch_frame_t));

    frame->gs = 0x10;
    frame->fs = 0x10;
    frame->es = 0x10;
    frame->ds = 0x10;
    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->esp = 0;
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->int_no = 32;
    frame->err_code = 0;
    frame->eip = (uint32_t)proc->entry;
    frame->cs = 0x08;
    frame->eflags = 0x202; 

    proc->esp = (uint32_t)frame;
    proc->has_frame = 1;
}

static process_t* create_process_fs(const char* name, process_entry_t entry, int bootstrap) {
    if (g_nprocs >= MAX_PROCS) {
        return NULL;
    }

    process_t* proc = &g_procs[g_nprocs];
    proc->stack_base = g_stacks[g_nprocs];
    proc->entry = entry;
    copy_name(proc->name, name);
    proc->esp = 0;
    proc->has_frame = 0;
    proc->cr3 = g_kernel_cr3;

    if (!bootstrap) {
        setup_frame(proc);
    }

    ++g_nprocs;
    return proc;
}

void sched_fs_init(void) {
    g_nprocs = 0;
    g_sched_ticks = 0;
    g_switches = 0;
    g_slice_left = TIME_SLICE;
    g_need_switch = 0;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(g_kernel_cr3));

    g_current = create_process_fs("idle", NULL, 1);
    
}

void sched_fs_on_tick(void) {
    ++g_sched_ticks;
    if (g_slice_left > 0u) {
        --g_slice_left;
    }
    if (g_slice_left == 0u && g_nprocs > 1u) {
        g_slice_left = TIME_SLICE;
        g_need_switch = 1;
    }
}

uint32_t sched_fs_maybe_switch(uint32_t current_esp) {
    if (!g_need_switch || !g_current || g_nprocs < 2u) {
        return 0;
    }

    g_need_switch = 0;
    g_current->esp = current_esp;
    g_current->has_frame = 1;

    uint32_t idx = (uint32_t)(g_current - g_procs);
    idx = (idx + 1u) % g_nprocs;
    g_current = &g_procs[idx];
    if (g_current->cr3 != g_kernel_cr3) {
        __asm__ volatile ("mov %0, %%cr3" : : "r"(g_current->cr3));
    }

    ++g_switches;

    return g_current->esp;
}

uint32_t sched_fs_ticks(void) {
    return g_sched_ticks;
}

uint32_t sched_fs_switches(void) {
    return g_switches;
}


const char* sched_fs_current_name(void) {
    if (!g_current) {
        return "?";
    }
    return g_current->name;
}



void sched_fs_create_process(const char* name, void (*entry)(void), uint8_t priority) {
    (void)priority;
    process_t* p = create_process_fs(name, entry, /*bootstrap=*/0);
    if (!p) {
        return;
    }
}

void sched_fs_run(uint32_t ticks) {
    uint32_t start = timer_ticks();
    while ((timer_ticks() - start) < ticks) {
        __asm__ volatile ("hlt");
    }
}
