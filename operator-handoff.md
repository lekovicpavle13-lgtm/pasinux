# Operator Handoff — pasinux

## Overview
Hobby x86 kernel in C + assembly. Two build paths live side by side:

- **Hosted simulator** (`pasinux/kernel`): `make` / `make run` — MM, scheduler, drivers, IPC on the host.
- **Freestanding boot image**: `make image` / `make qemu` — BIOS → PM → IDT/PIT → real preemptive context switch.

## Verified
- ✅ QEMU boot: BIOS loads kernel from floppy, enters protected mode, runs `kmain()`.
- ✅ Preemptive multitasking: `sched switches` climb; `init iters` / `worker iters` both increase (real interrupt-frame context switch).
- ✅ Full interrupt chain: IDT (256 gates, 48 stubs), PIC remapped (master 32, slave 40), PIT IRQ0 at 100 Hz, keyboard IRQ1.
- ✅ Interactive serial shell (`help`, `ps`, `uptime`, `dump`, `info`, `clear`) with keyboard input via IRQ1.
- ✅ IPC chess-protocol messages exercise the same demo flow as the hosted simulator.
- ✅ Kernel payload ~3.6 KiB / 8 KiB reserved.
- ✅ Hosted `kernel_sim` still builds with `make`.
- ✅ GUI `kernel_gui` still builds with `make gui`.

## Freestanding pieces
| File / Module | Role |
|---------------|------|
| `boot.asm` | BIOS load + A20 + GDT + PM → `0x10000` |
| `entry.asm` / `kmain.c` | Entry point, boot bring-up, demo processes, serial shell |
| `vga.c` | 80×25 VGA text-mode framebuffer |
| `serial.c` | COM1 at 115200 baud (logging + shell I/O) |
| `isr.asm` / `idt.c` / `interrupt.c` | IDT, PIC, IRQ dispatch + ESP switch hook |
| `timer.c` | PIT IRQ0 ~100 Hz, drives preemptive scheduler |
| `sched_fs.c` | Round-robin preempt: save/restore interrupt frames, 4 process slots |
| `keyboard.c` | PS/2 IRQ1 driver, scancode → ASCII, shift/ctrl, `keyboard_readline()` |
| `ipc_fs.c` | Chess-protocol IPC (pool, queues, dispatch), mirrors hosted ipc.c |
| `linker.ld` | Kernel PE layout at 0x10000 |
| `mkimage.py` | PE → flat binary → floppy image packer |

## Build targets (freestanding)
```sh
make image          # build floppy image (pasinux.img)
make qemu           # launch QEMU GUI window
make qemu-headless  # headless (stdio serial, no display, no monitor)
make qemu-serial    # serial interactive (Ctrl-A X to exit)
```

## Next work
1. Enlarge kernel payload beyond current 100-sector (50 KiB) CHS read limit
2. Multi-track CHS reads in boot.asm for kernels exceeding one cylinder
3. Boot from real hardware (USB / PXE) — needs FAT or extLinux chainload
