# pasinux kernel core

This directory builds two ways:

1. **Hosted simulator** (`kernel_sim`) — memory manager, scheduler, drivers, and IPC on the host OS for day-to-day debugging.
2. **Freestanding boot image** (`pasinux.img`) — BIOS boot sector loads a 32-bit protected-mode kernel that prints to VGA text mode under QEMU.

## Hosted simulator

### Build

```sh
make
```

Or directly:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -g -o kernel_sim kernel.c mm.c scheduler.c driver.c ipc.c
```

### Run

```sh
make run
```

The smoke run initializes memory, the scheduler, drivers, IPC, creates demo processes, queues chess-protocol messages, drains IPC, and prints scheduler and memory stats.

### GUI / sanitizer

```sh
make gui        # Win32 operator console
make run-gui
make sanitize   # ASan + UBSan CLI build
```

## Freestanding boot image

Requires **NASM**, **Python 3**, MinGW `gcc`/`ld` with `-m32`, and **QEMU** for `make qemu`.

```sh
make image      # boot.bin + kernel.bin -> pasinux.img
make qemu       # boot the floppy image in QEMU
```

Boot flow:

1. `boot.asm` (legacy BIOS sector at `0x7C00`) loads `KERNEL_SECTORS` from disk to `0x10000`.
2. Enables A20, loads a flat GDT, enters 32-bit protected mode.
3. Jumps to `entry.asm` (`_start`), which calls `kmain()` in `kmain.c`.
4. `kmain` sets up the IDT, remaps the PIC, starts a 100 Hz PIT on IRQ0, and runs as the idle process.
5. On each time slice the IRQ path saves/restores ESP so `init` and `worker` actually preempt on their own stacks (see rising `init iters` / `worker iters` on VGA).

Freestanding interrupt/sched pieces: `isr.asm`, `idt.c`, `interrupt.c`, `timer.c`, `sched_fs.c`.

Disk layout: LBA 0 = boot sector, LBA 1..16 = freestanding kernel (8 KiB reserved).

## Notes

- Hosted C library calls stay in the simulator stage only; the freestanding path is `-ffreestanding` and does not link libc.
- `linker.ld` + `mkimage.py` flatten the MinGW PE output into a raw binary loadable at `0x10000`.
- Next real-kernel steps: keyboard IRQ1, freestanding MM, grow past the 16-sector payload.
