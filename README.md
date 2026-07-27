# pasinux

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/blob/main/LICENSE)
[![Language: C11](https://img.shields.io/badge/language-C11-blue.svg)](https://github.com/lekovicpavle13-lgtm/pasinux)
[![Boot: x86_64 ASM](https://img.shields.io/badge/boot-x86__64%20ASM-informational.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/blob/main/pasinux/kernel/boot.asm)
[![Architecture: x86_64](https://img.shields.io/badge/arch-x86__64-9cf.svg)](#overview)
[![Status: early-stage / active](https://img.shields.io/badge/status-early--stage%20%2F%20active-orange.svg)](#roadmap)
[![Build: make](https://img.shields.io/badge/build-make-lightgrey.svg)](#getting-started)
[![Type: OS Kernel](https://img.shields.io/badge/type-OS%20kernel-critical.svg)](#overview)
[![Platform: hosted simulator](https://img.shields.io/badge/platform-hosted%20simulator-yellowgreen.svg)](#overview)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](#contributing)
[![Maintenance](https://img.shields.io/badge/maintained-yes-success.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/commits/main)

`os-kernel` · `x86-64` · `operating-system` · `c` · `assembly` · `bootloader` · `memory-allocator` · `process-scheduler` · `ipc` · `device-drivers` · `systems-programming` · `hobby-os`

A hobby **x86_64 OS kernel** written in C and assembly. It's currently developed as a **hosted C simulator**, so its core subsystems — memory management, process scheduling, drivers, and IPC — can be designed, built, and debugged in userspace before the project grows into a real, freestanding, bootable kernel.

> **Status:** early-stage / active development. The C sources build and run today as a userspace simulator (`kernel_sim`). The boot sector exists as a valid placeholder but is not yet wired into the build.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Project Structure](#project-structure)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Build](#build)
  - [Run](#run)
  - [Other Targets](#other-targets)
- [Architecture](#architecture)
  - [Boot Sequence](#boot-sequence)
  - [Sample Output](#sample-output)
- [Subsystems in Detail](#subsystems-in-detail)
  - [Memory Management](#memory-management)
  - [Process Scheduler](#process-scheduler)
  - [Driver Framework](#driver-framework)
  - [IPC](#ipc)
- [Continuous Integration](#continuous-integration)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)
- [Author](#author)

---

## Overview

**pasinux** is a from-scratch operating system kernel project targeting the **x86_64** architecture. Rather than jumping straight into bare-metal, freestanding development, the project takes a pragmatic "simulator-first" approach: every core kernel subsystem is written in portable, hosted C so it can be compiled and exercised as a normal userspace program (`kernel_sim`) with a standard compiler and no emulator required.

This lets the design of the allocator, scheduler, driver registry, and IPC layer be iterated on and tested quickly, before the added complexity of a freestanding, no-libc, bootable environment is introduced. A legacy BIOS boot sector already exists in the tree as a placeholder for that next phase.

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

- **Driver framework** — a minimal driver registry supporting `char`, `block`, `net`, and `input` device types through a common `driver_ops_t` interface, with a working console driver wired in at boot.

- **IPC** — a priority message queue between processes, exercised end-to-end by a small **chess protocol** (moves, resignations, draw offers, board state) used as a realistic workload for the queue.

- **Boot sector** — a valid legacy BIOS boot sector (`boot.asm`), reserved for the future freestanding build.

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
        ├── boot.asm              # Legacy BIOS boot sector (placeholder)
        ├── kernel.c               # Kernel entry point / demo process setup
        ├── mm.c / mm.h            # Heap allocator
        ├── scheduler.c / scheduler.h  # Process scheduler
        ├── driver.c / driver.h    # Driver registry + IPC message types
        ├── ipc.c / ipc.h          # IPC dispatch + chess protocol handlers
        ├── Makefile
        └── README.md              # Kernel-core build notes
```

## Getting Started

### Prerequisites

- `gcc` with C11 support
- `make`

### Build

```bash
cd pasinux/kernel
make
```

Or invoke `gcc` directly:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -g -o kernel_sim kernel.c mm.c scheduler.c driver.c ipc.c
```

### Run

```bash
make run
```

This smoke-runs the simulator: it initializes memory, the scheduler, drivers, and IPC, spawns an `init` and a `worker` process, exchanges chess-protocol messages over IPC, drains the queue, and prints final scheduler and memory statistics.

### Other Targets

```bash
make syntax   # Syntax-check all sources without building
make clean    # Remove build artifacts
```

## Architecture

`kernel_main()` in `kernel.c` brings the subsystems up in a fixed order and then exercises them with a small demo workload.

### Boot Sequence

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

## Continuous Integration

`.github/workflows/c-cpp.yml` currently runs an Autotools-style pipeline (`./configure`, `make`, `make check`, `make distcheck`). The kernel currently ships a plain `Makefile` with no `configure` script and no `check` / `distcheck` targets, so **the workflow will not pass CI as committed**. To fix this, either:

- add Autotools scaffolding (`configure.ac`, `Makefile.am`, etc.), **or**
- simplify the workflow to call `make` / `make run` to match the current build system.

This is tracked as an open item in the [Roadmap](#roadmap).

## Roadmap

- [x] Heap allocator with splitting and coalescing (`kmalloc` / `kcalloc` / `krealloc` / `kfree`)
- [x] Scheduler with sleep/wakeup, time-slice preemption, and a selectable priority policy
- [ ] VGA text-mode driver
- [ ] Interrupt handlers
- [ ] Wire `boot.asm` into a freestanding build and set up QEMU-based testing
- [ ] Bring the CI workflow in line with the actual `Makefile` (see [Continuous Integration](#continuous-integration))

## Topics

For discoverability on GitHub, consider adding these as repository **Topics** (Settings → General → Topics):

```
os-kernel  x86-64  operating-system  c  assembly  bootloader
memory-allocator  process-scheduler  ipc  device-drivers
systems-programming  hobby-os  kernel-development  bare-metal
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
