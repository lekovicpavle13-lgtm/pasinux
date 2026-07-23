#include "serial.h"
#include "vga.h"
#include "mm.h"

void kernel_main(void) {
    serial_init();
    vga_init();

    vga_puts("================================\n", VGA_LIGHT_CYAN);
    vga_puts("   PASINUX KERNEL BOOTED!   \n", VGA_WHITE);
    vga_puts("================================\n", VGA_LIGHT_CYAN);

    init_memory();

    char* test1 = (char*)kmalloc(32);
    char* test2 = (char*)kmalloc(64);
    
    if (test1 && test2) {
        vga_puts("[MM] kmalloc SUCCESS!\n", VGA_LIGHT_GREEN);
    } else {
        vga_puts("[MM] kmalloc FAILED!\n", VGA_RED);
    }

    kfree(test1);
    vga_puts("[MM] kfree SUCCESS!\n", VGA_LIGHT_GREEN);

    print_memory_stats();

    while (1) {
        asm volatile("hlt");
    }
}
