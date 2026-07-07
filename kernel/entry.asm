

BITS 32

section .text.entry
global _start
extern kernel_main
extern _bss_start
extern _bss_end

_start:
    mov esp, stack_top      ; switch to our own stack
    mov ebp, esp

    ; Zero .bss: for (p = _bss_start; p < _bss_end; p++) *p = 0;
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

    call kernel_main

    ; kernel_main() should not return, but if it does, halt cleanly
    ; rather than executing garbage.
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384              ; 16 KiB kernel stack - raise if you hit overflow
stack_top:
