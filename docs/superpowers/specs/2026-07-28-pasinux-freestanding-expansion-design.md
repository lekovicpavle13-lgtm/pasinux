# pasinux: Freestanding Expansion — LBA Boot, Heap Allocator & Unified Driver/IPC

**Date:** 2026-07-28
**Status:** Design (approved)

---

## Overview

Three independent sub-projects that expand pasinux from a minimal booting kernel into a
more capable hobby OS with proper memory management, an organized driver framework, and a
much larger kernel payload limit.

## Sub-project A: Boot sector overhaul (LBA + max floppy)

### Goal
Replace the single-track CHS read (32 sectors / 16 KiB limit) with LBA-based reads
supporting the full 1.44 MB floppy capacity (2880 sectors).

### Design decisions

- **LBA over extended CHS.** LBA (INT 0x13, AH=0x42) is geometry-independent, works on
  any media (floppy, HDD, USB), and paves the way toward real-hardware boot (item 5).
- **No fallback to CHS.** Old enough hardware to lack LBA (i.e., pre-PC-97) is not a
  target. The code does not attempt CHS fallback — if INT 0x13, AH=0x41 reports LBA
  unsupported, the boot hangs with a clear error message.
- **Chunked reads.** LBA's INT 0x13, AH=0x42 can read up to 127 sectors per call.
  2880 sectors → 23 calls of 127 + one of 31 (last chunk). The DAP buffer segment
  is re-computed for each chunk so no single read crosses a 64 KiB segment boundary.

### Files changed

| File | Change |
|------|--------|
| `boot.asm` | Rewrite disk-read path: add `lba_supported` check, DAP struct, chunked read loop. Keep A20/GDT/PM entry intact. |
| `mkimage.py` | Update `--kernel-sectors` default from 16 to 2880. |
| `Makefile` | Change `KERNEL_SECTORS := 32` to `2880`. |

### Key constants

- `LBA_SECTORS equ 2880` — max floppy capacity
- `LBA_CHUNK  equ 127` — max sectors per LBA call
- DAP is placed at a known low-memory location (e.g., `0x07E0` to avoid clobbering the
  boot sector or loaded kernel).

---

## Sub-project B: Freestanding MM port

### Goal
Port the existing hosted fixed-size-block allocator (`mm.c` / `mm.h`) to a freestanding
`mm_fs.c` / `mm_fs.h` with zero libc dependencies.

### Algorithm preserved from hosted

- 6 size classes (8, 16, 32, 64, 128, 256 bytes)
- Free-list recycling per size class
- Block splitting when an allocation leaves enough residual space
- Coalescing adjacent free blocks on `kfree`
- Stats: total_allocated, total_freed, current_usage, peak_usage, allocation_count,
  free_count, failed_allocations

### Freestanding adaptations

| libc dependency | Freestanding replacement |
|-----------------|--------------------------|
| `printf` | `serial_puts()` / `serial_put_u32()` for debug messages |
| `memset` | Inline `_memset_fs(void* s, int c, size_t n)` in `mm_fs.c` |
| `memcpy` | Inline `_memcpy_fs(void* d, const void* s, size_t n)` in `mm_fs.c` |
| `sizeof` / `offsetof` | Used normally — these are compile-time operators, not libc calls |

### Integration into kmain

- `freestanding_subsystems_up()`: replace the fake `"[MM] heap ready"` print with a
  real `init_memory()` call, which now prints the heap size itself.
- Shell's `mm` command: replaced with a call to `print_memory_stats()`.
- `freestanding_demo()`: replace `"[MM] no freestanding allocator wired up yet"` with
  real stats output.

### Files

| File | Role |
|------|------|
| `mm_fs.c` | Freestanding allocator implementation |
| `mm_fs.h` | Public API (`kmalloc`, `kfree`, `kcalloc`, `krealloc`, `init_memory`, `print_memory_stats`, `get_memory_stats`) |
| `mm.h` | Unchanged — still used by the hosted build (`mm.c`). `mm_fs.h` is an independent header. |
| `Makefile` | Add `mm_fs.o` to `FREE_OBJS`; remove `mm.o` (hosted only, no change needed — `FREE_OBJS` and `OBJS` are separate lists). |

---

## Sub-project C: Unified driver registry + IPC (`driver_fs.c`)

### Goal
Merge the hosted driver-registration framework (`driver.c`) with the existing freestanding
IPC (`ipc_fs.c`) into one unified `driver_fs.c`, then register all freestanding drivers
(VGA, serial, keyboard, timer) under the framework.

### Why merge
The hosted `driver.c` already bundles driver registration, IPC messaging, and chess-protocol
helpers into a single file. The freestanding `ipc_fs.c` is a parallel chess-IPC
implementation. Merging them into `driver_fs.c` mirrors the hosted architecture and
eliminates the redundant parallel implementation.

### Components

#### Driver registry (ported from `driver.c`)

- `driver_t` struct: `name`, `type` (CHAR / BLOCK / NET / INPUT), `ops` (init/open/close/read/write/ioctl), `next`
- `driver_register()`, `driver_lookup()`, `driver_get_list_head()`
- Registration order in `drivers_init_fs()`: serial → VGA → keyboard → timer
- Wire a `drivers` shell command to list all registered drivers with type and status

#### IPC + chess protocol (ported from `ipc_fs.c`)

- Message pool (`g_pool[IPC_FS_MAX_MESSAGES]`), usage bitmap (`g_used[]`), 4 priority queues
- `ipc_fs_poll(max_msgs)` → dispatches `chess_message_t` via the same `switch` statement
- `ipc_chess_send_move()`, `ipc_chess_send_state()`, `ipc_chess_send_draw_offer()`, etc.
- All function signatures stay identical — `kmain.c` callers need zero changes

### Cleanup

| File | Action |
|------|--------|
| `ipc_fs.c` | Delete (replaced by `driver_fs.c`) |
| `ipc_fs.h` | Delete (replaced by `driver_fs.h`) |
| `driver_fs.c` | New — unified driver registration + IPC |
| `driver_fs.h` | New — public API header |
| `Makefile` | Replace `ipc_fs.o` with `driver_fs.o` in `FREE_OBJS` |

### Integration

- `freestanding_subsystems_up()`: replace the five individual `[DRIVER]` `serial_puts` lines
  with a single `drivers_init_fs()` call.
- Shell: replace `shell_info`'s hardcoded features string with `driver_get_list_head()`
  enumeration — dynamically show what's registered.

---

## Build & verification

```sh
make image          # must succeed — all 2880 sectors allocated
make qemu           # boots with working shell, scheduler, IPC
make run            # hosted build must still work unchanged
make gui            # Win32 GUI must still build
```

### Verification checklist

| Check | How |
|-------|-----|
| LBA boot works | QEMU boots, `[KERNEL]` log visible on serial |
| MM allocates | Shell `mm` command shows stats > 0 |
| Driver registry populated | Shell `info` lists VGA, serial, keyboard, timer |
| IPC still works | Demo flow sends/receives chess messages |
| Hosted simulator unaffected | `make run` passes |
| 2880-sector image builds | `pasinux.img` is 512 + 2880×512 = 1,474,112 bytes |

---

## Future work (not in scope)

- Real-hardware boot (USB/PXE) — requires FAT filesystem or Syslinux chainload
- Multi-threaded/dynamic process creation
- Disk driver (ATA/PATA) for file-backed storage