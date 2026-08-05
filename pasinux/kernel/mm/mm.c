#include "mm.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define NUM_SIZE_CLASSES 6
static const size_t size_classes[NUM_SIZE_CLASSES] = {8, 16, 32, 64, 128, 256};

typedef union heap_align {
    long double ld;
    void* ptr;
    uint64_t u64;
} heap_align_t;

typedef struct block {
    size_t size;
    bool in_use;
    struct block* next;
    struct block* prev;
} block_t;

static heap_align_t heap_storage[(KERNEL_HEAP_SIZE + sizeof(heap_align_t) - 1) / sizeof(heap_align_t)];
static block_t* block_list;
static block_t* free_lists[NUM_SIZE_CLASSES];
static mem_stats_t stats;

static size_t align16(size_t value) {
    return (value + 15u) & ~(size_t)15u;
}

static void update_peak(void) {
    if (stats.current_usage > stats.peak_usage) {
        stats.peak_usage = stats.current_usage;
    }
}


static void block_list_remove(block_t* block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        block_list = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->next = NULL;
    block->prev = NULL;
}


static void block_list_insert_front(block_t* block) {
    block->prev = NULL;
    block->next = block_list;
    if (block_list) {
        block_list->prev = block;
    }
    block_list = block;
}


static int size_class_index(size_t size) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= size_classes[i]) {
            return i;
        }
    }
    return -1;
}


static void free_list_push(int class, block_t* block) {
    block->next = (void*)free_lists[class];
    block->prev = NULL;
    if (free_lists[class]) {
        free_lists[class]->prev = block;
    }
    free_lists[class] = block;
}


static block_t* free_list_pop(int class) {
    block_t* block = free_lists[class];
    if (!block) {
        return NULL;
    }
    free_lists[class] = (block_t*)block->next;
    if (free_lists[class]) {
        free_lists[class]->prev = NULL;
    }
    return block;
}

static void split_block(block_t* block, size_t size) {
    size_t remaining = block->size - size;
    if (remaining <= sizeof(block_t) + 16u) {
        return;
    }

    block_t* split = (block_t*)((unsigned char*)block + sizeof(block_t) + size);
    split->size = remaining - sizeof(block_t);
    split->in_use = false;
    split->next = block->next;
    split->prev = block;

    if (split->next) {
        split->next->prev = split;
    }

    block->size = size;
    block->next = split;
}

static void coalesce(block_t* block) {
    if (!block || block->in_use) {
        return;
    }

    
    if (block->prev && !block->prev->in_use) {
        block->size += sizeof(block_t) + block->prev->size;
        block->prev = block->prev->prev;
        if (block->prev) {
            block->prev->next = block;
        }
    }

    
    while (block->next && !block->next->in_use) {
        block_t* next = block->next;
        block->size += sizeof(block_t) + next->size;
        block->next = next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }
}

void init_memory(void) {
    memset(&stats, 0, sizeof(stats));
    memset(heap_storage, 0, sizeof(heap_storage));

    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        free_lists[i] = NULL;
    }

    block_list = (block_t*)heap_storage;
    block_list->size = sizeof(heap_storage) - sizeof(block_t);
    block_list->in_use = false;
    block_list->next = NULL;
    block_list->prev = NULL;

    printf("[MM] heap ready: %u bytes\n", (unsigned)sizeof(heap_storage));
}

void* kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    if (!block_list) {
        init_memory();
    }

    size = align16(size);

    
    if (size <= size_classes[NUM_SIZE_CLASSES - 1]) {

        int class = 0;
        while (size > size_classes[class] && class < NUM_SIZE_CLASSES) {
            class++;
        }

        if (class < NUM_SIZE_CLASSES) {
            block_t* block = free_list_pop(class);
            if (block) {
                stats.current_usage += block->size;
                stats.total_allocated += block->size;
                stats.allocation_count++;
                block->in_use = true;
                block_list_insert_front(block);
                update_peak();
                return (unsigned char*)block + sizeof(block_t);
            }
        }
    }

    for (block_t* block = block_list; block; block = block->next) {
        if (!block->in_use && block->size >= size) {
            split_block(block, size);
            block->in_use = true;
            stats.total_allocated += block->size;
            stats.current_usage += block->size;
            stats.allocation_count++;
            update_peak();
            return (unsigned char*)block + sizeof(block_t);
        }
    }

    stats.failed_allocations++;
    printf("[MM] allocation failed: %zu bytes\n", size);
    return NULL;
}

void* kcalloc(size_t nmemb, size_t size) {
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        stats.failed_allocations++;
        return NULL;
    }

    size_t total = nmemb * size;
    void* ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) {
        return kmalloc(size);
    }
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    block_t* block = (block_t*)((unsigned char*)ptr - sizeof(block_t));
    size_t copy_size = block->size < size ? block->size : size;
    void* next = kmalloc(size);
    if (!next) {
        return NULL;
    }

    memcpy(next, ptr, copy_size);
    kfree(ptr);
    return next;
}

void kfree(void* ptr) {
    if (!ptr) {
        return;
    }

    block_t* block = (block_t*)((unsigned char*)ptr - sizeof(block_t));
    if (!block->in_use) {
        printf("[MM] ignored double free at %p\n", ptr);
        return;
    }

    block->in_use = false;
    stats.total_freed += block->size;
    stats.current_usage -= block->size;
    stats.free_count++;
    coalesce(block);


    int class = size_class_index(block->size);
    if (class >= 0) {
        block_list_remove(block);
        free_list_push(class, block);
    }
}

mem_stats_t get_memory_stats(void) {
    return stats;
}

void print_memory_stats(void) {
    printf("[MM] allocations=%llu frees=%llu current=%llu peak=%llu failed=%llu\n",
           (unsigned long long)stats.allocation_count,
           (unsigned long long)stats.free_count,
           (unsigned long long)stats.current_usage,
           (unsigned long long)stats.peak_usage,
           (unsigned long long)stats.failed_allocations);
}
