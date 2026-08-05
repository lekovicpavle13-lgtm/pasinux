# pasinux Codebase Reorganization — Subsystem Directories

**Date:** 2026-08-04
**Status:** Design (approved)
**Goal:** Move from 55+ flat files in `pasinux/kernel/` into subsystem directories, keeping all includes and build paths working.

---

## Target Layout

```
pasinux/kernel/
├── Makefile          ← stays at root, updated with -I flags
│
├── boot/             ← boot chain (no CFLAGS needed, nasm + linker + python)
│   ├── boot.asm
│   ├── entry.asm
│   ├── linker.ld
│   ├── mkimage.py
│   └── _check_pe.py
│
├── arch/             ← x86-protected-mode arch support
│   ├── gdt.c / gdt.h
│   ├── tss.c / tss.h
│   ├── idt.c / idt.h
│   ├── interrupt.c / interrupt.h
│   ├── isr.asm
│   ├── paging.c / paging.h
│   ├── syscall.c / syscall.h
│   └── io.h
│
├── mm/               ← memory management
│   ├── mm.c / mm.h
│   └── mm_fs.c / mm_fs.h
│
├── sched/            ← process scheduler
│   ├── scheduler.c / scheduler.h
│   └── sched_fs.c / sched_fs.h
│
├── drivers/          ← device drivers
│   ├── serial.c / serial.h
│   ├── vga.c / vga.h
│   ├── keyboard.c / keyboard.h
│   ├── timer.c / timer.h
│   ├── pci.c / pci.h
│   ├── rtl8139.c / rtl8139.h
│   ├── driver.c / driver.h
│   └── driver_fs.c / driver_fs.h
│
├── net/              ← networking stack
│   ├── net_eth.c / net_eth.h
│   ├── net_arp.c / net_arp.h
│   ├── net_ip.c / net_ip.h
│   ├── net_tcp.c / net_tcp.h
│   ├── http.c / http.h
│   └── json.c / json.h
│
├── ipc/              ← inter-process communication
│   ├── ipc.c
│   └── ipc.h
│
├── kernel/           ← core kernel entry & types
│   ├── kmain.c
│   ├── kernel.c / kernel.h
│   └── types.h
│
├── user/             ← user-mode test code
│   └── user_start.asm
│
└── gui/              ← Win32 GUI (hosted)
    ├── gui_main.c
    └── gui_main.h
```

## Approach

### Includes
Add `-I` flags for every subdirectory to `FREE_CFLAGS` and hosted `CFLAGS`:
```
FREE_CFLAGS := ... -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui
```
All existing `#include "mm.h"` / `#include "serial.h"` continue to resolve without changes.

### NASM includes
Add `-I` paths to NASM rules for `entry.asm`, `isr.asm`, `user_start.asm` — though none currently use includes, keeping consistency.

### Makefile changes
- `FREE_OBJS` entries gain subdirectory prefixes: `arch/isr.o drivers/serial.o ...`
- Pattern rules updated to match `arch/%.o`, `drivers/%.o`, etc.
- Build output directory mirrors source tree; no separate object tree needed at this stage.

### Files not moved
- `Makefile` — stays at `kernel/` root
- `driver.h` — shared type definitions for the driver framework, stays in `drivers/`

## Build Verification
- `make` (hosted sim) — still builds and runs
- `make image` (freestanding) — still builds
- `make qemu-headless` — boots, same serial output as before
- `make gui` — still builds

## Out of scope
- No code changes — pure file moves + include-path updates
- No refactoring of logic
- No renaming of files or symbols
- Documentation location stays under `docs/` at repo root