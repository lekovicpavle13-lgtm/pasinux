#include "mm_fs.h"
#include "serial.h"
#include "io.h"
#include <stdbool.h>

static void _memset_fs(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0u; i < n; ++i) p[i] = (unsigned char)c;
}

static void _memcpy_fs(void* d, const void* s, size_t n) {
    const unsigned char* sp = (const unsigned char*)s;
    unsigned char* dp = (unsigned char*)d;
    for (size_t i = 0u; i < n; ++i) dp[i] = sp[i];
}

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

static heap_align_t heap_storage[(KERNEL_HEAP_SIZE + sizeof(heap_align_t) - 1u) / sizeof(heap_align_t)];
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
    for (int i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (size <= size_classes[i]) return i;
    }
    return -1;
}

static void free_list_push(int class_idx, block_t* block) {
    block->next = (void*)free_lists[class_idx];
    block->prev = NULL;
    if (free_lists[class_idx]) {
        free_lists[class_idx]->prev = block;
    }
    free_lists[class_idx] = block;
}

static block_t* free_list_pop(int class_idx) {
    block_t* block = free_lists[class_idx];
    if (!block) return NULL;
    free_lists[class_idx] = (block_t*)block->next;
    if (free_lists[class_idx]) {
        free_lists[class_idx]->prev = NULL;
    }
    return block;
}

static void split_block(block_t* block, size_t size) {
    size_t remaining = block->size - size;
    if (remaining <= sizeof(block_t) + 16u) return;

    block_t* split = (block_t*)((unsigned char*)block + sizeof(block_t) + size);
    split->size = remaining - sizeof(block_t);
    split->in_use = false;
    split->next = block->next;
    split->prev = block;

    if (split->next) split->next->prev = split;

    block->size = size;
    block->next = split;
}

static void coalesce(block_t* block) {
    if (!block || block->in_use) return;

    /* Coalesce with previous free block */
    if (block->prev && !block->prev->in_use) {
        block->size += sizeof(block_t) + block->prev->size;
        block->prev = block->prev->prev;
        if (block->prev) block->prev->next = block;
    }
    while (block->next && !block->next->in_use) {
        block_t* next = block->next;
        block->size += sizeof(block_t) + next->size;
        block->next = next->next;
        if (block->next) block->next->prev = block;
    }
}

void init_memory(void) {
    _memset_fs(&stats, 0, sizeof(stats));
    _memset_fs(heap_storage, 0, sizeof(heap_storage));

    for (int i = 0; i < NUM_SIZE_CLASSES; ++i) {
        free_lists[i] = NULL;
    }

    block_list = (block_t*)heap_storage;
    block_list->size = sizeof(heap_storage) - sizeof(block_t);
    block_list->in_use = false;
    block_list->next = NULL;
    block_list->prev = NULL;

    serial_puts("[MM] heap ready: ");
    serial_put_u32(KERNEL_HEAP_SIZE);
    serial_puts(" bytes\n");
}

void* kmalloc(size_t size) {
    if (size == 0u) return NULL;
    if (!block_list) init_memory();

    size = align16(size);

    if (size <= size_classes[NUM_SIZE_CLASSES - 1u]) {
        int class_idx = 0;
        while (size > size_classes[class_idx] && class_idx < NUM_SIZE_CLASSES) {
            ++class_idx;
        }
        if (class_idx < NUM_SIZE_CLASSES) {
            block_t* block = free_list_pop(class_idx);
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

    /* Linear scan for a suitable free block */
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
    serial_puts("[MM] allocation failed: ");
    serial_put_u32((uint32_t)size);
    serial_puts(" bytes\n");
    return NULL;
}

void* kcalloc(size_t nmemb, size_t size) {
    if (nmemb != 0u && size > ((size_t)-1) / nmemb) {
        stats.failed_allocations++;
        return NULL;
    }
    size_t total = nmemb * size;
    void* ptr = kmalloc(total);
    if (ptr) _memset_fs(ptr, 0, total);
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0u) { kfree(ptr); return NULL; }

    block_t* block = (block_t*)((unsigned char*)ptr - sizeof(block_t));
    size_t copy_size = block->size < size ? block->size : size;
    void* next = kmalloc(size);
    if (!next) return NULL;

    _memcpy_fs(next, ptr, copy_size);
    kfree(ptr);
    return next;
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_t* block = (block_t*)((unsigned char*)ptr - sizeof(block_t));
    if (!block->in_use) {
        serial_puts("[MM] ignored double free\n");
        return;
    }

    block->in_use = false;
    stats.total_freed += block->size;
    stats.current_usage -= block->size;
    stats.free_count++;
    coalesce(block);

    int class_idx = size_class_index(block->size);
    if (class_idx >= 0) {
        block_list_remove(block);
        free_list_push(class_idx, block);
    }
}

mem_stats_t get_memory_stats(void) {
    return stats;
}

void print_memory_stats(void) {
    serial_puts("[MM] allocations=");
    serial_put_u32((uint32_t)stats.allocation_count);
    serial_puts(" frees=");
    serial_put_u32((uint32_t)stats.free_count);
    serial_puts(" current=");
    serial_put_u32((uint32_t)stats.current_usage);
    serial_puts(" peak=");
    serial_put_u32((uint32_t)stats.peak_usage);
    serial_puts(" failed=");
    serial_put_u32((uint32_t)stats.failed_allocations);
    serial_puts("\n");
}