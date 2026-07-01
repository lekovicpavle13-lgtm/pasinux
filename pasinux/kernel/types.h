#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Basic memory types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// Process states
typedef enum {
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE,
    PROC_IDLE
} proc_state_t;

// Scheduling priorities
typedef enum {
    SCHED_PRIORITY_LOW = 0,
    SCHED_PRIORITY_NORMAL = 1,
    SCHED_PRIORITY_HIGH = 2
} sched_priority_t;

// Process Control Block (PCB) structure
struct process {
    void* stack_top;           // Top of process stack
    void* stack_bottom;        // Bottom of process stack
    proc_state_t state;        // Current process state
    sched_priority_t priority; // Scheduling priority
    struct process* next;      // Next process in list
    struct process* prev;      // Previous process in list
    uintptr_t kernel_sp;       // Kernel stack pointer
    uintptr_t user_sp;         // User stack pointer
    u64 process_id;            // Unique identifier
    void* entry_point;         // Entry function to execute
    char name[32];             // Process name for debugging
};

// Memory allocation type
struct heap_page {
    void* ptr;                 // Page pointer
    u32 size;                  // Page size
    struct heap_page* next;    // Next page in heap
    struct heap_page* prev;    // Previous page in heap
};

// Buffer descriptor for memory chunks
struct buffer {
    void* start;              // Start address
    u32 size;                 // Size in bytes
    struct buffer* next;      // Next buffer
    struct buffer* prev;      // Previous buffer
};

#endif // TYPES_H