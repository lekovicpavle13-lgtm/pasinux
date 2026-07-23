#include "mm.h"
#include "serial.h"
#include "vga.h"

typedef struct block_header {
    unsigned int size;
    unsigned int is_free;
    struct block_header* next;
} block_header_t;

static unsigned char* heap_start = (unsigned char*)0x100000;
static unsigned char* heap_end = (unsigned char*)0x200000;
static block_header_t* free_list = (block_header_t*)0x100000;

static unsigned int total_allocs = 0;
static unsigned int total_frees = 0;

void init_memory(void) {
    free_list->size = (unsigned int)(heap_end - heap_start) - sizeof(block_header_t);
    free_list->is_free = 1;
    free_list->next = 0;
    vga_puts("[MM] Heap initialized.\n", VGA_GREEN);
}

static void split_block(block_header_t* block, unsigned int size) {
    block_header_t* new_block = (block_header_t*)((unsigned char*)block + sizeof(block_header_t) + size);
    new_block->size = block->size - size - sizeof(block_header_t);
    new_block->is_free = 1;
    new_block->next = block->next;
    block->size = size;
    block->next = new_block;
}

void* kmalloc(unsigned int size) {
    if (size == 0) return 0;
    
    block_header_t* current = free_list;
    while (current) {
        if (current->is_free && current->size >= size) {
            if (current->size > size + sizeof(block_header_t) + 4) {
                split_block(current, size);
            }
            current->is_free = 0;
            total_allocs++;
            return (void*)((unsigned char*)current + sizeof(block_header_t));
        }
        current = current->next;
    }
    return 0;
}

void kfree(void* ptr) {
    if (!ptr) return;
    block_header_t* block = (block_header_t*)((unsigned char*)ptr - sizeof(block_header_t));
    block->is_free = 1;
    total_frees++;
}

void* kcalloc(unsigned int num, unsigned int size) {
    unsigned int total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) {
        unsigned char* byte_ptr = (unsigned char*)ptr;
        for (unsigned int i = 0; i < total; i++) {
            byte_ptr[i] = 0;
        }
    }
    return ptr;
}

void print_memory_stats(void) {
    vga_puts("[MM] Allocs: ", VGA_LIGHT_GREY);
    serial_puts("[MM] Allocs: ");
    
    char buf[16];
    int i = 0;
    unsigned int num = total_allocs;
    
    if (num == 0) {
        vga_putchar('0', VGA_LIGHT_GREY);
        serial_putchar('0');
    } else {
        while (num > 0) {
            buf[i++] = '0' + (num % 10);
            num /= 10;
        }
        for (int j = i - 1; j >= 0; j--) {
            vga_putchar(buf[j], VGA_LIGHT_GREY);
            serial_putchar(buf[j]);
        }
    }
    vga_putchar('\n', VGA_LIGHT_GREY);
    serial_putchar('\n');
}
