# pasinux CPU-Centric Redesign — Paging, User Mode & Higher-Half Kernel

**Date:** 2026-07-31
**Status:** Design
**Based on:** Obsidian vault `future goals/plans.md` (approved 2026-07-30)

---

## Overview

Redesign pasinux around the x86 CPU's protection model in three incremental stages:

| Stage | Deliverable | Hosted | Freestanding |
|-------|-------------|--------|-------------|
| 1 — Paging | Identity paging for 4 MB, MMU enabled | Stub (no-op) | Real CR0/CR3 |
| 2 — User Mode | Ring-3 process, INT 0x80 syscall, TSS + GDT | Syscall dispatch only | Full hardware |
| 3 — Higher-half | Kernel at 0xC0100000+, per-process PDs, identity mapping removed | N/A | Full hardware |

**Dual-build preserved:** `make` / `make run` (hosted) and `make image` / `make qemu` (freestanding) both continue to work. The principle: shared logic (`syscall.c`) compiles for both; hardware-only code (`paging_init()` body, `gdt.c`, `tss.c`, `user_start.asm`) is gated behind `#ifdef FREESTANDING`. The Makefile already compiles `FREE_OBJS` with `-m32 -ffreestanding`; add `-DFREESTANDING` to that compilation line so the preprocessor sees it.

---

## Architecture

```
┌──────────────────────────────────────┐
│  Ring 3 (User) — Freestanding Only   │
│  ┌──────────┐   ┌──────────┐        │
│  │ proc A   │   │ proc B   │        │
│  │ (init)   │   │ (worker) │        │
│  └────┬─────┘   └────┬─────┘        │
│       │ INT 0x80      │ INT 0x80    │
├───────┴───────────────┴─────────────┤
│  Ring 0 (Kernel)                     │
│  ┌────────────────────────────────┐ │
│  │  syscall_handler() — shared    │ │  ← both paths use this
│  │  IDT gate 0x80 DPL=3 — FS only │ │
│  │  TSS/GDT — FS only            │ │
│  ├────────────────────────────────┤ │
│  │  Page tables (freestanding)    │ │
│  │  Heap arena (both)            │ │
│  └────────────────────────────────┘ │
└─────────────────────────────────────┘
```

### Stage 3 Memory Map

```
Physical range         →  Virtual range          Purpose
───────────────────────────────────────────────────────────
0x00000000-0x0000FFFF  →  (same, identity stub)    BIOS data, IVT
0x00010000-0x0001FFFF  →  0xC0010000-0xC001FFFF    Kernel code+.data+.bss
0x00070000-0x0016FFFF  →  0xC0070000-0xC016FFFF    Heap arena (1 MiB)
0x000B8000-0x000B8FFF  →  (same, identity stub)    VGA text buffer
All other              →  unmapped / free
```

---

## Stage 1 — Identity Paging

### Hosted behavior
`paging_init()` compiles to `__attribute__((unused))` nothing. The PD/PT structs are visible but unused. All existing hosted code runs unchanged.

### Freestanding behavior

New file `paging.c`:
```c
__attribute__((aligned(4096)))
static uint32_t page_directory[1024];

__attribute__((aligned(4096)))
static uint32_t page_table[1024];  // covers first 4 MB

void paging_init(void) {
    // zero, fill PT[0..1023] = (i * 4096) | 0x003
    // PDE[0] = (uint32_t)page_table | 0x003
    // mov %0, %%cr3
    // set CR0 bit 31
    // serial "[PAGING] 4 MB identity-mapped\n"
}
```

### Boot order change
Insert `paging_init()` right after `serial_init()` in `freestanding_subsystems_up()` — before MM init, before VGA.

### Files changed

| File | Change |
|------|--------|
| `paging.c` | New — identity map init |
| `paging.h` | New — `paging_init()` decl |
| `kmain.c` | Call `paging_init()` after serial init |
| `Makefile` | Add `paging.o` to `FREE_OBJS` |
| Hosted kernel.c | No change needed |

---

## Stage 2 — User Mode & Syscall Gate

### Shared: syscall dispatch

New files `syscall.c`/`syscall.h` — pure C, no hardware dependency:

| Num | Name | Behavior |
|-----|------|----------|
| 0 | `SYS_PRINT` | `serial_puts((char*)ebx)` |
| 1 | `SYS_EXIT` | Kill + reap calling process |
| 2 | `SYS_GETTIME` | Returns `timer_ticks()` in eax |

```c
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx,
                         uint32_t edx, uint32_t esi) {
    switch (eax) {
        case 0: serial_puts((char*)ebx); return 0;
        case 1: /* kill current */; return 0;
        case 2: return timer_ticks();
        default: return -1;
    }
}
```

**Hosted path:** syscall dispatch is exercised via direct function calls in the simulator demo. The same `syscall.c` code is tested — just without the interrupt gate entry.

### Freestanding-only additions

**`gdt.c`/`gdt.h`** — replaces boot.asm's 3-entry GDT with 6 entries:
```
[0] null        — 0x00
[1] r0 code     — 0x08 (DPL=0, base=0, limit=4GB)
[2] r0 data     — 0x10 (DPL=0)
[3] r3 code     — 0x18|3 (DPL=3)
[4] r3 data     — 0x20|3 (DPL=3)
[5] TSS         — 0x28
```

**`tss.c`/`tss.h`** — TSS entry struct with `esp0`/`ss0`, loaded via `ltr 0x28`.

**`isr.asm`** — new stub `isr_syscall_stub`:
```asm
pusha; push ds; push es
mov ax, 0x10  ; kernel data segment
call syscall_handler
mov [esp+28], eax  ; stash return in user eax
pop es; pop ds; popa; iret
```

**`idt.c`** — register gate 0x80 with DPL=3.

**`user_start.asm`** — tiny ring-3 test routine that calls `SYS_PRINT` then loop-halts.

### Boot order change (freestanding)
```
serial → paging → MM → gdt_flush → tss_flush → idt_init (now incl. gate 0x80) → sched_fs_init → drivers_init_fs
```

### Verification output
```
[TSS] loaded TR
[GDT] loaded 6 entries (kernel cs/ds + user cs/ds + TSS)
[IDT] syscall gate 0x80 DPL=3 installed
[SYS] hello from ring 3. success
```

---

## Stage 3 — Higher-Half Kernel

Freestanding only. The hosted sim runs the same code with no change.

### Boot transition strategy

```
boot.asm loads kernel at physical 0x10000 (unchanged)
entry.asm → kmain()
  ├─ paging_init()  with TWO PDEs:
  │   PDE[0]   = identity 0-4MB (temporary)
  │   PDE[768] = map 0-4MB → 0xC0000000 (kernel base)
  ├─ CR0.PG = 1
  ├─ far JMP 0x08:higher_half_label
  │   → EIP now in 0xC01xxxxx
  ├─ identity_drop():
  │   PDE[0] = 0; reload CR3
  └─ rest of init continues at high addresses
```

### linker.ld change
`VMA = 0xC0010000` instead of `0x10000`. Boot loader still loads at `0x10000` (LMA unchanged — it's physical).

### Per-process page directories

`sched_fs_create_process()` calls `paging_alloc_page_directory()` which allocates a 4KB-aligned PD via a new `kmalloc_aligned(size, 4096)` helper, clones kernel PDEs (768–1023) from the master PD, and stores the physical address in the process struct. The context-switch path in `sched_fs_maybe_switch()` loads the new process's CR3 before `iret`.

`kmalloc_aligned()` is added to `mm_fs.c` — it uses the existing first-fit allocator but rounds up to the requested alignment, returning a page-aligned pointer suitable for CR3.

### Files changed

| File | Change |
|---|---|
| `linker.ld` | `VMA = 0xC0010000` |
| `paging.c` | `@amplifies:` dual PDE setup + `unmap_identity()` + `clone_kernel_pde()` |
| `paging.h` | ` paging_alloc_page_directory()`, `unmap_identity()` |
| `mm_fs.c` | New `kmalloc_aligned(size, align)` — first-fit with alignment |
| `sched_fs.c` | Per-process PD alloc, CR3 swap on context switch |
| `entry.asm` | Far jump into higher-half after paging enabled |
| `kmain.c` | Call `unmap_identity()` after far jump |

### Verification
```
[PAGING] kernel higher-half 0xC0000000+ established
```
Shell `info` prints `Kernel: 0xC0000000+ higher-half`. Shell `ps` lists processes with CR3. Demo init/worker continue preempting.

---

## Combined Build & Verify

```sh
make              # hosted simulator still builds
make run          # hosted sim runs with syscall dispatch
make image        # freestanding image: paging + user mode + higher-half
make qemu         # boots with full bring-up
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
[PAGING] kernel higher-half 0xC0000000+ established
[SCHED] scheduler ready
[DRIVER] ...
[SHELL] pasinux> ready.
```

---

## Files Summary

| New | Modified (fre. only) |
|-----|----------------------|
| `paging.c`, `paging.h` | `kmain.c` — wire into boot order |
| `syscall.c`, `syscall.h` (shared) | `Makefile` — `FREE_OBJS` additions |
| `gdt.c`, `gdt.h` (freestanding) | `isr.asm` — `isr_syscall_stub` |
| `tss.c`, `tss.h` (freestanding) | `idt.c` — gate 0x80 DPL=3 |
| `user_start.asm` (freestanding) | `sched_fs.c` — Stage 3 CR3 swap |
| | `linker.ld` — Stage 3 VMA=0xC0010000 |
| | `entry.asm` — Stage 3 far jump |

Zero files deleted. Both build paths remain functional.

## Out of scope

- Real hardware boot (USB/PXE)
- PCI enumeration
- ATA disk driver
- E820 memory detection
- Stack protection / canaries
- Signal handling
- Virtual filesystem / file descriptors