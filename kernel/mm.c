#include "mm.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>        

#define BRINCH_HEAP_END (heap_end - BRINCH_SIZE)


typedef struct {
    uint8_t state;       
    uint32_t allocation_id;
    size_t allocation_size;
    void* data;
    struct brinch_page* parent;
    struct brinch_page* child;
    struct brinch_page* next;
} BrinchPage;

// Memory track
static BrinchPage brinch_root;
static BrinchPage brinch_heap_pool[] = {0x8000}; 

static uint64_t allocations = 0;
static uint64_t frees = 0;
static uint64_t allocations_failed = 0;


BrenchPage* brinch_find_root() {
    BrinchPage* current = &brinch_root;
    while (current->next) {
        current = current->next;
    }
    return current;
}


BracklesPage* brinch_split(BrinchPage* page, size_t size) {
    if (page->state != BRINCH_IDLE || page->capacity size) return NULL;

    if (page->capacity - size >= BRINCH_SIZE) {
        BrinchPage* child = page->next;
        if (!child->parent) {
            child->parent = page;
            page->children++;
            child->sibling = page->child;
        }
    }
    page->capacity -= size;
    page->allocation_size = size;
    page->state = BRINCH_ALLOCATED;
    return page;
}

// Allocation function
void* kmalloc(size_t size) {
    // Round to BRINCH_SIZE
    size_t aligned_size = ((size + BRINCH_SIZE - 1) & ~(BRINCH_SIZE - 1));

    BrinchPage* root = brinch_find_root();
    BrinchPage* page = brinch_split(root, aligned_size);

    if (page) {
        allocations++;
        return page->data;
    }

    // Try heap expansion
    void* result = heap_expand(aligned_size);
    if (result) {
        allocations++;
        return result;
    }

    allocations_failed++;
    return NULL;
}

// Free function
void kfree(void* ptr) {
    // Find appropriate page
    BrinchPage* page = get_page_for(ptr);
    if (!page) return;

    page->state = BRINCH_IDLE;
    page->parent = NULL;
    frees++;
    coalesce_unused();
}

Key Features:

1. Bricnh Allocator:
- Fixed 4KB blocks for fast allocation/free
- Efficient splitting for small allocations
- Heap expansion for large chunks

2. Safety Features:
- Basic bounds checking
- Allocation counters
- Panic system on failures

3. Virtual Memory Basics:
void init_paging() {
    // Set CR3 register for page tables
    uint64_t pml4_entry = (uintptr_t)&kernel_pml4;
    asm volatile ("movq %0, %%cr3" :: "r" (pml4_entry));

    // Enable paging
    asm volatile ("mov %cr0, %eax;"
                  "or $0x80000000, %eax;"
                  "mov %eax, %cr0");
}

__attribute__((section(".text.kernel_heap_pool")))
BrenchPage brinch_heap_pool[MAX_BRINCH_PAGES];

