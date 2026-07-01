; boot.asm - tiny legacy BIOS boot sector placeholder for pasinux.
; The C code currently builds as a hosted kernel-core simulator. This sector is
; kept valid so future real-mode boot work starts from known-good assembly.

bits 16
org 0x7c00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov si, message
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0e
    mov bh, 0x00
    int 0x10
    jmp .print

.halt:
    cli
    hlt
    jmp .halt

message db 'pasinux: build kernel_sim from C sources first', 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xaa55
