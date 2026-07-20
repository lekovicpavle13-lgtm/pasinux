#include "serial.h"
#include "vga.h"
void kernel_main(void) {
serial_init();
vga_init();
serial_puts("OK from Serial\n");
vga_puts("================================\n", VGA_LIGHT_CYAN);
vga_puts("   PASINUX KERNEL BOOTED!   \n", VGA_WHITE);
vga_puts("================================\n", VGA_LIGHT_CYAN);
vga_puts("> Serial: Initialized\n", VGA_GREEN);
vga_puts("> VGA:    Initialized\n", VGA_GREEN);
vga_puts("> Status: Ready.\n", VGA_LIGHT_GREY);
while (1) {
asm volatile("hlt");
}
}