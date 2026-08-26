# FAT12 Write, ELF32 Loader, and Notepad Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Enable file/folder creation on FAT12, implement a physical page allocator and ELF32 loader for user-mode processes, and ship a user-space notepad.

**Architecture:** 
1. Extend `fat12.c` with a write path that flushes changes to the image.
2. Implement a PMM to provide user-space frames and an ELF loader to map binaries into separate page directories.
3. Expand the syscall table to provide file and console I/O to user-space, then build the notepad app.

**Tech Stack:** C11, x86 Assembly, i686-ELF Cross-Toolchain, QEMU.

## Global Constraints

- **Volume Geometry:** 1.44 MB FAT12 floppy (Sectors 1-9 FAT1, 10-18 FAT2, 19-32 Root Dir).
- **User Mode:** Ring 3, CS=0x18|3, SS=0x20|3.
- **Binary Format:** ELF32 (via i686-elf-gcc/ld).
- **Persistence:** All FAT mutations must call `write_sector` to survive reboots.
- **Scope:** Root-level files and folders only; no nested directory traversal.

---

## Phase 1: FAT12 Write Path & Folders

### Task 1: FAT12 Allocation Helpers
**Files:**
- Modify: `fs/fat12.c`, `fs/fat12.h`

**Interfaces:**
- Produces: `fat12_find_free_cluster`, `fat12_alloc_chain`, `fat12_free_chain`, `fat12_flush_fat`, `fat12_flush_root`.

- [x] **Step 1: Implement `fat12_find_free_cluster`**
  Scan `fs->fat` for first entry `== 0` starting from cluster 2.
- [x] **Step 2: Implement `fat12_alloc_chain`**
  Find `n` free clusters, link them in `fs->fat`, set last to `0xFFF`.
- [x] **Step 3: Implement `fat12_free_chain`**
  Walk chain from `first`, set each entry to `0`.
- [x] **Step 4: Implement `fat12_flush_fat` and `fat12_flush_root`**
  Write `fs->fat` to LBA 1 & 10; write `fs->root_dir` to LBA 19 using `write_sector`.
- [x] **Step 5: Build and verify logic via kernel logs.**
- [x] **Step 6: Commit** `feat(fs): add FAT12 allocation and flush helpers`

### Task 2: File Creation and Deletion
**Files:**
- Modify: `fs/fat12.c`, `kernel/kmain.c`

**Interfaces:**
- Consumes: `fat12_flush_root`, `fat12_free_chain`.
- Produces: `fat12_create_file`, `fat12_delete_file`.

- [x] **Step 1: Implement `fat12_create_file`**
  Find free slot (`0x00` or `0xE5`), write entry (`attr=0x20`, `size=0`), call `fat12_flush_root`.
- [x] **Step 2: Implement `fat12_delete_file`**
  Find entry, `fat12_free_chain`, mark slot `0xE5`, `fat12_flush_root`.
- [x] **Step 3: Add `touch <name>` and `rm <name>` to shell in `kmain.c`**.
- [x] **Step 4: Verify in QEMU:** `touch test.txt` $\rightarrow$ `ls` $\rightarrow$ `rm test.txt` $\rightarrow$ `ls`.
- [x] **Step 5: Commit** `feat(fs): implement fat12_create_file and fat12_delete_file`

### Task 3: File Writing and Durable Persistence
**Files:**
- Modify: `fs/fat12.c`, `kernel/kmain.c`

**Interfaces:**
- Consumes: `fat12_alloc_chain`, `fat12_flush_fat`, `fat12_flush_root`.
- Produces: `fat12_write_file`.

- [x] **Step 1: Implement `fat12_write_file`**
  Find/create entry, free existing chain, `fat12_alloc_chain(ceil(size/cluster_size))`, `write_sector` data, update size/first_cluster, flush FAT and root.
- [x] **Step 2: Add `write <file> <text>` to shell in `kmain.c`**.
- [x] **Step 3: Verify Durable Writes:** `write hello.txt world` $\rightarrow$ reboot QEMU $\rightarrow$ `cat hello.txt`.
- [x] **Step 4: Commit** `feat(fs): implement fat12_write_file with durable persistence`

### Task 4: Directory Creation
**Files:**
- Modify: `fs/fat12.c`, `kernel/kmain.c`

**Interfaces:**
- Produces: `fat12_create_dir`.

- [x] **Step 1: Implement `fat12_create_dir`**
  Create entry (`attr=0x10`), alloc 1 cluster, write `.` and `..` entries into that cluster, flush.
- [x] **Step 2: Add `mkdir <name>` to shell in `kmain.c`**.
- [x] **Step 3: Verify in QEMU:** `mkdir docs` $\rightarrow$ `ls`.
- [x] **Step 4: Commit** `feat(fs): implement fat12_create_dir for root-level folders`

---

## Phase 2: PMM and ELF Loader

### Task 5: Physical Frame Allocator (PMM)
**Files:**
- Create: `mm/pmm.c`, `mm/pmm.h`
- Modify: `Makefile` (add `pmm.o`)

**Interfaces:**
- Produces: `pmm_alloc_frame()`, `pmm_free_frame()`.

- [x] **Step 1: Implement bump allocator** in `pmm.c` from a fixed base (e.g., 4MB) to 128MB.
- [x] **Step 2: Implement `pmm_alloc_frame`** returning physically contiguous 4KB frames.
- [x] **Step 3: Implement `pmm_free_frame`** (stub as a no-op or basic bitmap if needed, but bump is sufficient for the loader).
- [x] **Step 4: Commit** `feat(mm): add physical frame allocator`

### Task 6: ELF32 Loader Logic
**Files:**
- Create: `fs/elf.c`, `fs/elf.h`
- Modify: `Makefile` (add `elf.o`)

**Interfaces:**
- Consumes: `fat12_read_file`, `pmm_alloc_frame`, `paging_map_page`.
- Produces: `user_prog_exec(fs, name)`.

- [x] **Step 1: Implement ELF header validation** (\x7FELF, 32-bit, x86).
- [x] **Step 2: Implement PT_LOAD segment mapping** using `pmm_alloc_frame` and `paging_map_page`.
- [x] **Step 3: Implement user stack mapping** at the top of the lower half.
- [x] **Step 4: Implement `user_prog_exec`** to coordinate reading, mapping, and returning the entry point.
- [x] **Step 5: Commit** `feat(fs): implement ELF32 binary loader`

### Task 7: User-Mode Process Integration
**Files:**
- Modify: `sched/sched_fs.c`, `sched/sched_fs.h`, `kernel/kmain.c`

**Interfaces:**
- Consumes: `user_prog_exec`.
- Produces: `sched_fs_create_process_user(path)`.

- [x] **Step 1: Update `process_t`** to include `uint32_t cr3` and `fd_entry_t fds[16]`.
- [x] **Step 2: Implement `sched_fs_create_process_user`** $\rightarrow$ call `user_prog_exec` $\rightarrow$ set up ring-3 iret frame.
- [x] **Step 3: Update scheduler context switch** to load `proc->cr3` into CR3 register.
- [x] **Step 4: Add `run <app>` command** to shell in `kmain.c`.
- [x] **Step 5: Commit** `feat(sched): implement user-mode process loading and CR3 switching`

---

## Phase 3: Syscalls and Notepad

### Task 8: Syscall Table Expansion
**Files:**
- Modify: `arch/syscall.h`, `arch/syscall.c`

**Interfaces:**
- Produces: `SYS_OPEN`, `SYS_CLOSE`, `SYS_READ`, `SYS_WRITE`, `SYS_SEEK`, `SYS_BRK`, `SYS_EXEC`.

- [x] **Step 1: Define new syscall IDs** in `syscall.h`.
- [x] **Step 2: Implement global FD table** in `syscall.c`.
- [x] **Step 3: Implement `handle_open`** calling `fat12_find_file` or `fat12_create_file`.
- [x] **Step 4: Implement `handle_read` and `handle_write`** for file FDs using `fat12_read_file` and `fat12_write_file`.
- [x] **Step 5: Implement `handle_close`** to free FD slot.
- [x] **Step 6: Commit** `feat(syscall): expand syscall table with file I/O`

### Task 9: Console Syscalls (fd 0 & 1)
**Files:**
- Modify: `arch/syscall.c`

**Interfaces:**
- Produces: `SYS_READ(0, ...)` and `SYS_WRITE(1, ...)`.

- [x] **Step 1: Implement `SYS_WRITE` for fd 1** using `vga_puts`.
- [x] **Step 2: Implement `SYS_READ` for fd 0** using `keyboard_readline_vga`.
- [x] **Step 3: Verify with a simple user-mode "Hello World" ELF**.
- [x] **Step 4: Commit** `feat(syscall): implement console I/O via syscalls`

### Task 10: Notepad Application
**Files:**
- Create: `user/notepad.c`, `user/user.ld`
- Modify: `Makefile` (add notepad build target)

**Interfaces:**
- Consumes: `SYS_OPEN`, `SYS_READ`, `SYS_WRITE`, `SYS_CLOSE`, `SYS_EXIT`.

- [x] **Step 1: Write `user/user.ld`** to set the entry point and memory layout for ELF32.
- [x] **Step 2: Implement `notepad.c` core loop** (text buffer, VGA rendering via `SYS_WRITE`).
- [x] **Step 3: Implement `:open`, `:save`, `:new`, `:quit` commands**.
- [x] **Step 4: Add notepad build target to Makefile** using `i686-elf-gcc`.
- [x] **Step 5: Commit** `feat(user): implement user-mode notepad application`

### Task 11: Image Integration and Final Test
**Files:**
- Modify: `boot/mkimage.py`, `kernel/kmain.c`, `Makefile`

**Interfaces:**
- Produces: `pasinux.img` containing `NOTEPAD.BIN`.

- [x] **Step 1: Update `mkimage.py`** to accept `--program` and create a FAT12 file entry for the notepad.
- [x] **Step 2: Add `notepad` command** to shell in `kmain.c` that calls `sched_fs_create_process_user("NOTEPAD")`.
- [x] **Step 3: Final Integration Test in QEMU:**
  1. `notepad` $\rightarrow$ type text $\rightarrow$ `:save note.txt` $\rightarrow$ `:quit`.
  2. `cat note.txt` (verify content).
  3. Reboot $\rightarrow$ `cat note.txt` (verify persistence).
- [x] **Step 4: Commit** `feat: finalize notepad integration and FAT12 write path`
