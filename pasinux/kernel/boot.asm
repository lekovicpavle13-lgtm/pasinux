

bits 16
org 0x7c00

KERNEL_OFFSET  equ 0x10000
KERNEL_SEGMENT equ 0x1000
KERNEL_SECTORS equ 32         
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl

    mov si, msg_boot
    call print_string

   
    mov ax, KERNEL_SEGMENT
    mov es, ax
    xor bx, bx
    mov ah, 0x02               
    mov al, KERNEL_SECTORS
    mov ch, 0x00               
    mov cl, 0x02               
    mov dh, 0x00               
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    call enable_a20
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp dword 0x08:protected_entry

disk_error:
    mov si, msg_disk
    call print_string
.hang:
    cli
    hlt
    jmp .hang

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bh, 0x00
    int 0x10
    jmp print_string
.done:
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

bits 32
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp KERNEL_OFFSET

bits 16

boot_drive db 0

msg_boot db 'pasinux: loading kernel...', 13, 10, 0
msg_disk db 'pasinux: disk read failed', 13, 10, 0

align 8
gdt_start:
    dq 0x0000000000000000      
    dq 0x00CF9A000000FFFF      
    dq 0x00CF92000000FFFF     
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xaa55