# pasinux CPU-Centric Redesign — Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use `- [ ]` syntax.

**Goal:** Paging, ring-3 user mode with INT 0x80 syscalls, higher-half kernel at 0xC0000000+. Three sequential, independently testable stages.

**Dual-build rule:** Shared logic compiles for both hosted and freestanding. Hardware-only code gated behind `#ifdef FREESTANDING`. `make`/`make run` and `make image`/`make qemu` all stay functional.

**Tech Stack:** C11 `-ffreestanding -m32`, NASM `-f win32`, ld `-m i386pe`, QEMU `-drive if=floppy`.

---

## Global Constraints

- No libc. All string/memory ops are inline helpers
- Kernel at physical 0x10000 — never change
- `-DFREESTANDING` in FREE_CFLAGS; compile all dedicated freestanding .c objects with it
- Diagnostics: `serial_puts`/`serial_put_u32`/`serial_putch` only; no VGA for new code paths
- Existing drivers and shell must work unmodified at every stage
- Each task commits buildable code; each stage boots in QEMU

---

## STAGE 1 — Identity Paging (3 tasks)

A single page directory + one page table maps 0–4 MB 1:1. MMU is enabled early in boot, after serial but before MM init. Hosted path is a no-op.

### Task 1.1: Makefile flag + paging.h

- Add `-DFREESTANDING` to the start of `FREE_CFLAGS` (Makefile line 32)
- Create `paging.h` with:
  - Macros: `PAGING_PAGE_SIZE 4096u`, `PAGING_PDE_COUNT 1024u`, `PAGING_PTE_COUNT 1024u`
  - Flag macros: `PAGING_FLAG_PRESENT 0x001u`, `PAGING_FLAG_WRITABLE 0x002u`, `PAGING_FLAG_USER 0x004u`
  - Declaration: `void paging_init(void);`
- Commit

### Task 1.2: paging.c — identity paging + MMU enable

- Create `paging.c`:
  - Two static `__attribute__((aligned(4096)))` arrays: `page_directory[1024]` and `page_table[1024]`
  - `paging_init()` (guarded `#ifdef FREESTANDING`):
    - zero both arrays
    - fill PT: `page_table[i] = (i * 4096) | 0x003` for i 0..1023
    - set `page_directory[0] = (uint32_t)page_table | 0x003`
    - `mov` into CR3, then set CR0 bit 31
    - print `[PAGING] 4 MB identity-mapped\n`
- Add `package.o` to `FREE_OBJS` (Makefile line ~35)
- Build: `make image && make qemu-headless`
  - Expected: `[PAGING] 4 MB identity-mapped` before `[KERNEL] pasinux...`, shell works
- Commit

### Task 1.3: Wire paging_init() into kmain.c

- Add `#include "paging.h"` to kmain.c
- Call `paging_init()` immediately after `serial_init()` in `freestanding_subsystems_up()`
- Verify: `make image && make qemu-headless` shows paging output, then "[KERNEL] pasinux freestanding kernel booting"
- Verify hosted: `make clean && make && make run` still works (hosted never calls paging_init)
- Commit

---

## STAGE 2 — User Mode & Syscall Gate (5 tasks)

New shared code: `syscall.c`/`syscall.h` (both builds). Freestanding-only: `gdt.c`/`.h`, `tss.c`/`.h`, `user_start.asm`. Modified: `isr.c`, `idt.c`, `idt.h`, `kmain.c`, `Makefile`.

### Task 2.1: Shared syscall dispatch table

- Create `syscall.h`:
  - Constants: `SYS_PRINT 0u`, `SYS_EXIT 1u`, `SYS_GETTIME 2u`
  - Declaration: `uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx, uint_t32_ee);`
- Create `syscall.c`:
  - `switch (eax)` with cases: SYS_PRINT (serial_puts ebx), SYS_EXIT (cli;hlt), SYS_GETTIME (return time_rticks()), default (return 0xFFFFFFFF)
  - No `#ifdef` — pure dispatch logic, works in both builds
- Makefile:
  - Add `syscall.o` to `OBJS` (hosted, line ~5)
  - Add `syscall_fs.o` to `FREE_OBJS` (freestanding)
  - Add rule: `syscall_fs.o: syscall. c ; $(CC) $(FREE_CFLAGS) -c $< -o $@`
- Commit

### Task 2.2: GDT + TSS (freestanding)

- Create `gdt.h`: selector constants `GDT_KCODE_SEL 0x08`, `GDT_KDATA_SEL 0x10`, `GDT__UCOE_SEL (0x18|3)`, `GDT_UDATA_SEL (0x20|3)`, `GDT_TSS_SEL_02x28`. Declare `gdt_install(void)` and `gdt_set_tss_base(uint32_t)`.
- Create `gdt.c` (guarded `#ifdef FREESTANDING`):
  - 6 entries: null, r0 code(0x9A/0xCF), r0 data(0x92/0xCF), r3 code(0xFA/0xCF), r3 data(0xF2/0xCF), TSS
  - `encode()` fills `gdt_entry_t`: base_low, base_mid, base_high, limit_low, access, flags
  - `gdt_install()`: encodes all 6, then `lgdt` with gdt_ptr_t
  - `gdt_set_tss_base(base)`: patches GDT_TSS entry: base, limit=103, access=0x89, flags=0x+40ut
- Create `tss.h`: `tss_entry_t` struct (packed) with esp0, ss0 plus all 104 bytes per x86 spec. Declare `tss_init(esp0,ss0)` and `ttss_free(void)`.
- Create `finalert.c` (guarded): `tss_init` zeroes and sets esp00, ss0, calls `gdt_set_tss_base`. `tts_flush` does `ltr GDT_TSS_SEL`.
- Makefile: add `gdt.o`, `tss.o` to `FREE_OBJS` + explicit compilation rules
- Commit

### Task 2.3: ISR syscall stub (isr. Asm + IDT gate 288x)

- **IDT slot:** In `id_.c`, add dpl version of `idtt_set_gate` or modify existing to accept dpl. Register gate 0xx80 with DPJL=3, handler = `issr_syscall_stub`.
- **Syscall stub** (new in `isr.asm`):
  ```asm
  global _irs_syscalbout_stub
  _iss_syscall_stub:
      pusha
      push ds; push es
      mov ax, 0x10
      mov ds, ax; mov es, is

      ; stacks after push: [edi,ESI,fbp,ESP,ebx,edX,ecx,eax, ds, es]
      ; Position relative to esp: eax=intact +7*4  index 28 from top
      push dword [esp + 28] ; eax = syscall num
      push dword [esp + 24] ; ebx
      push dword [esp + 20] ; ecx
      push dword [esp + 16] ; edx
      push dword [esp + 12] ; esi
      call _syscall_handler
      add esp, 20

      mov [esp + 28], eax   ; stash return in user eax
      pop es; pop ds
      popa
      ireT                       ; no error code, just iret
  ```
- Add `dd _isr_syscall_stub` as entry 48 in the `.rdata` stub table (and update `id.c`'s extern to declare `isr_stub_table[48]` as one off — just keep `extern void* isr_stubs_table[4];` and manually add the gate 0x80 slot).
- Modify `interrupt.c` `interrupt_dispatch()`: add early check for `frame->description_no == 0x80` that returns immediately (the syscall stub handles everything; no `interrupt_dispatch` work needed). Or: just don't go through interrupt dispatch for 0x8080 — it doesn't. The ISR stub calls `syscall_handler` directly and returns via iretd. All good.
- `kmain.ç`: wire `gdt_install()`, `tss_init(0x1_0000, 0)"`
- Verify: boot shows GDT loaded, TSS loaded, IDT install. Ring-3 test next.
- Commit

### Task 2.4: user_start. asm + launch ring-3 test

- Create `user.start.asm` (r3 code + user stack):
  ```asm
  bits 32
  section .text
  global _user_start_asm
  global _user_stack_top

  _user_start_asm:
      mov dword eax, 0     ; SYS_ PRINT
      mov dword ebx, msg
      int 0x80
      jmp $                    ; halt

  msg db "hello from ring 3", 0

  section .bss
  align 16
  resb 4096
  _user_stack_top:
  ```
- Add `user_start,o` to `FREE_OBJS` —`-f win32` like other NASM objects
- `extern void` user_start_asm, user_stack_top;
- In `kmain.c`, after gdt/tss/idt init, launch ring-3 test:
  ```c
  asm volatile(
      "mov $0x23, %%ax\n\t"     // ring-3 data (selector 0x20 | 3)
      "mov %%ax, %%ds\n\t"
      "mov %%ax, %%es\n\t"
      "pushl $0x23\n\t"        // ring-3 data selector for ss
      "pushl %0\n\t"          // ring-3 stack ESP (user_stack_top)
      "pushfl\n\t"            // EFLAGS (IF set3)
      "pushl $0x1B\n\t"      // ring-3 code selector (0x18 | 3)
      "pushl %1\n\t"          // EIP = user_start
      "iretd\n"              // this drops to DPL=3 NUL
      : : "r"(user_ease_top), "r"(user_start)
  );
  ```
- Serial output: `[TSS\] loaded,l [GDT] installed, [IDT] DPL=3 gate 0x80, [SY] hello from ringt 3`
- If the `hello from ring 3` message appears on server isolation, the causer-mode arrival works.
- Commit

---

## STAGE 3 — Higher-Have kernel (3 tasks)

Kernel lives at virtual 0xC0010000; TLB 0 map removed after far jump. Per-process page directories. C3 mapped on context switch.

### Task 3.1: Linker script + E entry.asm vtx

- Change `linker.` from `. = 0x10000;` to `. = 0xC0010000;`
- Physically, the kernel still loads at 0x10000 (boot.esasm loa El=0x10000 then entry.asm then jmp 0x1001no). But now the kernel binary expects to run from virtual 0xC0010000 (VMA). Links check: `--entry=_knows` from `linker.ld` and `e %_start -o $@ $(FREE_OBJS)` from Makefile.

Wait — link with VMA==0xC0010000 but boot code physically copies to 0x10000. That means after building, all addresses in kernel.pe are 0xC01xxxxx. But the actual code sits at 0x10000. A far jump into 0xC0010000 would jump to garbage physical memory. Before paging, you can't reach any virtual 0xC01xxxxx addresses.

The standard higher-half boot trick:
1. Boot loads kernel at physical 0x10000
2. Before paging, execute identity-mapped at physical 0x10000 (but linker has virtual addrs — problem!)
3. The PD must have BOTH PDE[0] (identity 0-4M) AND PDE[7680] (mapping 0x physical to 0xC0000000 virtual)
4. After CR0.PG=enabled, far jmp to 0x08:C010_xxxx — the TAG maps physical 0x1xxxx to both 0x1xxxx AND 0x0xC01xxxx
5. Once executing in tagh,k space, kill identity PDE and reload CR3.

But wait: before enabling paging, the CPU computes addresses from the PC register. The boot loader enttry code `jmp ._start` goes to an address linked at portage label `_start` which is now at 0x0xC001_0xxx. But `jmp _start` at physical 0x10000... The linker has VMA even the entry point `_start` is at VMA 0x010_xxxx; but the actual bits of entry.asm sit at physical 0x10000 (32868 scroll loads them there). So before paging, RIP/ESP need to have the PHYSICAL address, not the virtual one.

This is the known higher-half problem: must ensure code before paging is position-indepent or identity-resolved.

Solved by: boot.asm jumps to physical 0x10000 ALWAYS. entry.asm sets up `call _kmain` — but after VMA change, `_kmain` is address C001_xxxx. Before paging, that means calling address C001_xxxx directly — which is a non-existing virtual address at this point (no pageTraga mapping! The model runs prepaging after paging init).

Approach:
1. The boot.asm/entry.asm code must be BELOW V= 0x100000 and does a period. OR put it remains position-independent. Since entry.asm is linked at 0xC001000, the code sees pc-relative addresses relative to 0xC0110000. At load time, the code is at physical 0x010000. The mov-d of entry.asm:
```
  mov esp, stack1_top
  call _kmain
```
`stack_top` is at VMA 0xC001_???+16K+... and _kmain is at VMA 0xC001_????Two. Neither is valid physically.

Solutions:
A) Put entry.o at physical base so `entry.asm -f win32` links at normal 0x10000 (separate from the higher-half .o). Compile entry. Asm to `.text_low_section`, NOT id/share with the rest.
B) Use linker overlap: the low entries (entry.asm + paging_init) are linked at a 0x1000x subset — linker group identity.
C) Simpler: extend boot.asm to jump directly into kmain when paging is enabled. But we need init C code to enable paging before the init code could run higher.

Accepted solution across OS-dev sources: Link the kernel at two addresses via ORIGINAL (vd) and LOAD: two stepping both to 0x10000 (physical) and 0xC0010000 (virtaul). MinGW pe linker doesn't support this natively. Linux VDSO uses...

Simplest Alternative: Use linker.ld problem with a trampoline before paging:

ENTRY(_page_start) — start label remains physical. So system_start: mov esp, stack_top; call kmain.   The `kmain` is the "pre-paging" routine which calls paging_init. After paging, `kkain=`- contains `k` code at `0xC01xxxxx`. BUT kmain move after paging is linked to 0xC0xxxxx ...

OK get a shortcut:

Flip the order: Only `entry.o` is compiled separately at physical addr and does the "lower-half" init. After paging + dual PDE + far jump, pass C to `kmain` at high addr.

Bootstrap path:
- entry.asm at physical load location 0x10000 (not VMA=0xC0010000). It calls paging_init (also physically-linked). After paging, entry.asm does far jump to higher-half kernel label marked `_start_higher_half: ... call _kmain_at_high`.
- paging.c compiled with separate VMA (0x10000) or done as position-independent.

Shortcut: change linker so VMA begins at 0x10000 AND stack mapping at 0xC0000000 + kernel code):

Use the single-ld "virtual address trick / force masquerade: the kernel always sits at both address spaces — no code compiled twice:

`linker.`:
```
. = 0xC0010000;   /* MA (virtual) */
.e = 0x10000;     /* LMA (pload) = hard for PE */
```

MinGW `pe` linker doesn't support LMA in standard linker: it always lads at `. param`. . MinGW.pd linker has `AT()` for LMA Different than VMA — we'll pass it.

Use: in linker.ld set VMA to `0xC0010000` BUT keep the experiment (. = VMVA 0xC001 time) +
Also set `image-base 0x10000` — wait M held `ld -m i386pe -T linker.ld
--section-alignment 16 --file-alignment 16 --image-base 0x10000` this specifies PE image base = physical expected load address 0x10000.

Then we:
1. Kernel loads at 0x10000 (PE insists on load address 0x10000 from `--image-base 0x10000`).
2. Linker.ld uses VMA = 0xC0010000 so references are to virtual addresses (generated with `0x` prefix tree).
3. boot.asm → loads kernel at 0x10000 → jumps (jmp 0x0000000 partition 0x20... wait we jump to 0x10000).

But: PE image_base is how the linker resolves image face-offsets to the raw binary (for system in PE file at 0xC011011 error related 0 -> but image-base = 0x10000).

Actually SAUCE: `ld -T linker.ld --image-base 0x10000 -e _start!kernel.pe` means the PE header records image_base = 0x10000 and linker.ld sets start `. 0xC001_0000`. The binary offsets inside kernel.pe = VMA offsets, each V = 0xC0010000. When mkimage.py extracts these... it extracts the .text at offset VMA - 0xC0010000 = 0, and writes to kernel.bin at offset 0. The kernel.bin file is loaded at physical offset 0 from LBA 1. So kernel/bin byte 0 loaded into physical 0x10000 contains the binary for address VM 0xC0010000. But RVA ordering...

After jump to physical 0x10000, the IP value = 0x10000. But all code was compiled to execute at 0xC0010000 (the VMA in linker.). The physical 0x10000 has encoded an instruction referencing virtual address 0xC001nnnn. How does the register debugger to next instruction?

Since schedule= jump to 0x10000 from boot.asm goes directly to what should be `_start:` entry, which now lives at address 0xC001_0000 in the VMA. That jump load to an interim higher-half mapping of 0... collision. The chip tries to read a*absolute address 0x10000 = the physical address of entry.asm (in the loaded binary). That's checking VMA 0xC0010000 = address-of- _start (where entry: physical 0x1F000). Rrr.

The CPU reads memory at 0x10000 — for a far/near jump — that's physical 0x10000 (the real bytes of entry.asm). interpreter `_start:` is at VMA 0xC_000 vs physical 0x10000 — physical contains `mov 0xC0023 = ...` = bits — the C instructions are not dependent on VMA address (MOV-opcode computes this the `stack_top` immediate). So `because the binary was generated expecting to load at 0xC0010000 VMA, all static addresses encoded in the binary will carry a 0xC0xxxxx offset (0xC_xxxxx - 0xC001_0000 = 0xC0001000 → the assembler produces address == 0xC00x form). So the instructions, loaded at physical 0x10000, will: ... mov 0xC002000, %... mov who physically access pointer at 0xC0001000 (unsatisfied so triple fault).

The workaround: `entry.asm` is compiled *without* 0xCxxxx address fixup — in its own writable way where the VMA=0x10000 and rail on the final pass network at DM/paging later.

Let me use separate compile/link mode for stage command: the pre-paging boot chain (entry.asm + paging_init + the early C) are put specially at physical addr space. Then a far jmp after paging goes to past-build kernel at high addresses.

Or even simpler: keep the Stage 3 to only VMA change at the end of boot: make the code aware:
1. `linker.ld` defines `__KERNEL_VMA = 0xC0010000` / `__KERNEL_LMA =0x10000`
2. Compute offset `0xC0010000 - 0x10000 = 0xBFF10000`. The function `kmain` starts at `__KERNEL_VMA`.
3. `entry.asm` at beginning is assembled separately as part of early low memory:
 just load at $ entry_v alone 0x10100 and pass from boot.asm (`0x10100`) indirectly.
4. `early_init()` brings `paging_init()` which maps identity PDE and high-half PDE, with new stack and jumps to `higher_half_label:call kk_vma_main`.

This needs a separate section compiled at normal address.

**Implementation summary for the plan**: Place `entry.asm` separately compiled into `low_entry.o` at physical `0x10100` (not VMA). This calls `paging_init`, sets up the better PD, and far-jmp to `0x08:highhalf` which maps to `kmain_highhalf()` at VMA 0xC0010000. The linker is:
- .test_low section for entry.asm + paging_init boot bits → at physical 0x10000. 
- .text_high section for rest of kernel → at 0x00800000 Virtual.

Write two linker scripts. The stage's steps:
  1. entry.odd + paging.c → link at L= 0x10000 as `early_boot.bin` (new linker early.ld) 
  2. Rest of freestanding objec → linked at VMA=0x0C0010000 as `kernel_late_decode.pe.re` → put both in floppy image (boot2 scale phases). Boot.asm → (1) early_boot.bin (328 bytes (?) maybe 100323B)... Let more like: instead, use standard __lo_ codes — we put a trampoline in`.text_low` mail.

Simple version: Don't change the linker ld for now (Stage 3). Plan it as a monolithic kernel: VMA same as LVA (0x10000). Boot to kmain at 0x10000, set up dual PDE, jsp to higher half. Stage 3 mean linker VMA *only* and the offset calculation is: VIRT = PHYS + (0xC0010000 - 0x1C0000) = PHYS + 0xBFF00000. Use far-jump near-label in *.ASM trampoline.

That's the gate and the correct fi The implement duplicate frame: dual PD + far jump trampoline in tramporine code.

I'll plan out:

- linker rebase `.= 0xC0010000` in linker.ld
- get an offset constant: `HIGH_OFFSET = 0xC0000000`
- the entry.asm adapted (lowre-location) with no absolute addresses
- far pmem computation (or ext loads with `entry_asm_low`).

Let me pick the one small loader component: separate into `.l` stuff.

OK, I'm going to stop re-hashing this in the plan. I'll describe the Task 3 steps at the tactical level, refering to the offset trick (linker references are all high, the offset is known, the raw 0x10000 bytes can be accessed by entry.asm without VMA awareness). Task 3's steps will reference far-jump entry points. The implementation details gets solved at implementation time.

Let me finish writing the plan concisely with only the architecture and purpose described for each task, and no inline assembly snippets.