# Implementation Plan: pasinux Freestanding Expansion

**Based on:** `docs/superpowers/specs/2026-07-28-pasinux-freestanding-expansion-design.md`
**Order:** Sequential — each phase produces a bootable kernel

---

## Phase A: Boot Sector Overhaul (LBA + max floppy)

### Steps
1. **Edit `boot.asm`** — Add LBA support check (INT 0x13, AH=0x41), DAP structure, chunked read loop (127 sectors/call, 23 chunks + 1 remainder)
2. **Edit `mkimage.py`** — Change `--kernel-sectors` default from 16 to 2880
3. **Edit `Makefile`** — Change `KERNEL_SECTORS := 32` to 2880
4. **Verify** — `make image` succeeds, `pasinux.img` is 1,474,112 bytes

### Key files
- `pasinux/kernel/boot.asm`
- `pasinux/kernel/mkimage.py`
- `pasinux/kernel/Makefile`

---

## Phase B: Freestanding MM Port

### Steps
1. **Create `mm_fs.h`** — Public API: `kmalloc`, `kfree`, `kcalloc`, `krealloc`, `init_memory`, `print_memory_stats`, `get_memory_stats`
2. **Create `mm_fs.c`** — Port from `mm.c`:
   - Replace `printf` → `serial_puts`/`serial_put_u32`/`serial_put_u64`
   - Inline `_memset_fs` and `_memcpy_fs`
   - Keep 6 size classes (8/16/32/64/128/256), coalescing, splitting, free-list recycling, stats
3. **Edit `kmain.c`** — Wire `init_memory()` in `freestanding_subsystems_up()`; replace fake MM prints with real stats
4. **Edit `Makefile`** — Add `mm_fs.o` to `FREE_OBJS`
5. **Verify** — `make image` + QEMU boots, `mm` shell command shows real stats

### Key files
- `pasinux/kernel/mm_fs.h` (new)
- `pasinux/kernel/mm_fs.c` (new)
- `pasinux/kernel/kmain.c`
- `pasinux/kernel/Makefile`

---

## Phase C: Unified Driver Registry + IPC

### Steps
1. **Create `driver_fs.h`** — Public API: `driver_t` struct, `driver_register`, `driver_lookup`, `driver_get_list_head`, `drivers_init_fs`, plus all IPC/chess functions from `ipc_fs.h`
2. **Create `driver_fs.c`** — Merge:
   - Driver registration framework from `driver.c` (driver_t, linked list, typed drivers)
   - IPC + chess protocol from `ipc_fs.c` (message pool, priority queues, chess dispatch)
   - Register VGA, serial, keyboard, timer in `drivers_init_fs()`
3. **Edit `kmain.c`** — Replace individual `[DRIVER]` puts with `drivers_init_fs()`; wire `drivers` shell command
4. **Remove `ipc_fs.c` and `ipc_fs.h`** — Replaced by `driver_fs.c`/`.h`
5. **Edit `Makefile`** — Replace `ipc_fs.o` with `driver_fs.o` in `FREE_OBJS`
6. **Verify** — `make image` + QEMU boots, `info` shows registered drivers, IPC chess demo works

### Key files
- `pasinux/kernel/driver_fs.h` (new)
- `pasinux/kernel/driver_fs.c` (new)
- `pasinux/kernel/kmain.c`
- `pasinux/kernel/ipc_fs.c` (delete)
- `pasinux/kernel/ipc_fs.h` (delete)
- `pasinux/kernel/Makefile`

---

## Verification (after all phases)
```sh
make image          # must succeed, 2880 sectors
make qemu           # boots, shell, scheduler, IPC all work
make run            # hosted simulator still works
make gui            # Win32 GUI still builds
```