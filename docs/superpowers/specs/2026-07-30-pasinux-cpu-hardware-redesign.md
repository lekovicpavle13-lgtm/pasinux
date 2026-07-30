# pasinux: CPU-Centric Redesign — Paging, User Mode & Higher-Half Kernel

**Date:** 2026-07-30
**Status:** Design (approved)
**Depends on:** All prior freestanding work (LBA, MM, driver+IPC unified)

---

## Overview

Redesign pasinux around the x86 CPU's protection model: enable paging (MMU), implement
ring-3 user mode with TSS-based kernel entry, a simple INT 0x80 syscall gate, and
relocate the kernel to a higher-half 0xC0000000+ address space. The hosted simulator is
phased out — all future development is freestanding kernel only, iterated in QEMU.

Three incremental stages, each independently testable:

| Stage | Deliverable | Verify |
|-------|-------------|--------|
| 1 — Paging | Identity paging for 4 MB, MMU enabled | `[PAGING] 4 MB identity-mapped` on serial, shell unchanged |
| 2 — User mode | One ring-3 process, INT 0x80 round-trip, TSS + GDT replaced | `[SYS] hello from ring 3` serial output |
| 3 — Higher-half | Kernel runs from 0xC0100000+, per-process page directories, identity mapping removed | Kernel shell shows `Kernel: 0xC0000000+ higher-half` |

---

## Architecture

```
┌──────────────────────────────────────┐
│  Ring 3 (User)                       │
│  ┌──────────┐   ┌──────────┐        │
│  │ proc A   │   │ proc B   │        │  ← user processes can't touch kernel memory
│  │ (init)   │   │ (worker) │        │
│  └────┬─────┘   └────┬─────┘        │
│       │ INT 0x80      │ INT 0x80    │  ← software interrupt gate (DPL=3)
├───────┴───────────────┴─────────────┤
│  Ring 0 (Kernel)                     │
│  ┌────────────────────────────────┐ │
│  │  IDT gate 0x80 — DPL=3         │ │  ← user-mode callable
│  │  syscall dispatch table         │ │
│  ├────────────────────────────────┤ │
│  │  TSS: ring 0 stack (esp0/ss0)  │ │  ← CPU auto-switches stack on int
│  ├────────────────────────────────┼─┤
│  │  Page tables (CR3 → PD → PT)   │ │  ← identity in S1+S2, higher-half in S3
│  └────────────────────────────────┘ │
└─────────────────────────────────────┘
```

---

## Stage 1 — Identity Paging

### Goal
Enable the MMU with a 1:1 physical-to-virtual mapping. The kernel still sees the same
addresses it did before paging, but now runs with memory protection hardware active — an
incorrect memory access produces a page fault instead of silent corruption.

### Data structures

```c
// paging.c — two statically-aligned page structures

__attribute__((aligned(4096)))
static uint32_t page_directory[1024];    // CR3 points here

__attribute__((aligned(4096)))
static uint32_t page_table[1024];       // covers first 4 MB (1024 × 4 KB)
```

### Initialization sequence (early boot, right after serial init)

1. Zero both PD and PT (inline memset).
2. Fill 1024 PT entries: `page_table[i] = (i * 4096) | 0x003` (present + writable).
3. Point PDE[0] at the page table: `page_directory[0] = (uint32_t)page_table | 0x003`.
4. Load CR3: `__asm__ volatile("mov %0, %%cr3" :: "r"(page_directory))`.
5. Enable paging: set CR0 bit 31.
6. Print `[PAGING] 4 MB identity-mapped\n`.

### Memory map post-paging

```
Physical range    →   Virtual range       Purpose
─────────────────────────────────────────────────────
0x00000000-0x0FFFF  →  (same)             BIOS data, real-mode IVT
0x00010000-0x04FFFF →  (same)             Kernel code + stack + heap
0x000B8000-var       →  (same)             VGA text buffer
All other            →  (same)             Everything unchanged
```

### Files

| File | Change |
|------|--------|
| `paging.c` | New — PD + PT setup, CR3 load, CR0 paging bit |
| `paging.h` | New — declares `paging_init()` |
| `kmain.c` | Call `paging_init()` after serial init, before MM init |
| `Makefile` | Add `paging.o` to `FREE_OBJS` |

Zero changes to `boot.asm`, `entry.asm`, or `linker.ld` — boot loader loads kernel at
physical 0x10000 exactly as today.

### Implementation notes

- `-ffreestanding`; no libc calls.
- 4 KB alignment via `__attribute__((aligned(4096)))`.
- Zero-touch for existing arbitrary functional code — serial, VGA, timer, scheduler,
  drivers, IPC all run unmodified. The kernel is physically the same as before, but
  write-protected pages are now possible for the next stages.

---

## Stage 2 — User Mode & Syscall Gate

### Goals

Create and run the first user-mode process at CPL=3. It has its own ring-3 stack, sees
kernel memory as "not present" (once per-process PDs are set up in Stage 3), and can only
invoke the kernel through a hardware INT 0x80 syscall gate.

### Ingredients

| Ingredient | Role |
|---|---|
| TSS | Stores the ring-0 stack pointer (esp0) + ss0. The CPU uses these automatically when a ring 3 → ring 0 interrupt occurs. |
| New GDT | 6 descriptors replacing the barebones 3-entry GDT from `boot.asm`: null, r0 code, r0 data, r3 code (DPL=3), r3 data (DPL=3), TSS descriptor at selector 0x28. |
| INT 0x80 gate | IDT entry 0x80 set to DPL=3 → isr_syscall_stub in asm → C handler. |
| C syscall handler | swich on `eax` (syscall number), read args from ebx/ect/edx/esi, write result to eax. |
| User test | Tiny far-callable ring-3 routine that calls `syscall_print` and loop-halts. |

### TSS structure

```c
struct tss_entry {
    uint16_t prev_tss, reserved0;
    uint32_t esp0;        // ★ ring-0 stack pointer (we write this)
    uint16_t ss0;         // ★ ring-0 stack segment  (we write this)
    uint16_t reserved1;
    uint32_t esp1;        // unused
    uint16_t ss1;         // ...
    uint16_t reserved_two;
    Uint32_t esp2;
    uint16_t ss2;
    uint16_t reserved;
    // hardware task-switch fields below — we leave them zeroed
    // since we iret directly rather than using task gates
    uint32_t cr3, eip, eflags;
    1 ünter (ignore)
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t es, cs, ss, ds, fs, gs;
    intmb fixme: struct
} __attribute__((packed));
```

### New GDT layout

```
Offset  Index  Selector  Description
────────────────────────────────────────
0x00    [0]    —         null descriptor (unchanged)
0x08    [1]    0x08      ring-0 code — base=0, limit=4 GB, DPL=0
0x10    [2]    0x10      ring-0 data — base=0, limit=4 GB, DPL=0
0x18    [3]    0x18|3    ring-3 code — base=0, limit=4 GB, DPL=3    ★ new
0x20    [4]    0x20|3    ring-3 data — base=0, limit=4 GB, DPL=3    ★ new
0x28    [5]    0x28      TSS descriptor → tss_entry                   ★ new
```

### syscall dispatch

Three initial syscall numbers:

| Num | Name | Behavior |
|-----|------|----------|
| 0 | SYS_PRINT | `serial_puts((char*)ebx)` |
| 1 | SYS_EXIT | kill and reap the calling user process |
| 2 | SYS_GETTIME | returns current `timer_ticks()` in eax |

### Syscall stub (isr.asm)

```asm
isr_syscall_stub:
    pusha                   ; save all general-purpose regs
    push ds
    push es
    mov  ax, 0x10           ; kernel data segment (selector 0x10)
    mov  ds, ax
    mov  es, ax
    call syscall_handler    ; C: uint32_t syscall_handler(uint32_t eax, …)
    pop  es
    pop  ds
    mov  [esp+28], eax      ; stash result → user eax after popa
    popa
    add  esp, 0             ; dummy error code was not pushed — mer-safe
    iret
```

`syscall_handler()` dispatches `eax` by number; ebx, ecx, edx, esi each carry further
arguments per the syscall table above.

### Boot order after Stage 2

1. `serial, VGA` — first lights
2. `paging_init()` — Stage 1 identity map
3. `init_memory()` — heap
4. `gdt_flush(&gdt_ptr)` — replaces boot.asm's minimal GDT
5. `tss_flush()` — writes esp0 + ss0, calls `ltr 0x28`
6. `idt_init()` — now installs gate 0x80 with DPL=3
7. `ssched_fs_init()` — processes still ring 0 for now; ring 3 test is a stand-alone launch

### Files

| File | Change |
|------|--------|
| `tss.c`, `tss.h` | New — TSS structure + write, `ltr` load helper |
| `gdt.c`, `gdt.h` | New — GDT builder + `lgdt` flush (replaces boot.asm GDT entirely) |
| `syscall.c`, `syscall.h` | New — main handler C function + dispatcher |
| `user_start.asm` | New — ring-3 test entry |
| `isr.asm` | New stub `isr_syscall_stub` |
| `idt.c` | Register entry 0x80 as DPL=3 (it dis not pre exist) |
| `kmain.c` | Wire `gdt_flush()`, `tss_flush()` early in boot; launch test ring-3 proc |
| `Makefile` | Add `tss.o gdt.o syscall.o user_start.o` to `FREE_OBJS` |

### Verification

Serial output:
```
[TSS] loaded TR
[GDT] loaded 6 entries (kernel cs/ds + user cs/ds + TSS)
[IDT] syscall gate 0x80 DPL=3 installed
[SYS] hello from ring 3. success
```

---

## Stage 3 — Higher-Half Kernel

### Goal

Move kernel to virtual address 0xC0010000 (still physically at 0x10000). Every kernel
pointer is now a high address / i. User processes live below 0xC0000000 and cannot
access kernel memory — paging on protects all kernel pages the moment the identity
mapping is removed from PDE 0.

### Boot transition (dual map → unmap)

```
      Page Directory contains TWO PDEs:
           PDE [ 0 ] =   identity map 0-4MB (temporary stub)
           PDE [Step] =   map 0-4MB to 0xC000000  (kernel base)

      CR0.PG = 1        →  MMU runs, identity stub + HIGH both visible.
      far JMP 0x08:higher_half_start  →  EIP hops onto 0xC01xxxxx
      once executing in high kernel:
          page_directory[0] = EMPTY      → identity mapping gone
           CR3 reload                    → means ONLY high addresses for kernel
```

### Per-process page directories

Every malloc of process now carries a page directory:

```c
void arch_process_init(process_t *proc) {
    proc->page_directory   = kmalloc_aligned(4096);
    // duplicate kernel PDEs (768→1023) from the master kernel PD
    clone_kernel_pde_stub(proc->page_directory);
    proc->cr3 = (uint32_t)proc->page_directory;
}
```

The scheduler switches CR3 to `proc->cr3` on every context switch — user pages belong
to that process; kernel pages (shared across all PDs) always stay visible.

### linker relocate

| Element | Before | After |
|---|---|---|
| Memory address | 0x10000 (physical) | 0xC0010000 (virtual) |
| Data segment | physical | virtual |
| Entry | `_start: 0x10000` | `kmain: 0xC0010000` (reached via far jump) |

### Far jump post-paging

```asm
jmjb 0x08:higher_half_kernel
```

### Files

| File | Change |
|------|--------|
| `linker.ld` | `VMA = 0xC0010000` |
| `paging.c` | Dual PDE setup + clone-kernel-PD helper |
| `paging.h` | `map_user_page()`, `walk_page_table()`, `clone_kernel_pd()` |
| `sched_fs.c` | Allocate per-process page directory on create; load CR3 on context switch |
| `entry.asm` | First jump into higher-half (beyond paging enabled) |
| `kmain.c` | Remove identity PDE after jump; call `unmap_identity()` |

---

## Combined build & verify

```sh
make image          # builds the new kernel including all three stages
make qemu           # boots with paging, user system mode, higher-half
```

Expected serial output:

```
[Serial] pasinux freestanding kernel booting
[PAGING] 4 MB identity-mapped
[MM] heap ready: 1048576 bytes
[TSS] loaded TR
[GDT] 6 entries loaded
[IDT] syscall gate 0x80 DPL=3 installed
[SYS] hello from ring 3. success
[PAGING] kernel higher-half 0xC00000+ established
[SCHED] scheduler ready
[DRIVER] …
[SCOM]: pasinux> ready
```

After Stage 3 completes:
- `info` prints `Kernel: 0xC0000000+ higher-half`
- `ps` lists processes (each now carries page_directory / cr3)
- Demo init/worker processes continue preempting and producing `init iters`/`worker iters` counts

---

## Combined files plan

| New | Modified | Deleted |
|-----|----------|---------|
| `paging.c`, `paging.h` | `kmain.c` | (None) |
| `tss.c`, `tss.h` | `isr.asm` | |
| `gdt.c`, `gdt.h` | `linker.ld` | |
| `syscall.c`, `syscall.h` | `idt.c` | |
| `user_start.asm` | `Makefile` | |
| | `sched_fs.c` (stage 3) | |
| | `entry.asm` (stage 3) | |

---

## Out of scope

- Real-hardware boot (USB/PXE) — follow-up after PCI scanning
- pci bus enumeration
- ATA disk driver
- E820 / BIOS memory detection beyond hard-coded 4 MB
- Stack protection / canaries
- Signal handling
- Virtual file system / file descriptors