
bits 32
section .text


global _start
extern _kmain

_start:
    mov esp, stack_top
    call _kmain

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
