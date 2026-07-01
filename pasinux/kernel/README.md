# pasinux kernel core

This directory currently builds as a hosted C kernel-core simulator. That makes the memory manager, scheduler, driver registry, and IPC queue easy to debug before the project grows into a real freestanding bootable kernel.

## Build

```sh
make
```

Or directly:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -g -o kernel_sim kernel.c mm.c scheduler.c driver.c ipc.c
```

## Run

```sh
make run
```

The smoke run initializes memory, the scheduler, drivers, IPC, creates two demo processes, queues chess-protocol messages, drains IPC, and prints scheduler and memory stats.

## Notes

- `boot.asm` is a valid placeholder boot sector, but it is not wired into the C simulator build yet.
- Hosted C library calls are intentionally kept in this simulator stage for visibility while debugging.
- The next real-kernel step is to add a linker script, freestanding output routines, interrupt setup, and a proper boot handoff.
