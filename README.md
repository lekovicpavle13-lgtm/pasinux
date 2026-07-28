#                                                                               pasinux

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/blob/main/LICENSE)
[![Language: C11](https://img.shields.io/badge/language-C11-blue.svg)](https://github.com/lekovicpavle13-lgtm/pasinux)
[![Boot: BIOS + Protected Mode](https://img.shields.io/badge/boot-BIOS%20%2B%20protected%20mode-informational.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/blob/main/pasinux/kernel/boot.asm)
[![Architecture: x86 (32-bit)](https://img.shields.io/badge/arch-x86%20(32--bit)-9cf.svg)](#overview)
[![Status: early-stage / active](https://img.shields.io/badge/status-early--stage%20%2F%20active-orange.svg)](#roadmap)
[![Build: make](https://img.shields.io/badge/build-make-lightgrey.svg)](#getting-started)
[![Type: OS Kernel](https://img.shields.io/badge/type-OS%20kernel-critical.svg)](#overview)
[![Runs in: QEMU](https://img.shields.io/badge/runs%20in-QEMU-ff69b4.svg)](#freestanding-boot-image-qemu)
[![Platform: hosted + freestanding](https://img.shields.io/badge/platform-hosted%20%2B%20freestanding-yellowgreen.svg)](#overview)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](#contributing)
[![Maintenance](https://img.shields.io/badge/maintained-yes-success.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/commits/main)

`os-kernel` · `x86` · `operating-system` · `c` · `assembly` · `bootloader` · `qemu` · `protected-mode` · `memory-allocator` · `process-scheduler` · `ipc` · `interrupts` · `device-drivers` · `systems-programming` · `hobby-os` · `bare-metal`

A hobby **x86 OS kernel** written in C and assembly, buildable two ways: as a **hosted C simulator** (`kernel_sim`) for fast userspace iteration on the allocator, scheduler, drivers, and IPC, and as a **real freestanding boot image** (`pasinux.img`) that boots from a legacy BIOS sector into 32-bit protected mode and runs under **QEMU**.

> **Status:** early-stage / active development. Both build paths work today: `kernel_sim` runs as a normal userspace program, and `make image && make qemu` boots the freestanding kernel end to end — IDT, remapped PIC, a 100 Hz PIT, keyboard IRQ1, and preemptive scheduling — with live output on VGA text mode and the serial console.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Hosted Simulator](#hosted-simulator)
  - [GUI / Sanitizer Builds](#gui--sanitizer-builds)
  - [Freestanding Boot Image (QEMU)](#freestanding-boot-image-qemu)
- [Architecture](#architecture)
  - [Hosted Simulator Flow](#hosted-simulator-flow)
  - [Freestanding Boot Flow](#freestanding-boot-flow)
  - [Sample Output](#sample-output)
- [Subsystems in Detail](#subsystems-in-detail)
  - [Memory Management](#memory-management)
  - [Process Scheduler](#process-scheduler)
  - [Driver Framework](#driver-framework)
  - [IPC](#ipc)
  - [Freestanding Kernel Pieces](#freestanding-kernel-pieces)
- [Continuous Integration](#continuous-integration)
- [Roadmap](#roadmap)
- [Topics](#topics)
- [Contributing](#contributing)
- [License](#license)
- [Author](#author)

---

## Overview

**pasinux** is a from-scratch operating system kernel project targeting **x86**. It's built in two parallel stages that share the same design:

1. **Hosted simulator** (`kernel_sim`) — the memory manager, scheduler, driver registry, and IPC layer written in portable, hosted C11 and run as a normal userspace program. No emulator, no bootloader, fast iteration.
2. **Freestanding boot image** (`pasinux.img`) — the real thing: a 16-bit legacy BIOS boot sector (`boot.asm`) that enables the A20 line, loads a flat GDT, switches to 32-bit protected mode, and jumps into a `-ffreestanding`, no-libc kernel (`kmain.c`) that sets up its own IDT, remaps the PIC, drives a 100 Hz PIT, handles keyboard IRQ1, and preemptively schedules the same three demo processes as the hosted build. It boots and runs under **QEMU**.

The hosted simulator exists so the allocator, scheduler, driver, and IPC logic can be designed and debugged quickly in userspace; the freestanding image mirrors that same logic down to real interrupt handlers and a real boot sector, so the two builds validate each other.

## Features

- **Memory management** — a real heap allocator over a static 1 MiB arena, with:
  - First-fit free-block search
  - 16-byte aligned blocks
  - Block splitting on allocation
  - Coalescing of adjacent free blocks on `free`
  - `kmalloc`, `kcalloc`, `krealloc`, `kfree`
  - Live allocator stats: current/peak usage, allocation/free/failure counts

- **Process scheduler** — a circular ready queue supporting:
  - Priority levels: `LOW`, `NORMAL`, `HIGH`
  - Process states: `READY`, `RUNNING`, `SLEEPING`, `ZOMBIE`
  - A sleep/wakeup queue
  - Configurable time-slice preemption
  - Selectable **round-robin** or **strict-priority** scheduling policy
  - `process_exit()` and graceful teardown
  - Runtime stats: context switches, idle vs. work time, created/terminated process counts

- **Driver framework** — a minimal driver registry supporting `char`, `block`, `net`, and `input` device types through a common `driver_ops_t` interface, with a working console driver wired in at boot (hosted) and dedicated serial, VGA, and keyboard drivers wired in at boot (freestanding).

- **IPC** — a priority message queue between processes, exercised end-to-end by a small **chess protocol** (moves, resignations, draw offers, board state) used as a realistic workload for the queue. The freestanding kernel runs the identical protocol over its own `ipc_fs` implementation.

- **Freestanding boot image** — a legacy BIOS boot sector (`boot.asm`) that enables A20, loads a flat GDT, enters 32-bit protected mode, and loads a real kernel (`kmain.c`) with its own IDT (`idt.c`), PIC remap and PIT timer (`interrupt.c`, `timer.c`), keyboard IRQ1 (`keyboard.c`), serial console (`serial.c`), VGA text-mode output (`vga.c`), and preemptive scheduler (`sched_fs.c`) — bootable and testable end to end in **QEMU**.

- **Win32 operator GUI** — an optional `kernel_gui` build (`gui_main.c`) that runs the same kernel core behind a native Win32 console for interactive operation, in addition to the plain CLI `kernel_sim`.

- **Sanitizer build** — a `kernel_sim_san` target compiled with `-fsanitize=address,undefined` for memory- and UB-safety testing of the hosted core.

- **CI scaffolding** — a GitHub Actions workflow for automated builds on push/PR (see [Continuous Integration](#continuous-integration) for its current status).

## Project Structure

```
pasinux/
├── .github/workflows/c-cpp.yml   # CI workflow
├── .gitignore
├── LICENSE                       # MIT License
├── operator-handoff.md           # Project status & roadmap notes
└── pasinux/
    ├── operator-handoff.md       # Kernel-core status notes
    └── kernel/
        ├── Makefile               # Hosted, GUI, sanitizer, and image/QEMU targets
        ├── README.md              # Kernel-core build notes
        │
        ├── kernel.c                    # Hosted kernel entry point / demo process setup
        ├── mm.c / mm.h                 # Heap allocator (hosted)
        ├── scheduler.c / scheduler.h   # Process scheduler (hosted)
        ├── driver.c / driver.h         # Driver registry + IPC message types (hosted)
        ├── ipc.c / ipc.h               # IPC dispatch + chess protocol handlers (hosted)
        ├── gui_main.c                  # Win32 operator console (kernel_gui build)
        │
        ├── boot.asm                    # Legacy BIOS boot sector (0x7C00 → protected mode)
        ├── entry.asm                   # Freestanding entry point, calls kmain()
        ├── isr.asm                     # Interrupt service routine stubs
        ├── kmain.c                     # Freestanding kernel entry / demo process setup
        ├── idt.c / idt.h               # Interrupt Descriptor Table setup
        ├── interrupt.c                 # PIC remap + IRQ dispatch
        ├── timer.c                     # 100 Hz PIT driver
        ├── serial.c                    # Serial (COM1) console driver
        ├── vga.c                       # VGA text-mode (80x25) driver
        ├── keyboard.c                  # PS/2 keyboard driver (IRQ1)
        ├── sched_fs.c                  # Freestanding preemptive scheduler
        ├── ipc_fs.c                    # Freestanding IPC + chess protocol
        ├── linker.ld                   # Flat freestanding image layout (loads at 0x10000)
        └── mkimage.py                  # Flattens boot.bin + kernel.pe into pasinux.img
```

## Getting Started

### Prerequisites

| For | Requires |
|---|---|
| Hosted simulator (`kernel_sim`) | `gcc` with C11 support, `make` |
| GUI build (`kernel_gui`) | MinGW `gcc` on Windows (links `gdi32`, `user32`, `comctl32`, `comdlg32`) |
| Sanitizer build (`kernel_sim_san`) | `gcc`/`clang` with `-fsanitize=address,undefined` support |
| Freestanding image + QEMU | **NASM**, **Python 3**, MinGW `gcc`/`ld` (`-m32`), **QEMU** (`qemu-system-i386`) |

### Hosted Simulator

```bash
cd pasinux/kernel
make          # builds kernel_sim
make run      # runs the smoke-test demo
```

Or invoke `gcc` directly:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -g -o kernel_sim kernel.c mm.c scheduler.c driver.c ipc.c
```

`make run` initializes memory, the scheduler, drivers, and IPC, spawns an `init` and a `worker` process, exchanges chess-protocol messages over IPC, drains the queue, and prints final scheduler and memory statistics.

```bash
make syntax   # Syntax-check all hosted sources without building
make clean    # Remove build artifacts from every target
```

### GUI / Sanitizer Builds

```bash
make gui        # builds kernel_gui.exe — Win32 operator console
make run-gui    # builds and launches it
make sanitize   # builds kernel_sim_san with ASan + UBSan instrumentation
```

### Freestanding Boot Image (QEMU)

```bash
make image      # assembles boot.asm, compiles kmain.c and friends with -m32 -ffreestanding,
                 # links a flat PE with linker.ld, and flattens it into pasinux.img via mkimage.py
make qemu       # boots pasinux.img as a floppy image in QEMU
```

Two headless variants are also available for scripted or serial-only testing:

```bash
make qemu-headless   # no display, serial console piped to stdio, exits without a reboot prompt
make qemu-serial     # no display, serial console piped to stdio, monitor disabled
```

On boot you'll see `pasinux freestanding kernel` and rising `init iters` / `worker iters` counters on the VGA text-mode screen, confirming the scheduler is actually preempting between processes on real (emulated) hardware — not just simulating it.

## Architecture

### Hosted Simulator Flow

`kernel_main()` in `kernel.c` brings the subsystems up in a fixed order and then exercises them with a small demo workload:

1. **Heap** is initialized over a static 1 MiB arena.
2. **Scheduler** is initialized with an empty ready queue.
3. **Drivers** are registered — currently a console driver.
4. **IPC** is initialized.
5. Three demo processes are created to exercise the subsystems:
   - **`init`** (high priority) — sends a starting chess position and a move over IPC.
   - **`worker`** (normal priority) — replies with a move.
   - **`idle-demo`** (low priority) — demonstrates the scheduler falling back to idle when there is no work.
6. `scheduler_run()` advances the scheduler for a fixed number of ticks: ticking the sleep queue, preempting the running process once its time slice is spent, and selecting the next process (round-robin by default, or strict priority under the alternate policy).
7. IPC messages queued during the run are drained.
8. Final scheduler and memory statistics are printed before shutdown.

### Freestanding Boot Flow

The freestanding image mirrors the same demo, but on real emulated hardware, disk, and interrupts:

1. **`boot.asm`** — the legacy BIOS boot sector loaded by the BIOS at `0x7C00` — reads `KERNEL_SECTORS` (32) sectors from disk into `0x10000`.
2. It enables the **A20 line**, loads a flat **GDT**, and switches the CPU into **32-bit protected mode**.
3. Control jumps to **`entry.asm`**'s `_start`, which calls **`kmain()`** in `kmain.c`.
4. `kmain()` brings up the freestanding subsystems in order: serial console → VGA text mode → scheduler (`sched_fs`) → keyboard → IPC (`ipc_fs`) → **IDT** setup, remapping the **PIC** and starting a **100 Hz PIT** on IRQ0.
5. The same three demo processes (`init`, `worker`, `idle-demo`) are created with the same priorities as the hosted build, and the same chess-protocol IPC exchange runs over `ipc_fs`.
6. On every timer tick, the IRQ0 handler saves and restores each process's own stack (`ESP`), so `init` and `worker` genuinely preempt each other on real hardware — visible as their iteration counters climbing independently on the VGA screen.
7. Disk layout: **LBA 0** is the boot sector; **LBA 1–16** (8 KiB) holds the freestanding kernel image, flattened from the linked PE by `mkimage.py`.

`linker.ld` + `mkimage.py` exist because the freestanding kernel is compiled and linked with MinGW's PE toolchain (`-m32 -ffreestanding`, no libc) and then flattened into a raw, position-correct binary that the boot sector can load directly at `0x10000` — no ELF or PE loader needed at boot time.

### Sample Output

```
[KERNEL] pasinux kernel core starting
[MM] heap ready: 1048576 bytes
[SCHED] scheduler ready
[DRIVER] registered console
[DRIVER] driver core ready
[IPC] ipc ready
[SCHED] created init pid=1 priority=10
[SCHED] created worker pid=2 priority=5
[SCHED] created idle-demo pid=3 priority=1
...
[IPC] chess move from pid=1 to pid=1: e2e4 promotion=0 score=-49
[SCHED] ticks=8 switches=8 created=3 terminated=0 idle=0 work=8
[MM] allocations=6 frees=0 current=12672 peak=12672 failed=0
[KERNEL] shutdown complete
```

## Subsystems in Detail

### Memory Management (`mm.c` / `mm.h`)

A classic freelist-based heap allocator, implemented over a fixed static arena (no dependency on the host OS's `malloc`):

| Aspect | Behavior |
|---|---|
| Arena size | Static 1 MiB |
| Search strategy | First-fit |
| Alignment | 16-byte aligned blocks |
| Allocation | Splits blocks when a free block is larger than requested |
| Deallocation | Coalesces adjacent free blocks |
| API | `kmalloc`, `kcalloc`, `krealloc`, `kfree` |
| Observability | Current/peak usage, allocation/free/failure counters |

### Process Scheduler (`scheduler.c` / `scheduler.h`)

A cooperative/preemptive hybrid scheduler built around a circular ready queue:

- Three priority tiers: `LOW`, `NORMAL`, `HIGH`
- Process lifecycle states: `READY` → `RUNNING` → (`SLEEPING` ⇄ `RUNNING`) → `ZOMBIE`
- A dedicated sleep/wakeup queue for blocked processes
- Configurable time-slice length for preemptive round-robin scheduling
- A second, selectable strict-priority policy for scenarios where priority ordering must always win
- `process_exit()` for controlled process termination
- Runtime statistics: total context switches, idle vs. active time, and created/terminated process counts

### Driver Framework (`driver.c` / `driver.h`)

A lightweight driver registry decoupling subsystem code from concrete device implementations:

- Supports `char`, `block`, `net`, and `input` device classes
- Drivers implement a common `driver_ops_t` interface
- A console driver is registered and wired in automatically at boot, providing kernel logging output

### IPC (`ipc.c` / `ipc.h`)

A priority-ordered inter-process message queue:

- Processes exchange typed messages through the queue
- Includes a purpose-built **chess protocol** as a realistic test workload — moves, resignations, draw offers, and board-state messages — used to validate correctness and priority ordering across processes end to end

### Freestanding Kernel Pieces

The bare-metal counterparts to the hosted subsystems above, compiled `-ffreestanding` with no libc:

| File | Role |
|---|---|
| `boot.asm` | 16-bit BIOS boot sector: A20 enable, GDT load, protected-mode switch, kernel load from disk |
| `entry.asm` | 32-bit entry point (`_start`), hands off to `kmain()` |
| `isr.asm` | Interrupt service routine stubs feeding into the C interrupt dispatcher |
| `idt.c` / `idt.h` | Builds and loads the Interrupt Descriptor Table |
| `interrupt.c` | Remaps the 8259 PIC and dispatches IRQs |
| `timer.c` | Programs the PIT for 100 Hz tick interrupts (IRQ0) |
| `serial.c` | COM1 serial driver used for boot/debug logging |
| `vga.c` | 80×25 VGA text-mode output driver |
| `keyboard.c` | PS/2 keyboard driver on IRQ1 |
| `sched_fs.c` | Freestanding preemptive scheduler, stack-saving on every tick |
| `ipc_fs.c` | Freestanding IPC queue + chess protocol handlers |
| `linker.ld` | Places the freestanding image at physical `0x10000` |
| `mkimage.py` | Flattens the linked PE into the raw `pasinux.img` boot image |

## Continuous Integration

`.github/workflows/c-cpp.yml` currently runs an Autotools-style pipeline (`./configure`, `make`, `make check`, `make distcheck`). The kernel currently ships a plain `Makefile` with no `configure` script and no `check` / `distcheck` targets, so **the workflow will not pass CI as committed**. To fix this, either:

- add Autotools scaffolding (`configure.ac`, `Makefile.am`, etc.), **or**
- simplify the workflow to call `make` / `make run` to match the current build system.

This is tracked as an open item in the [Roadmap](#roadmap).

## Roadmap

- [x] Heap allocator with splitting and coalescing (`kmalloc` / `kcalloc` / `krealloc` / `kfree`)
- [x] Scheduler with sleep/wakeup, time-slice preemption, and a selectable priority policy
- [x] VGA text-mode driver
- [x] Interrupt handlers (IDT, PIC remap, PIT timer)
- [x] Wire `boot.asm` into a freestanding build and set up QEMU-based testing
- [x] Keyboard IRQ1 support
- [ ] Freestanding memory manager (the freestanding kernel currently has no heap of its own)
- [ ] Grow the kernel image past its current 16-sector (8 KiB) payload limit
- [ ] Bring the CI workflow in line with the actual `Makefile` (see [Continuous Integration](#continuous-integration))

## Topics

For discoverability on GitHub, consider adding these as repository **Topics** (Settings → General → Topics):

```
os-kernel  x86  operating-system  c  assembly  bootloader  qemu
protected-mode  memory-allocator  process-scheduler  ipc  interrupts
device-drivers  systems-programming  hobby-os  kernel-development  bare-metal
```

## Contributing

This is an early-stage hobby project, and contributions, issues, and design discussion are welcome:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Build and smoke-test locally with `make && make run`
4. Run `make syntax` to catch compile issues quickly
5. Open a pull request describing the change and its motivation

Bug reports and design proposals are best opened as [GitHub Issues](https://github.com/lekovicpavle13-lgtm/pasinux/issues).

## License

Distributed under the **MIT License**. See [`LICENSE`](https://github.com/lekovicpavle13-lgtm/pasinux/blob/main/LICENSE) for the full text.

## Author

**[lekovicpavle13-lgtm](https://github.com/lekovicpavle13-lgtm)**

---

<p align="center"><i>pasinux — a kernel built one subsystem at a time.</i></p>
