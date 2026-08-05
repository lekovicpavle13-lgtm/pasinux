# pasinux — hobby x86 OS kernel

**A from-scratch x86 kernel in C and assembly, currently developed as a hosted userspace simulator for fast iteration before targeting bare metal.**

[![MIT License](LICENSE)](LICENSE)
![Status](https://img.shields.io/badge/status-early--stage/active-yellow)

> **Status:** The C sources (memory manager, scheduler, driver framework, IPC) compile and run today as a normal userspace program on any POSIX-like host. A legacy BIOS boot sector (`boot.asm`) exists but is **not yet wired into a freestanding build** — that's the next major milestone. Everything below describes the working hosted simulator.

---

## Features

### Memory Management (`mm.c` / `mm.h`)
A classic freelist-based heap allocator over a static 1 MiB arena — no dependency on the host OS's `malloc`:

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
- Process lifecycle states: `READY → RUNNING → (SLEEPING ⇄ RUNNING) → ZOMBIE`
- A dedicated sleep/wakeup queue for blocked processes
- Configurable time-slice length for preemptive round-robin scheduling
- A second, selectable **strict-priority** policy for scenarios where priority ordering must always win
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

### Boot Sector (`boot/boot.asm`)
A **placeholder** legacy BIOS boot sector that loads the kernel from floppy, enables the A20 gate, sets up a flat GDT, and enters 32-bit protected mode. **Not yet wired into the build system** — this is the next major integration target.

### CI (`.github/workflows/`)
GitHub Actions scaffolding is in place (see [CI note](#ci) below).

---

## Project Structure

```
pasinux/
├── .github/workflows/        # CI scaffold (requires alignment)
├── LICENSE                   # MIT License
├── .gitignore
│
└── pasinux/kernel/
    ├── Makefile              # Hosted, GUI, sanitizer, and image/QEMU targets
    │
    ├── boot/                 # Boot sector + image packaging
    │   ├── boot.asm          #   Legacy BIOS boot sector
    │   ├── entry.asm         #   32-bit PM entry point
    │   ├── linker.ld         #   PE linker script
    │   └── mkimage.py        #   Flattens kernel into boot image
    │
    ├── kernel/               # Kernel entry points (hosted + freestanding)
    │   ├── kernel.c          #   Hosted simulator entry / demo process setup
    │   ├── kmain.c           #   Freestanding entry (bare-metal)
    │   ├── kernel.h
    │   └── types.h
    │
    ├── arch/                 # CPU-level infrastructure
    │   ├── gdt.c/h           #   Global Descriptor Table
    │   ├── idt.c/h           #   Interrupt Descriptor Table
    │   ├── interrupt.c/h     #   PIC remap + IRQ dispatch
    │   ├── paging.c/h        #   Page tables
    │   ├── tss.c/h           #   Task State Segment
    │   ├── syscall.c/h       #   Syscall gate (INT 0x80)
    │   ├── isr.asm           #   ISR stubs (48 vectors)
    │   └── io.h              #   Port I/O helpers
    │
    ├── mm/                   # Memory management
    │   ├── mm.c/h            #   Heap allocator (hosted)
    │   └── mm_fs.c/h         #   Heap allocator (freestanding)
    │
    ├── sched/                # Process scheduling
    │   ├── scheduler.c/h     #   Scheduler (hosted)
    │   └── sched_fs.c/h      #   Preemptive scheduler (freestanding)
    │
    ├── ipc/                  # Inter-process communication
    │   ├── ipc.c/h           #   Message queue + chess protocol
    │
    ├── drivers/              # Device drivers
    │   ├── driver.c/h        #   Driver registry (hosted)
    │   ├── driver_fs.c/h     #   Driver registry (freestanding)
    │   ├── serial.c/h        #   COM1 serial port
    │   ├── vga.c/h           #   VGA text mode (80×25)
    │   ├── keyboard.c/h      #   PS/2 keyboard
    │   ├── timer.c/h         #   PIT timer (100 Hz)
    │   ├── pci.c/h           #   PCI bus enumeration
    │   └── rtl8139.c/h       #   RTL8139 network card
    │
    ├── net/                  # Networking stack
    │   ├── net_eth.c/h       #   Ethernet framing
    │   ├── net_arp.c/h       #   ARP protocol
    │   ├── net_ip.c/h        #   IPv4
    │   ├── net_tcp.c/h       #   TCP
    │   ├── http.c/h          #   HTTP client
    │   └── json.c/h          #   JSON parser
    │
    ├── gui/                  # Win32 operator GUI
    │   └── gui_main.c/h
    │
    └── user/                 # User-mode process support
        └── user_start.asm
```

---

## Getting Started

### Prerequisites

- **gcc** with C11 support
- **make**
- `nasm`, `python3`, `ld` (MinGW), and **QEMU** are required only for the freestanding image target

### Build (hosted simulator)

```sh
cd pasinux/kernel
make          # builds kernel_sim
```

Or invoke gcc directly:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -g \
    -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui \
    -o kernel_sim \
    kernel/kernel.c mm/mm.c sched/scheduler.c drivers/driver.c ipc/ipc.c
```

### Run

```sh
make run      # builds and runs the smoke-test demo
```

`make run` initializes memory, the scheduler, drivers, and IPC, spawns three demo processes that exchange chess-protocol messages, drains the IPC queue, and prints final scheduler and memory statistics.

### Other targets

```sh
make syntax     # Syntax-check all hosted sources
make gui        # Build kernel_gui.exe (Win32 console)
make sanitize   # Build with ASan + UBSan instrumentation
make clean      # Remove all build artifacts
```

---

## Architecture

`kernel_run_demo()` in `kernel.c` brings subsystems up in a fixed order and exercises them with a small demo workload:

1. **Heap** is initialized over a static 1 MiB arena.
2. **Scheduler** is initialized with an empty ready queue.
3. **Drivers** are registered — currently a console driver.
4. **IPC** is initialized.
5. **Three demo processes** are created to exercise the subsystems:
   - `init` (high priority) — sends a chess starting position and a move over IPC.
   - `worker` (normal priority) — replies with a move.
   - `idle-demo` (low priority) — demonstrates the scheduler falling back to idle when there is no work.
6. `scheduler_run(8)` advances the scheduler for 8 ticks: ticking the sleep queue, preempting the running process once its time slice is spent, and selecting the next process (round-robin by default, or strict priority under the alternate policy).
7. IPC messages queued during the run are drained.
8. Final scheduler and memory statistics are printed before shutdown.

### Sample output

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

---

## CI

The `.github/workflows/` directory contains a **placeholder** GitHub Actions workflow that currently expects an Autotools-style pipeline (`./configure`, `make check`, `make distcheck`). The project uses a plain `Makefile`, so the workflow will not pass as committed. Aligning the CI configuration with the actual build system is an open item (see [Roadmap](#roadmap)).

---

## Roadmap

- [x] **Heap allocator** — first-fit, splitting, coalescing, live stats
- [x] **Process scheduler** — circular ready queue, priorities, sleep/wakeup, runtime stats
- [ ] **VGA text-mode driver** — 80×25 console output
- [ ] **Interrupt handlers** — IDT, PIC remap, PIT timer
- [ ] **Wire boot.asm into a freestanding build** — boot sector loads and boots the kernel in QEMU
- [ ] **Align CI with actual Makefile** — replace Autotools scaffold with `make` / `make run` / `make syntax`

---

## License

Distributed under the [MIT License](LICENSE).

---

## Author

[lekovicpavle13-lgtm](https://github.com/lekovicpavle13-lgtm)
