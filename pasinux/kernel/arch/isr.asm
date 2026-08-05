bits 32
	section .text

	extern _interrupt_dispatch
	extern _sched_fs_maybe_switch

	%macro ISR_STUB 1
	global _isr%1
	_isr%1:
	    push dword 0
	    push dword %1
	    jmp isr_common
	%endmacro

	%macro ISR_ERR 1
	global _isr%1
	_isr%1:
	    push dword %1
	    jmp isr_common
	%endmacro

	ISR_STUB 0
	ISR_STUB 1
	ISR_STUB 2
	ISR_STUB 3
	ISR_STUB 4
	ISR_STUB 5
	ISR_STUB 6
	ISR_STUB 7
	ISR_ERR  8
	ISR_STUB 9
	ISR_ERR  10
	ISR_ERR  11
	ISR_ERR  12
	ISR_ERR  13
	ISR_ERR  14
	ISR_STUB 15
	ISR_STUB 16
	ISR_ERR  17
	ISR_STUB 18
	ISR_STUB 19
	ISR_STUB 20
	ISR_STUB 21
	ISR_STUB 22
	ISR_STUB 23
	ISR_STUB 24
	ISR_STUB 25
	ISR_STUB 26
	ISR_STUB 27
	ISR_STUB 28
	ISR_STUB 29
	ISR_STUB 30
	ISR_STUB 31
	ISR_STUB 32
	ISR_STUB 33
	ISR_STUB 34
	ISR_STUB 35
	ISR_STUB 36
	ISR_STUB 37
	ISR_STUB 38
	ISR_STUB 39
	ISR_STUB 40
	ISR_STUB 41
	ISR_STUB 42
	ISR_STUB 43
	ISR_STUB 44
	ISR_STUB 45
	ISR_STUB 46
	ISR_STUB 47

	isr_common:
	    pusha
	    push ds
	    push es
	    push fs
	    push gs

	    mov ax, 0x10
	    mov ds, ax
	    mov es, ax
	    mov fs, ax
	    mov gs, ax

	    push esp
	    call _interrupt_dispatch
	    add esp, 4

	    push esp
	    call _sched_fs_maybe_switch
	    add esp, 4
	    test eax, eax
	    jz .no_switch
	    mov esp, eax
	.no_switch:

	    pop gs
	    pop fs
	    pop es
	    pop ds
	    popa
	    add esp, 8
	    iret


	; ----------------------------------------------------------------
	; INT 0x80 syscall gate stub — called from ring-3 via int 0x80
	; ----------------------------------------------------------------
	global _isr_syscall_stub
	extern _syscall_handler
	_isr_syscall_stub:
	    pusha
	    push ds
	    push es

	    mov ax, 0x10             ; kernel data selector
	    mov ds, ax
	    mov es, ax

	    ; Reload user register values from the pusha frame.
	    ; [esp+0]=es, +4=ds, +8=edi, +12=esi, +16=ebp, +20=old_esp,
	    ; +24=ebx, +28=edx, +32=ecx, +36=eax
	    mov eax, [esp + 36]      ; user's eax = syscall number
	    mov ecx, [esp + 32]      ; user's ecx
	    mov edx, [esp + 28]      ; user's edx
	    mov ebx, [esp + 24]      ; user's ebx
	    mov esi, [esp + 12]      ; user's esi
	    push esi
	    push edx
	    push ecx
	    push ebx
	    push eax
	    call _syscall_handler
	    add esp, 20              ; pop 5 args

	    ; ---- Check: did SYS_EXIT set the return-to-kernel flag? ----
	    extern _g_syscall_did_exit
	    cmp dword [_g_syscall_did_exit], 0
	    jz .do_normal_ret

	    ; SYS_EXIT was called: restore kernel stack, push return
	    ; address, and ret to C instead of iretd to ring-3.
	    ; Re-enable interrupts before returning (syscall entry may
	    ; have cleared IF if the gate was an interrupt gate).
	    mov dword [_g_syscall_did_exit], 0
	    extern _g_syscall_ret_esp
	    extern _g_syscall_ret_eip
	    mov esp, [_g_syscall_ret_esp]
	    sti
	    push dword [_g_syscall_ret_eip]
	    ret

	.do_normal_ret:
	    ; Syscall returned normally — stash result into saved eax.
	    mov [esp + 36], eax

	    pop es
	    pop ds
	    popa
	    iretd


	section .rdata
	global _isr_stub_table
	_isr_stub_table:
	%assign i 0
	%rep 48
	    dd _isr%+i
	%assign i i+1
	%endrep