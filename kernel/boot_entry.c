void _start() {
    asm volatile("mov %%esp, %0" : : "r"(0x90000));
    
    extern void kernel_main(void);
    kernel_main();
    
    while(1) {
        asm volatile("hlt");
    }
}
