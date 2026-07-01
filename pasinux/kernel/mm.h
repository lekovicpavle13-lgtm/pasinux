#ifndef MM_H
#define MM_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_HEAP_SIZE (1024u * 1024u)

typedef struct {
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t current_usage;
    uint64_t peak_usage;
    uint64_t allocation_count;
    uint64_t free_count;
    uint64_t failed_allocations;
} mem_stats_t;

void init_memory(void);
void* kmalloc(size_t size);
void* kcalloc(size_t nmemb, size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);
mem_stats_t get_memory_stats(void);
void print_memory_stats(void);

#endif
