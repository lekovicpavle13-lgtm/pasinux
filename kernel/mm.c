#include "mm.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t state;
    size_t size;
    struct page* next;
} page;

static page* free_list = NULL;
static page* heap_start = NULL;
static page* heap_end = NULL;
static size_t total_heap_size = 0;

static size_t allocations = 0;
static size_t frees = 0;
static size_t allocations_failed = 0;

void init_memory(size_t size) {
    total_heap_size = size;
    heap_start = (page*)heap_alloc(size);
    heap_end = (page*)((char*)heap_start + size);

    free_list = heap_start;
    free_list->state = 0;
    free_list->size = size;
    free_list->next = NULL;
}

void* kmalloc(size_t size) {
        size = (size + 7) & ~7;

        if (size == 0) return NULL;

    page* current = free_list;
    page* prev = NULL;
    while (current != NULL) {
        if (current->size >= size) {
            // Found suitable block
            break;
        }
        prev = current;
        current = current->next;
    }

    
    if (current == NULL) {
        current = heap_expand(size);
        if (current == NULL) return NULL;
    }

    // Check if remaining space is sufficient for splitting
    size_t remaining_size = current->size - size;
    if (remaining_size >= 16) {  // Minimum block size
        // Split the block
        page* new_page = (page*)((char*)current + size);
        new_page->state = 0;  // Free
        new_page->size = remaining_size;
        new_page->next = current->next;

        current->size = size;
        current->next = new_page;
    }

    
    current->state = 1;

    
    allocations++;
    return current->data;
}

void* kcalloc(size_t size, size_t count) {
    void* ptr = kmalloc(size * count);
    if (ptr) {
        memset(ptr, 0, size * count);
    }
    return ptr;
}

void kfree(void* ptr) {
    if (ptr == NULL) return;
 
    page* page_header = (page*)ptr - 1;

    // Mark as free
    page_header->state = 0;
    frees++;

    
    coalesce_free_blocks(page_header);
}

void print_memory_stats() {
    size_t free_blocks = 0;
    size_t total_free = 0;
    page* current = free_list;

    while (current != NULL) {
        free_blocks++;
        total_free += current->size;
        current = current->next;
    }


    page* alloc_blocks = heap_start;  
    size_t allocated_blocks = 0;
    size_t total_allocated = 0;

    while (alloc_blocks != NULL) {
        if (alloc_blocks->state) {
            allocated_blocks++;
            total_allocated += alloc_blocks->size;
        }
        alloc_blocks = alloc_blocks->next;
    }

    
    #ifdef _WIN32
    #include <stdio.h>
    printf("[MM] Heap: %zu bytes total, %zu bytes free, %zu allocated blocks\n",
           total_heap_size, total_free, allocated_blocks);
    #endif
}


void coalesce_free_blocks(page* current) {
        
    return;
}


page* heap_expand(size_t size) {
    return NULL;  }


page* get_page_for(void* ptr) {
    return (page*)ptr - 1;  
}