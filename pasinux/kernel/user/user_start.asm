bits 32
section .text


global _user_start
_user_start:
    mov eax, 0
    mov ebx, user_msg
    int 0x80

    mov eax, 1
    int 0x80

    
    hlt

extern _g_syscall_ret_esp
extern _g_syscall_ret_eip

global _launch_ring3
_launch_ring3:
    ; Ring-3 code must use ring-3 data selectors: iretd does not reload
    ; DS/ES/FS/GS, and entering a compiler-generated program with kernel
    ; data selectors would #GP on its first memory access.
    mov ax, 0x20 | 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, [esp]             
    mov [_g_syscall_ret_eip], eax
    lea eax, [esp + 4]        
    mov [_g_syscall_ret_esp], eax
    mov eax, [esp + 4]          
    mov ecx, [esp + 8]         

    push 0x20 | 3               
    push ecx                    
    pushfd                     
    pop ebx
    or ebx, 0x200              
    push ebx
    push 0x18 | 3              
    push eax                   

    iretd                       

section .data
user_msg: db '[SYS] hello from ring 3', 10, 0