; =============================================================================
; pasinux FAT12 boot loader
;
; Boots a real 1.44 MB FAT12 floppy. Reads the root directory, finds the
; KERNEL.BIN file, walks its FAT cluster chain, loads it to 0x10000, then
; enters protected mode and jumps to it.
;
; Geometry (echoed by mkimage.py so the packed image matches this loader):
;   bytes/sector       = 512
;   sectors/cluster    = 1
;   reserved sectors   = 1   (this boot sector)
;   number of FATs     = 2
;   root entries       = 224
;   total sectors      = 2880  (1.44 MB)
;   sectors/FAT        = 9
; =============================================================================
bits 16
org 0x7c00

final:
    jmp short start
    nop

; -- BIOS Parameter Block (FAT12 / 1.44 MB) ----------------------------------
oem             db 'PASINUX '          ; 8 bytes
bpb_bytes_sec   dw 512
bpb_sec_per_clu db 1
bpb_resvd_secs  dw 1
bpb_num_fats    db 2
bpb_root_ents   dw 224
bpb_tot_sec16   dw 2880
bpb_media       db 0xF0
bpb_fat_size16  dw 9
bpb_sec_per_trk dw 18
bpb_heads       dw 2
bpb_hidden      dd 0
bpb_tot_sec32   dd 0
bpb_drv_num     db 0
bpb_reserved    db 0
bpb_boot_sig    db 0x29
bpb_vol_id      dd 0x12345678
bpb_vol_label   db 'PASINUX    '       ; 11 bytes (7 + 4 spaces)
bpb_fs_type     db 'FAT12   '          ; 8 bytes

KERNEL_LOAD_ADDR equ 0x10000      ; flat: 0x1000:0  (must match linker.ld)

; Real-mode scratch segments. These live above the kernel's load target
; (0x10000 up to ~0x1C800 with a 100-sector kernel) and below 1 MiB, so
; neither the root dir nor the FAT collides with the load.
ROOT_SEG       equ 0x2000         ; root directory buffer  (0x20000)
FAT_SEG        equ 0x2000         ; FAT buffer, reused after the search
KERNEL_SEGMENT equ 0x1000         ; kernel load segment     (0x1000:0 = 0x10000)

BYTES_PER_SECTOR equ 512
SECTORS_PER_FAT  equ 9
ROOT_ENTRIES     equ 224
NUM_FATS         equ 2
RESERVED_SECTORS equ 1
ROOT_DIR_SECTORS equ 14           ; (224*32)/512
ROOT_DIR_LBA     equ 19           ; 1 + 2*9
DATA_LBA         equ 33           ; 19 + 14

KERNEL_FNAME db 'KERNEL  BIN'     ; 8.3: "KERNEL" + 2 spc + "BIN"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl          ; BIOS passes floppy drive number in dl

    mov ax, 0x0003
    int 0x10                      ; 80x25 text mode

    call enable_a20

    ; 1. Load the root directory, find KERNEL.BIN --------------------------
    mov ax, ROOT_SEG
    mov es, ax
    xor bx, bx
    mov ax, ROOT_DIR_LBA
    mov cx, ROOT_DIR_SECTORS
    call read_sectors
    jc  boot_error

    call find_kernel              ; sets [first_cluster], [file_size]
    jc  boot_error

    ; 2. Load FAT #1 -------------------------------------------------------
    mov ax, FAT_SEG
    mov es, ax
    xor bx, bx
    mov ax, 1                     ; FAT #1 starts at LBA 1
    mov cx, SECTORS_PER_FAT
    call read_sectors
    jc  boot_error

    ; 3. Walk the cluster chain -> 0x1000:0 --------------------------------
    mov ax, KERNEL_SEGMENT
    mov es, ax
    xor di, di                    ; es:di = kernel load pointer
    mov bp, [first_cluster]       ; bp = current cluster

.load_loop:
    ; LBA = DATA_LBA + (cluster - 2)
    mov ax, bp
    sub ax, 2
    mov cx, DATA_LBA
    add ax, cx

    mov bx, di
    mov cx, 1                     ; sectors_per_cluster == 1
    call read_sectors             ; read 1 sector at LBA ax -> es:di
    jc  boot_error

    add di, BYTES_PER_SECTOR

    ; next = FAT[cluster]
    mov ax, bp
    call next_cluster             ; ax = next cluster
    cmp ax, 0x0FF8                ; >= 0xFF8 marks end-of-chain
    jae .done

    mov bp, ax
    ; next_cluster clobbered es; restore the load segment.
    mov ax, KERNEL_SEGMENT
    mov es, ax
    jmp .load_loop
.done:
    jmp switch_to_pmode

; ---------------------------------------------------------------------------
; find_kernel: search the root dir buffer at ROOT_SEG for KERNEL.BIN.
; Sets [first_cluster] and [file_size]. CF set on not-found.
; clobbers: ax, bx, cx, si, di, es
; ---------------------------------------------------------------------------
find_kernel:
    mov ax, ROOT_SEG
    mov es, ax
    xor di, di                    ; di = current 32-byte entry
    mov cx, ROOT_ENTRIES
.loop:
    mov bl, [es:di]
    cmp bl, 0x00                  ; 0x00 = no more entries
    je  .not_found
    cmp bl, 0xE5                  ; 0xE5 = deleted entry, skip
    je  .advance
    ; compare the 11-byte name
    pusha
    mov si, KERNEL_FNAME
    mov cx, 11
    repe cmpsb                    ; ds:si vs es:di
    popa
    je  .found
.advance:
    add di, 32
    dec cx
    jnz .loop
    ; fall-through = not found
.not_found:
    stc
    ret
.found:
    mov ax, [es:di + 26]          ; first cluster (offset 26)
    mov [first_cluster], ax
    clc
    ret

; ---------------------------------------------------------------------------
; next_cluster: cluster in ax, returns next cluster in ax.
; Uses the FAT already cached at FAT_SEG.
; clobbers: bx, cx, dx, es
; ---------------------------------------------------------------------------
next_cluster:
    mov cx, ax                    ; save cluster (parity check after clobbering)
    mov bx, ax
    shr bx, 1
    add bx, ax                    ; bx = byte offset = cluster*3/2
    mov ax, FAT_SEG
    mov es, ax
    mov dx, [es:bx]               ; 16 bits spanning the 12-bit field
    test cx, 1                    ; cluster odd?
    jnz .odd
    and dx, 0x0FFF
    jmp .done
.odd:
    shr dx, 4
.done:
    mov ax, dx
    ret

; ---------------------------------------------------------------------------
; read_sectors: extended INT 13h read of cx sectors at LBA ax into es:bx.
; CF set on error. clobbers: si, ah, dl (bx preserved via stack).
; ---------------------------------------------------------------------------
read_sectors:
    push bx
    mov [dap_count], cx
    mov [dap_lba_lo], ax
    mov [dap_buf_off], bx
    mov [dap_buf_seg], es
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop bx
    ret

boot_error:
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
    push si
    push ax
    mov ah, 0x0e
    mov bh, 0x00
    int 0x10
    pop ax
    call serial_write
    pop si
    jmp print_string
.done:
    ret

serial_write:
    push ax
    mov dx, 0x3F8 + 5
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, 0x3F8
    out dx, al
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

switch_to_pmode:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:protected_entry

bits 32
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp KERNEL_LOAD_ADDR

bits 16

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------
boot_drive     db 0
first_cluster  dw 0
msg_disk       db 'disk/fat12 error', 13, 10, 0

; Disk Address Packet (INT 13h / 42h)
align 4
dap:
    db 0x10             ; packet size
    db 0                ; reserved
dap_count:
    dw 0                ; number of sectors
dap_buf_off:
    dw 0                ; buffer offset
dap_buf_seg:
    dw 0                ; buffer segment
dap_lba_lo:
    dd 0                ; starting LBA (low)
dap_lba_hi:
    dd 0                ; starting LBA (high)

; Minimal flat GDT: null, 32-bit code, 32-bit data.
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