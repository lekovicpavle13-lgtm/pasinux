global bss_start
global bss_end
global kernel_main

extern _kernel_main

section .data
bss_start: dd 0
bss_end: dd 0

section .text
kernel_main:
    jmp _kernel_main

