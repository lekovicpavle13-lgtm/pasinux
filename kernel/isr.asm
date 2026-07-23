

BITS 32

extern isr_handler
extern irq_handler

section .text

; ---- ISR stub generators -------------------------------------------
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0         ; dummy error code, so stack shape matches
                          ; the vectors that push a real one
    push dword %1         ; interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1         ;
    jmp isr_common_stub
%endmacro

; CPU exceptions 0-31. Vectors 8, 10-14, 17, 21 push an error code
; themselves; the rest do not.
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; ---- IRQ stub generator ---------------------------------------------
; Hardware IRQs 0-15, remapped to interrupt vectors 32-47 by
; pic_remap() in idt.c. Arg 1 = IRQ line, arg 2 = interrupt vector.
%macro IRQ 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp irq_common_stub
%endmacro

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; ---- Common stubs -----------------------------------------------------
; Both build the same struct registers layout on the stack, then call
; the matching C dispatcher. The 0x10 selector below is the kernel
; data segment - must match DATA_SEG in boot.asm's GDT.
isr_common_stub:
    pusha
    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call isr_handler

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8          ; discard int_no and err_code pushed by the stub
    iret

irq_common_stub:
    pusha
    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call irq_handler

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8
    iret

; ---- idt_flush(uint32_t idt_ptr_addr): loads the IDT via LIDT --------
global idt_flush
idt_flush:
    mov eax, [esp + 4]   ; cdecl: first (only) argument
    lidt [eax]
    ret
