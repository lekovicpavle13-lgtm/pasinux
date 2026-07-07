BITS 16
ORG 0x7C00

KERNEL_SCRATCH_SEG   equ 0x1000      ; scratch buffer at 0x10000 (real mode seg:0)
KERNEL_LOAD_ADDR     equ 0x100000    ; 1 MiB - must match linker.ld
KERNEL_SECTORS       equ 128         ; placeholder: 128 * 512 = 64 KiB. Raise as needed.

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; stack grows down from just below us
    sti

    mov [boot_drive], dl    ; BIOS passes boot drive number in dl

    call enable_a20
    call load_kernel_lba
    call enter_protected_mode
    ; never returns - enter_protected_mode jumps into pmode_entry below

; ---------------------------------------------------------------
; Enable the A20 line via the fast A20 method (works on most BIOSes/
; QEMU). If you target real old hardware you may need the keyboard
; controller method as a fallback.
; ---------------------------------------------------------------
enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; ---------------------------------------------------------------
; Load KERNEL_SECTORS sectors starting at LBA 1 into
; KERNEL_SCRATCH_SEG:0x0000, using INT 13h extended read (works
; regardless of CHS geometry; QEMU and modern BIOSes support this).
; ---------------------------------------------------------------
load_kernel_lba:
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    ret

disk_error:
    mov si, disk_error_msg
.print:
    lodsb
    or al, al
    jz .hang
    mov ah, 0x0E
    int 0x10
    jmp .print
.hang:
    cli
    hlt
    jmp .hang

disk_error_msg db "pasinux: disk read error", 0
boot_drive     db 0

; Disk Address Packet for INT 13h/42h
align 4
dap:
    db 0x10             ; packet size
    db 0                ; reserved
    dw KERNEL_SECTORS   ; number of sectors to read
    dw 0x0000           ; offset into scratch segment
    dw KERNEL_SCRATCH_SEG ; scratch segment
    dq 1                ; starting LBA (sector 1 = right after boot sector)

; ---------------------------------------------------------------
; Switch to 32-bit protected mode and jump to pmode_entry.
; ---------------------------------------------------------------
enter_protected_mode:
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:pmode_entry

BITS 32
pmode_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x9FC00        ; temporary pmode stack, well below 1 MiB

    ; Copy the kernel from the real-mode scratch buffer
    ; (KERNEL_SCRATCH_SEG:0 -> linear KERNEL_SCRATCH_SEG*16)
    ; up to KERNEL_LOAD_ADDR.
    mov esi, KERNEL_SCRATCH_SEG * 16
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, (KERNEL_SECTORS * 512) / 4
    rep movsd

    jmp CODE_SEG:KERNEL_LOAD_ADDR   ; hand off to entry.asm's _start

; ---------------------------------------------------------------
; Minimal flat GDT: null, 32-bit code, 32-bit data.
; ---------------------------------------------------------------
align 8
gdt_start:
    dq 0x0000000000000000          ; null descriptor

gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55