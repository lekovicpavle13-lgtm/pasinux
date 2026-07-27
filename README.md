# pasinux

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/blob//LICENSE)[![Language: C11](https://img.shields.io/badge/language-C11-blue.svg)](https://github.com/lekovicpavle13-lgtm/pasinux)[![Boot: x86_64 ASM](https://img.shields.io/badge/boot-x86__64%20ASM-informational.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/blob/main/pasinux/kernel/boot.asm)[![Architecture: x86_64](https://img.shields.io/badge/arch-x86__64-9cf.svg)](#overview)[![Status: stage / active](https://img.shields.io/badge/status-early--stage%20%2F%20active-orange.svg)](#roadmap)[![Build: make](https://img.shields.io/badge/build-make-lightgrey.svg)](#getting-started)[![Type: OS Kernel](https://img.shields.io/badge/type-OS%20kernel-critical.svg)](#overview)[![Platform: hosted simulator](https://img.shields.io/badge/platform-hosted%20simulator-yellowgreen.svg)](#overview)[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](#contributing)[![Maintenance](https://img.shields.io/badge/maintained-yes-success.svg)](https://github.com/lekovicpavle13-lgtm/pasinux/commits/main)

`os-kernel` · `x86-64` · `operating-system` · `c` · `assembly` · `bootloader` · `memory-allocator` · `process-scheduler` · `ipc` · `device-drivers` · `systems-programming` · `Os`

A hobby **x86_64 OS kernel** written in C and assembly. Its currently developed as a *hosted C simulator**, so its core subsystems. Memory management, process scheduling, drivers and IPC. Can be designed, built and debugged in userspace before the project grows into a real, freestanding bootable kernel.

> **Status:** stage / active development. The C sources. Run today as a userspace simulator (`kernel_sim`). The boot sector exists as a placeholder but is not yet wired into the build.

---

## Table of Contents

- [Overview](#overview)

- [Features](#features)

- [Project Structure](#project-structure)

- [Getting Started](#getting-started)

[Prerequisites](#prerequisites)

[Build](#build)

[Run](#run)

[Other Targets](#other-targets)

- [Architecture](#architecture)

[Boot Sequence](#boot-sequence)

[Sample Output](#sample-output)

- [Subsystems in Detail](#subsystems-in-detail)

[Memory Management](#memory-management)

[Process Scheduler](#process-scheduler)

[Driver Framework](#driver-framework)

[IPC](#ipc)

- [Continuous Integration](#continuous-integration)

- [Roadmap](#roadmap)

- [Contributing](#contributing)

- [License](#license)

- [Author](#author)

---

## Overview

pasinux is a from-scratch operating system kernel project targeting the x86_64 architecture. Than jumping straight into bare-metal freestanding development the project takes a pragmatic "simulator-first" approach: every core kernel subsystem is written in portable hosted C so it can be compiled and exercised as a normal userspace program (`kernel_sim`) with a standard compiler and no emulator required.

This lets the design of the allocator, scheduler, driver registry and IPC layer be iterated on and tested quickly before the added complexity of a freestanding no-libc, environment is introduced. A legacy BIOS boot sector already exists in the tree as a placeholder for that phase.

## Features

- **Memory management**. A heap allocator over a static 1 MiB arena with:

First-fit free-block search

16-Byte aligned blocks

Block splitting on allocation

Coalescing of adjacent free blocks on `free`

`Kmalloc` `kcalloc` `krealloc` `kfree`

Live allocator stats: current/peak usage, allocation/free/failure counts

- **Process scheduler**. A circular ready queue supporting:

Priority levels: `LOW` `NORMAL` `HIGH`

Process states: `READY` `RUNNING` `SLEEPING` `ZOMBIE`

A sleep/wakeup queue

Configurable time-slice preemption

Selectable **round-robin** or **strict-priority** scheduling policy

`Process_exit()` and graceful teardown

Runtime stats: context switches, idle vs. Work time created/terminated process counts

- **Driver framework**. A minimal driver registry supporting `char` `block` `net` and `input` device types through a common `driver_ops_t` interface with a working console driver wired in at boot.

- **IPC**. A priority message queue between processes exercised end-to-end by a small **chess protocol** (moves, resignations, draw offers, board state) used as a workload for the queue.

- **Boot sector**. A valid legacy BIOS boot sector (`boot.asm`) reserved for the freestanding build.

- **CI scaffolding**. A GitHub Actions workflow for automated builds on push/PR (see [ Integration](#continuous-integration) for its current status).

## Project Structure

```

pasinux/

├──.github/workflows/c-cpp.yml # CI workflow

├──.gitignore

├── LICENSE # MIT License

├── operator-handoff.md # Project status & roadmap notes

└── pasinux/

├── # Kernel-core status notes

└── kernel/

├── boot.asm # Legacy BIOS boot sector (placeholder)

├── kernel.c # Kernel entry point / demo process setup

├── mm.c / mm.h # Heap allocator

├── scheduler.c / scheduler.h # Process scheduler

├── driver.c / driver.h # Driver registry + IPC message types

├── ipc.c / ipc.h # IPC dispatch + chess protocol handlers

├── Makefile

└── README.md # Kernel-core build notes

```

## Getting Started
```
git clone https://github.com/lekovicpavle13-lgtm/pasinux
```
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

gcc -std=c11 -Wall -Wextra -Wpedantic -g -o kernel_sim kernel.c scheduler.c driver.c ipc.c

```

### Run

```bash

make run

```

This smoke-runs the simulator: it initializes memory, the scheduler, drivers and IPC spawns an `init` and a `worker` process, exchanges chess-protocol messages over IPC drains the queue and prints final scheduler and memory statistics.

### Other Targets

```bash

make syntax # Syntax-check all sources without building

make clean # Remove build artifacts

```

## Architecture

`kernel_main()` in `kernel.c` brings the subsystems up in a fixed order and then exercises them with a small demo workload.

### Boot Sequence

1. **Heap** is initialized over a 1 MiB arena.

2. **Scheduler** is initialized with a ready queue.

3. **Drivers** are registered. A console driver.

4. **IPC** is initialized.

5. Three demo processes are created to exercise the subsystems:

**`Init`** ( priority). Sends a starting chess position and a move over IPC.

**`Worker`** ( priority). Replies with a move.

**`Idle-demo`** ( priority). Demonstrates the scheduler falling back to idle when there is no work.

6. `Scheduler_run()` advances the scheduler for a fixed number of ticks: ticking the sleep queue preempting the running process once its time slice is spent and selecting the process (round-robin by default or strict priority under the alternate policy).

7. IPC messages queued during the run are drained.

8. Final scheduler and memory statistics are printed before shutdown.

### Sample Output

```

[KERNEL] pasinux kernel core starting

[MM] heap 1048576 bytes

[SCHED] scheduler ready

[DRIVER] registered console

[DRIVER] driver core

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

### Memory Management (`` / `mm.h`)

A classic freelist-based heap allocator, implemented over a fixed arena (no dependency, on the host OSs `malloc`):

| Aspect | Behavior |

|---|---|

| Arena size | Static 1 MiB |

| Search strategy | First-fit |

| Alignment | 16-byte aligned blocks |

| Allocation | Splits blocks when a free block is larger than requested |

| Deallocation Coalesces adjacent free blocks |

| API | `kmalloc` `kcalloc` `krealloc` `kfree` |

| Observability | Current/peak usage, allocation/free/failure counters |

### Process Scheduler (`scheduler.c` / `scheduler.h`)

A scheduler that works together and can take control, based on a circle of processes waiting to run:

- Three levels of importance: NORMAL HIGH

- States a process goes through: READY becomes RUNNING then can go to SLEEPING and back to RUNNING and finally becomes ZOMBIE

- A special place for processes that are waiting for something to happen

- You can set how long a process runs before it is stopped to let another run

- Another option for when the most important process must always come first

- A way to end a process properly called process_exit()

- Information about how the system is running: how many times processes changed how much time was spent doing nothing or working and how many processes were made and ended

### Driver Framework (driver.c / driver.h)

A simple system that lets parts of the system work without being tied to specific devices:

- Works with types like char, block, net and input

- Drivers use a common driver_ops_t way to work

- A console driver is set up automatically when the system starts so it can show messages from the kernel

### IPC (ipc.c / ipc.h)

A way for processes to send messages to each other in order of importance:

- Processes send messages with types through a queue

- There is a special chess game setup to test if messages are handled correctly and in the right order between processes

## Continuous Integration

The file.github/workflows/c-cpp.yml runs a pipeline that uses Autotools (./configure make, make check make distcheck). The kernel has a Makefile without a configure script and no check or distcheck parts so the workflow will not work as it is. To fix this either:

- add the Autotools setup (configure.ac, Makefile.am, etc.) or

- make the workflow simpler by calling make and make run to match the current setup.

This is something that needs to be fixed and is listed in the Roadmap.

## Roadmap

- [x] Memory manager that can split and join memory blocks ( / kcalloc / krealloc / kfree)

- [x] Scheduler that handles waiting, stopping after a time and choosing priority

- [ ] Driver for text mode on VGA

- [ ] Ways to handle interruptions

- [ ] Connect boot.asm to a setup that works on its own and set up testing with QEMU

- [ ] Make the CI workflow match the real Makefile (see Integration)

## Topics

To help people find this on GitHub you might want to add these as repository Topics (Settings → General → Topics):

```

os-kernel x86-64 operating-system c assembly bootloader

memory-allocator process-scheduler ipc device-drivers

systems-programming hobby-os kernel-development bare-metal

```

## Contributing

This is a project that is still new and anyone can help with ideas, problems and talks about how to make it better:

1. Copy the project

2. Make a branch for your idea (git checkout -b feature/my-feature)

3. Test it locally with make and make run

4. Use make syntax to find problems quickly

5. Share your idea by making a request that explains what you did and why

Reporting problems and suggesting changes is best done as GitHub Issues.

## License

It is shared under the MIT License. See LICENSE, for the text.

## Author

**[lekovicpavle13-lgtm](https://github.com/lekovicpavle13-lgtm)**

---

<p align="center"><i>pasinux. A kernel built one subsystem at a time.</i></p>system at a time.</i></p>
