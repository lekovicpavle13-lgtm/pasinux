; entry.asm — 32-bit protected mode entry point
;
; The boot sector loads this image at physical 0x10000 and jumps here.
; Order of operations:
;   1. Zero BSS (physical addressing — paging not yet active)
;   2. Set up page directory / table in BSS
;   3. Enable PSE + paging (identity + higher-half PDEs)
;   4. Jump to higher-half virtual address
;   5. Clear identity PDE, flush TLB
;   6. Set up stack and call kmain()

bits 32
section .text

HIGHER_HALF_OFFSET  equ 0xC0000000

extern _bss_start
extern _bss_end
extern _kmain

global _start
_start:

    ; ----------------------------------------------------------------
    ; Phase 1 — Zero BSS while still running at physical addresses.
    ; Both _bss_start and _bss_end are calculated at VMA (0xC001XXXX+).
    ; Subtract HIGHER_HALF_OFFSET to get physical addresses.
    ; ----------------------------------------------------------------
    mov edi, _bss_start
    sub edi, HIGHER_HALF_OFFSET        ; EDI = phys addr of BSS start
    mov ecx, _bss_end
    sub ecx, HIGHER_HALF_OFFSET        ; ECX = phys addr of BSS end
    sub ecx, edi                       ; ECX = BSS size in bytes
    xor eax, eax
    cld
    rep stosb

    ; ----------------------------------------------------------------
    ; Phase 2 — Build page tables in BSS (now zeroed)
    ; ----------------------------------------------------------------
    ; Compute physical addresses.
    mov eax, _page_directory
    sub eax, HIGHER_HALF_OFFSET        ; EAX = phys addr of page directory
    mov edi, eax                        ; EDI = PD phys (saved for CR3)

    mov ebx, _page_table
    sub ebx, HIGHER_HALF_OFFSET         ; EBX = phys addr of page table

    ; Populate page table: identity-map physical 0 -> 4 MB
    ; PT[i] = (i * 4096) | PRESENT | WRITABLE | USER
    xor edx, edx                        ; index i
.set_pt:
    mov esi, edx
    shl esi, 12                         ; ESI = i * 4096
    or  esi, 0x007                      ; PRESENT | WRITABLE | USER
    mov [ebx + edx * 4], esi
    inc edx
    cmp edx, 1024
    jl .set_pt

    ; PDE[0] = page table phys | PRESENT | WRITABLE (temp identity map)
    mov edx, ebx
    or  edx, 0x003
    mov [edi], edx

    ; PDE[768] = 4 MB page at phys 0 | PRESENT | WRITABLE | USER | PS (0x87)
    ; Maps 0xC0000000 -> physical 0x00000000 (kernel + ring-3 test code)
    mov dword [edi + 768 * 4], 0x00000087

    ; ----------------------------------------------------------------
    ; Phase 3 — Enable paging
    ; ----------------------------------------------------------------
    mov cr3, edi                        ; CR3 = PD physical address

    ; Enable PSE (CR4 bit 4) — needed for 4 MB pages
    mov eax, cr4
    or  eax, 0x00000010
    mov cr4, eax

    ; Enable paging (CR0 bit 31)
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    ; ----------------------------------------------------------------
    ; Phase 4 — Jump to higher half
    ; ----------------------------------------------------------------
    mov eax, _start_higher_half
    jmp eax

; ----------------------------------------------------------------
; Phase 5 — Now running in higher half (0xC000XXXX+)
; ----------------------------------------------------------------
_start_higher_half:
    ; Clear PDE[0] identity mapping — use virtual address of PD
    mov eax, _page_directory
    mov dword [eax], 0

    ; Invlpg the first page to flush the stale TLB entry
    xor eax, eax
    invlpg [eax]

    ; ----------------------------------------------------------------
    ; Phase 6 — Set up stack and enter C kernel
    ; ----------------------------------------------------------------
    mov esp, stack_top
    call _kmain

.hang:
    cli
    hlt
    jmp .hang


; ----------------------------------------------------------------
; .bss — page tables, stacks
; ----------------------------------------------------------------
section .bss
align 4096
global _page_directory
_page_directory:
    resb 4096                           ; 1024 x 4 = 4 KB

align 4096
global _page_table
_page_table:
    resb 4096                           ; 1024 x 4 = 4 KB

align 16
stack_bottom:
    resb 16384
stack_top: