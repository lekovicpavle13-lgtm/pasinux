# pasinux — FAT12 write path, ELF32 user-mode loader, and notepad

Date: 2026-08-10
Status: Approved (sections 1–3) — awaiting final spec review

## 1. Goal

Make the pasinux kernel's existing FAT12 filesystem support **creating files and
folders** (currently the write/create/delete path is stubbed to return `-1`), add
a **real ELF32 user-mode loader**, and ship a **user-space notepad** that reads and
writes files through an expanded syscall table. Built in dependency order:
**FAT12 write → ELF loader → notepad**.

## 2. Current state (verified)

- The bootable kernel lives at `pasinux/pasinux/kernel/` (its own git repo, branch
  `main`). `Makefile` links `drivers/ata.o` and `fs/fat12.o` into the freestanding
  image `pasinux.img`.
- The volume is a genuine FAT12 1.44 MB floppy built by `boot/mkimage.py` and
  booted as an IDE volume by `boot/boot.asm`:
  - LBA 0 boot · LBA 1–9 FAT1 · LBA 10–18 FAT2 (mirror) · LBA 19–32 root dir
    (224 × 32-byte entries) · LBA 33+ data area. `KERNEL.BIN` starts at cluster 2;
    ~2842 clusters are free.
- `fs/fat12.c` already implements the **read** path (mount, find, read, FAT12
  pack/unpack, cluster→sector) plus a working `write_sector`. The **write** path is
  stubbed: `fat12_create_file`, `fat12_write_file`, `fat12_delete_file` all
  `return -1;`. There is no `fat12_create_dir`.
- `drivers/ata.c` is a real ATA PIO driver (28-bit LBA, polling, read+write
  sectors) registered as `DRIVER_TYPE_BLOCK` with `ATA_IOCTL_SEEK`.
- `arch/paging.c` provides the loader primitives: `paging_create_pd()` (fresh PD
  with empty user space + kernel higher half copied) and
  `paging_map_page(pd, vaddr, phys, flags)`. There is **no physical-frame
  allocator**; the existing `ring3_demo` reuses `kmalloc` heap pages as a user
  stack (a shortcut we will not replicate).
- `arch/syscall.c` handles `int 0x80` with only `SYS_PRINT(0)`, `SYS_EXIT(1)`,
  `SYS_GETTIME(2)`. User mode today is `user/user_start.asm`, a hard-coded
  ring-3 stub linked into the kernel and jumped via `launch_ring3(entry, stack)`
  (`CS=0x18|3, SS=0x20|3`).
- **Toolchain constraint:** the bundled MinGW `ld` supports only `i386pe`/`i386pep`
  (PE), so a true i386 **ELF** executable cannot be linked here. An i686-ELF cross
  toolchain is required to produce the notepad binary (see §3.2).

## 3. Design

### 3.1 FAT12 write path + folders (`fs/fat12.c`, `kernel/kmain.c`)

**New/updated helpers:**

| Helper | Purpose |
|---|---|
| `fat12_find_free_cluster(fs)` | scan `fs->fat` for an entry `== 0`, past KERNEL's clusters; return cluster or `0`. |
| `fat12_alloc_chain(fs, n)` | allocate `n` free clusters, link them (last → `0xFFF` EOF), return first. |
| `fat12_free_chain(fs, first)` | walk chain setting each entry to `0` (free). |
| `fat12_flush_fat(fs)` | write `fs->fat` to **both** FAT1 (LBA 1) and FAT2 (LBA 10) via `write_sector`. |
| `fat12_flush_root(fs)` | write `fs->root_dir` back at LBA 19. |
| `fat12_max_cluster(fs)` | derive usable limit from `total_sectors`/`first_data_sector`. |

**Public ops (replace the stubs):**

- `fat12_create_file(fs, name)` — format 8.3, find a free root slot (`0x00`/`0xE5`),
  write entry (`attr=0x20`, `first_cluster=0`, `size=0`), flush root dir.
- `fat12_write_file(fs, name, data, size)` — find or create the entry; free any
  existing chain; `alloc_chain(ceil(size / bytes_per_cluster))`; write each data
  sector via `write_sector`; update entry `first_cluster`+`size`; flush FAT + root.
- `fat12_delete_file(fs, name)` — find entry, `free_chain`, mark slot `0xE5`, flush.
- `fat12_create_dir(fs, name)` — new root entry `attr=0x10`, allocate one zeroed
  cluster, write `.`/`..` entries into it, set `first_cluster`, flush. Files stay
  flat in root (per decision); subdir traversal is out of scope for this pass.

**Persistence:** every mutation flushes synchronously to the volume image via
`write_sector`, so a reboot of the same `pasinux.img` retains the changes. The
allocator only ever takes free clusters, so `KERNEL.BIN` is never touched.

**Shell commands (`kmain.c`):** add `touch <file>`, `mkdir <dir>`, `rm <file>`,
`write <file> <text…>` to test end-to-end.

### 3.2 i686-ELF cross toolchain (environment setup)

Required to **link** the notepad ELF. Prefer one of:

- **WSL (recommended):** `sudo apt install gcc-i686-linux-gnu binutils-i686-linux-gnu`
- **MSYS2 / prebuilt:** a Windows `i686-elf-gcc` bundle (e.g. *lordmilko/i686-elf-tools*).

The Makefile exposes `$(ELFCC)`/`$(ELFLD)` (with a fallback to `i686-linux-gnu-gcc`
/ `i686-linux-gnu-ld` so WSL binaries resolve on PATH). The notepad is built:

```
$(ELFCC) -m32 -ffreestanding -fno-pie -no-pie -nostdlib user/notepad.c \
         -T user/user.ld -o build/NOTEPAD.elf
```

`boot/mkimage.py` grows a `--program <elf> --program-name NOTEPAD.BIN` option to
write the app as a second root-dir entry + FAT cluster chain, leaving `KERNEL.BIN`
and its chain intact.

### 3.3 User-frame allocator (`mm/pmm.c`, new)

Add `pmm_alloc_frame()`/`pmm_free_frame()` handing out **physical** frames from a
free region (bump from a fixed physical base up to a bound; QEMU `-machine pc`
provides ≥128 MiB). The loader maps each frame into the user PD at its user vaddr
(`PRESENT|WRITABLE|USER`) **and** a kernel scratch alias so ring-0 can `memcpy`
into it. This is the proper replacement for the `kmalloc`-as-frame shortcut.

### 3.4 ELF loader (`fs/elf.c`, new)

```
int user_prog_exec(fat12_fs_t *fs, const char *name)
```

1. `fat12_find_file` + `fat12_read_file` → whole ELF in a temp heap buffer.
2. Validate `\x7FELF`, 32-bit LSB, `EM_386`, `ET_EXEC`; walk program headers.
3. Per `PT_LOAD`: `pmm_alloc_frame` per page, `paging_map_page(user_pd, seg_vaddr,
   frame, PRESENT|WRITABLE|USER)`, `memcpy` `filesz` from the temp buffer via the
   scratch alias, zero `memsz−filesz` (BSS).
4. Map a user stack region near the top of the lower half; store `entry` and user
   `esp`.
5. `CR3 ← user_pd`; set the TSS kernel stack; build the same iret frame
   `launch_ring3` uses (`CS=0x18|3, SS=0x20|3`) and jump.

`ring3_demo`/`user_start` is left untouched (no regression).

### 3.5 File + console syscalls (`arch/syscall.h`, `arch/syscall.c`)

```
SYS_PRINT  0  exists
SYS_EXIT   1  exists
SYS_GETTIME 2 exists
SYS_OPEN   3  (path *ebx, flags *ecx) -> fd
SYS_CLOSE  4  (fd *ebx)
SYS_READ   5  (fd *ebx, buf *ecx, len *edx) -> bytes
SYS_WRITE  6  (fd *ebx, buf *ecx, len *edx) -> bytes
```

- A small kernel-side **global fd table** (8 entries) in `syscall.c` maps
  `fd → { fat12_fs_t*, file_info_t, pos, mode }` — sufficient for the single
  foreground ring-3 app; no per-process PCB plumbing.
- **fd 0 = stdin** → `keyboard_readline_vga`; **fd 1 = stdout** → `vga_puts`.
- File `SYS_OPEN(path, flags)` with `O_READ / O_WRITE / O_RDWR / O_CREATE`: look up
  via `fat12_find_file`, or `fat12_create_file` when `O_CREATE` and missing.
- `SYS_READ` on a file fd → `fat12_read_file` (whole file, copy `pos..len`,
  advance `pos`, kfree). `SYS_WRITE` on a file fd → `fat12_write_file(name, buf,
  len)` (truncate-overwrite = "save").

### 3.6 Notepad (`user/notepad.c`, compiled to ELF32)

A line-oriented editor over the 80×25 VGA console via `SYS_WRITE(1,…)` /
`SYS_READ(0,…)`:

- Text buffer kept in the app (scrollable line array).
- Status line (filename, line count), ~22 text lines, command line at bottom.
- Line ops (append / insert / delete line) and commands
  `:open <file>`, `:save <file>`, `:new`, `:quit` (quits via `SYS_EXIT`).

### 3.7 Integration / file inventory

**Modified:** `fs/fat12.c` · `fs/fat12.h` · `arch/syscall.c` · `arch/syscall.h` ·
`kernel/kmain.c` (shell commands) · `boot/mkimage.py` (`--program`) · `Makefile`
(`pmm.o`/`elf.o` in `FREE_OBJS`, ELFCC/ELFLD vars, notepad target).

**Added:** `mm/pmm.c` (frame allocator) · `fs/elf.c` (+ `fs/elf.h`) ·
`user/notepad.c` · `user/user.ld`.

## 4. Error handling

- All FAT mutations return `-1` and leave the on-disk state untouched on failure
  (allocate before mutating the directory, flush last).
- ELF rejection (bad magic/class/machine/type) frees the temp buffer and returns
  failure without touching CR3 or the PD.
- Out-of-frames / out-of-clusters cause a clean `-1`/`NULL`, never a partial write
  with a corrupt FAT.

## 5. Testing

- **FAT12 (QEMU):** `touch hello.txt` → `write hello.txt hi` → `cat hello.txt` →
  `mkdir docs` → `rm hello.txt` → `ls`. Because the volume is the host
  `pasinux.img`, creating a file, rebooting QEMU, and `cat`-ing it again proves real
  durable writes.
- **Loader + notepad (QEMU):** `notepad` → `:new` → type lines → `:save test.txt` →
  `:quit` → `cat test.txt` (content matches) → reboot → `cat test.txt` (persisted).
- The existing read-path self-test in `kmain.c` must still print `[FS] selftest:
  KERNEL.BIN … read … bytes: OK`.

## 6. Out of scope (defer)

- Nested subdirectory traversal (files-in-folders). Folders are real FAT12 subdirs
  (own cluster, `.`/`..`) but are not descended into for file ops.
- Per-process fd tables / preemptively scheduled *user* processes.
- Long file names (LFN), FAT32/exFAT.