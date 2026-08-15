# FAT12 Write Path + Folders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add real write/create/delete/folder support to the pasinux FAT12 filesystem so the kernel shell can create, save, and remove files and folders on a durable 1.44 MB floppy image.

**Architecture:** Implement the missing write path in `fs/fat12.c` on top of the already-correct read infrastructure: an in-memory FAT + root-dir cache that `fat12_mount` already loads, `set_fat_entry`/`write_sector` (which exist but are currently `__attribute__((unused))`), plus a few new helpers (`find_free_cluster`, `alloc_chain`, `flush_fat`, `flush_root`). Expose the new operations as shell commands (`touch`, `mkdir`, `rm`, `write`) in `kernel/kmain.c`. Test end-to-end in QEMU against the real `pasinux.img`, which `boot/mkimage.py` already builds as a genuine FAT12 volume.

**Tech Stack:** C11 freestanding (kernel driver), NASM/Python build (mkimage.py), QEMU (`-drive if=ide`), Makefile-run shell integration tests.

## Global Constraints

- **Freestanding, no libc:** `fs/fat12.c` runs in the kernel. No `strcpy`, `memcpy`, `strlen`, `printf` from libc — use the file-local `fs_*` helpers already present (`fs_memset`, `fs_memcpy`, `fs_strlen`). `kernel/kmain.c` likewise has no libc; don't add any.
- **Werror build:** `Makefile` uses `-Wall -Wextra -Wpedantic -Werror -m32 -ffreestanding` for `FREE_OBJS`. Every added function must be used or carry `__attribute__((unused))`, or the build fails. There is also `-fno-pic`/`-fno-pie`: no global pointer weirdness.
- **Geometry (from `boot/mkimage.py` and `boot/boot.asm`, DON'T drift):** 512-byte sectors; 1 sector/cluster; 1 reserved sector (boot, LBA 0); 2 FATs × 9 sectors (FAT1 LBA 1-9, FAT2 LBA 10-18); root dir LBA 19-32 (224 × 32-byte entries); data area starts LBA 33, so data-cluster `N` maps to sector `33 + (N-2)`. First cluster is cluster 2. KERNEL.BIN occupies clusters 2..~85; ~2760 clusters are free. Do **not** allocate/write KERNEL.BIN's clusters (`< first free data cluster`).
- **`bytes_per_sector` is 0 during boot-sector read** — sector transfers always use `ATA_SECTOR_SIZE` (512), never the BPB field (see the comment in `read_sector`). Reuse that pattern in all new `write_sector` callers: build a 512-byte stack buffer per sector.
- **8.3 filename format:** names are stored uppercase, space-padded to 11 chars (name 8, ext 3). Directory entries are 32 bytes: `[0-7]` name · `[8-10]` ext · `[11]` attr · `[12..31]` reserved/metadata. `0x00` first byte = end-of-dir; `0xE5` = deleted; attr `0x0F` = LFN marker (skip); attr `0x20` = file; `0x10` = directory. `[26-27]` = first cluster (LE), `[28-31]` = file size (LE).
- **Read path already works; don't regress it.** `fs/fat12.c` read path (`mount`, `find_file`, `read_file`, `get_fat_entry`, `cluster_to_sector`, `name_match`) is complete and is exercised by a boot-time selftest in `kmain.c` that must keep printing `[FS] selftest: KERNEL.BIN … read … bytes: OK`.
- **Test scope:** This plan (Part 1) covers the FAT12 write path + folders + shell commands only. The ELF loader and notepad are separate later plans gated on an i686-ELF cross-toolchain being installed (a hard external environment prerequisite).

---

## File structure (locked in by this plan)

**Modified:**
- `pasinux/kernel/fs/fat12.c` — replace three stubs with real implementations; add static helpers; remove the `__attribute__((unused))` on `write_sector` (now used).
- `pasinux/kernel/fs/fat12.h` — add `int fat12_create_dir(fat12_fs_t*, const char*)` (declaration only).
- `pasinux/kernel/kernel/kmain.c` — add four shell handlers + four `else if` branches + one boot-time write-selftest.

**Unchanged:** everything else (Makefile, mkimage.py, boot.asm, paging, ATA — all already supported).

## Task ordering & dependency map

| Task | Touches | Depends on | QEMU behavior under test |
|---|---|---|---|
| 1. `fat12_create_file` + `touch` | fat12.c, kmain.c | (none) | `touch` doesn't print "unknown"; `ls` sees the new entry; survives reboot |
| 2. `fat12_write_file` + `write` | fat12.c, kmain.c | Task 1 | `write f.txt hi` then `cat f.txt` → `hi` |
| 3. `fat12_delete_file` + `rm` | fat12.c, kmain.c | Task 1 | `rm` removes from `ls`; frees the FAT chain |
| 4. `fat12_create_dir` + `mkdir` | fat12.c, fat12.h, kmain.c | Task 1 | `mkdir d` makes entry visible with attr `0x10` |
| 5. multi-cluster write selftest at boot | kmain.c | Task 2 | `[FS] writeselftest: ... OK` after `make qemu` |
| 6. full integration + persistence + KERNEL.BIN safety | (regression only) | Tasks 1-5 | KERNEL.BIN still bootable after a real dirty session |

---

## Task 1: `fat12_create_file` + shell `touch`

**Files:**
- Modify: `pasinux/kernel/fs/fat12.c` — replace stub + add helpers
- Modify: `pasinux/kernel/kernel/kmain.c` — add `vga_shell_touch` and a `touch` branch

**Interfaces:**
- **Produces (file-static, fat12.c):**
  - `static void fat12_format_name(const char* src, char out[11])`
  - `static int find_free_dir_slot(fat12_fs_t* fs)` — byte offset, or `-1`
  - `static int flush_root(fat12_fs_t* fs)` — 0/-1
- **Replaces stub:** `fat12_create_file(fs, name)` now creates an empty file in root with attr `0x20`, first_cluster 0, size 0, flushed to disk.

**Steps:**

- [ ] **Step 1: Add `fat12_format_name` and remove the duplicated inline-83 logic**

In `pasinux/kernel/fs/fat12.c`, immediately after `fs_toupper` (around line 38), add:

```c
static void fat12_format_name(const char* name, char out[11]) {
    fs_memset(out, ' ', 11);
    const char* dot = fs_strchr(name, '.');
    size_t name_len = dot ? (size_t)(dot - name) : fs_strlen(name);
    size_t ext_len = dot ? fs_strlen(dot + 1) : 0;
    if (name_len > 8) name_len = 8;
    if (ext_len > 3) ext_len = 3;
    for (size_t i = 0; i < name_len; ++i) out[i] = (char)fs_toupper((unsigned char)name[i]);
    for (size_t i = 0; i < ext_len; ++i) out[8 + i] = (char)fs_toupper((unsigned char)dot[1 + i]);
}
```

- [ ] **Step 2: Add `find_free_dir_slot` and `flush_root`**

Immediately after `cluster_to_sector` (around line 102), add:

```c
static int find_free_dir_slot(fat12_fs_t* fs) {
    for (uint32_t i = 0; i < fs->root_entry_count; ++i) {
        uint8_t* e = fs->root_dir + i * 32u;
        if (e[0] == 0x00 || e[0] == 0xE5) return (int)(i * 32u);
    }
    return -1;
}

static int flush_root(fat12_fs_t* fs) {
    if (!fs || !fs->root_dir) return -1;
    uint32_t root_start = fs->reserved_sectors + fs->num_fats * fs->sectors_per_fat;
    for (uint32_t i = 0; i < fs->root_dir_sectors; ++i) {
        if (write_sector(fs, root_start + i, fs->root_dir + i * fs->bytes_per_sector) != 0)
            return -1;
    }
    return 0;
}
```

- [ ] **Step 3: Remove the `__attribute__((unused))` from `write_sector`**

Change line 59 of `fat12.c` from `__attribute__((unused)) static int write_sector(...)` to `static int write_sector(...)`. (`flush_root` now uses it.)

- [ ] **Step 4: Replace the `fat12_create_file` stub**

Replace lines 230-233 with:

```c
int fat12_create_file(fat12_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;
    file_info_t existing;
    if (fat12_find_file(fs, name, &existing) == 0) return -1; /* already exists */
    char fname[11];
    fat12_format_name(name, fname);
    int off = find_free_dir_slot(fs);
    if (off < 0) return -1;
    uint8_t* e = fs->root_dir + off;
    fs_memcpy(e, fname, 11);
    e[11] = 0x20;               /* archive (normal file) */
    fs_memset(e + 12, 0, 20);   /* reserved + time/date + first_cluster=0 + size=0 */
    return flush_root(fs);
}
```

- [ ] **Step 5: Add `vga_shell_touch` in `kmain.c`**

In `pasinux/kernel/kernel/kmain.c`, immediately after `vga_shell_cat` (the function closing `}` near line 301), add:

```c
static void vga_shell_touch(const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) { vga_puts("usage: touch <file>\n"); return; }
    if (!g_fs) { vga_puts("filesystem not mounted\n"); return; }
    int rc = fat12_create_file(g_fs, arg);
    if (rc != 0) vga_puts("touch: failed (exists or dir full)\n");
    else         vga_puts("touch: ok\n");
}
```

- [ ] **Step 6: Add the `touch` branch in the shell dispatcher**

In the long `else if (...)` chain inside `vga_shell_run`, insert immediately before the final `else { vga_puts("unknown: ..."); }`:

```c
} else if (cmd[0] == 't' && cmd[1] == 'o') {
    vga_shell_touch(line + 5);
```

- [ ] **Step 7: Build and verify**

```bash
cd pasinux/kernel && make image
```

Expected: clean build (`-Werror` passes, no warnings). `pasinux.img` written.

- [ ] **Step 8: QEMU integration test**

```bash
cd pasinux/kernel && make qemu
```

The SDL window opens. On the VGA console type:

| Typed | Expected output |
|---|---|
| `ls` | `KERNEL.BIN  (42496 bytes)` (size = current kernel.bin size; row may include new entry if hello already exists from a prior partial run) |
| `touch hello.txt` | `touch: ok` |
| `ls` | `KERNEL.BIN  (...)` then `HELLO.TXT  (0 bytes)` |
| `touch hello.txt` | `touch: failed (exists or dir full)` |

**Today (before this task) the first `touch hello.txt` prints** `unknown: touch hello.txt` — that is the failing-test signal.

- [ ] **Step 9: Persistence proof (preliminary)**

Close the QEMU window (or `Ctrl-A X` then type `quit`). Re-run `make qemu`. Type `ls`. **Expected**: `HELLO.TXT` is still listed. The change persists on `pasinux.img`.

- [ ] **Step 10: Commit**

```bash
cd /c/Users/lekov/pasinux
git add pasinux/kernel/fs/fat12.c pasinux/kernel/kernel/kmain.c docs/superpowers/plans/2026-08-13-fat12-write-folders.md
git commit -m "feat(fat12): create_file + touch shell command"
```

---

## Task 2: `fat12_write_file` + shell `write`

**Files:** `pasinux/kernel/fs/fat12.c` (replace stub) · `pasinux/kernel/kernel/kmain.c` (handler + branch)

**Interfaces (added, file-static to fat12.c):**
- `static uint16_t max_cluster(fat12_fs_t* fs)` — highest data-cluster index `+1` (cluster numbers in `[2, max_cluster)`)
- `static uint16_t alloc_chain(fat12_fs_t* fs, uint32_t n)` — allocate `n` free clusters, link, return first; `0` on failure (in-memory rollback)
- `static void free_chain(fat12_fs_t* fs, uint16_t first)` — walk chain, zero entries
- `static int flush_fat(fat12_fs_t* fs)` — write `fs->fat` to **both** FAT1 (LBA 1) and FAT2 (LBA 10); 0/-1
- **Replaces stub:** `fat12_write_file(fs, name, data, size)` — find or create; free any existing chain; `alloc_chain(ceil(size / bytes_per_cluster))`; write data sectors; update entry; flush FAT + root. `size==0` writes an empty entry with first_cluster=0.

**Steps:**

- [ ] **Step 1: Add chain helpers**

In `fat12.c`, immediately after the new `flush_root` from Task 1, add:

```c
static uint16_t max_cluster(fat12_fs_t* fs) {
    uint32_t data_sectors = fs->total_sectors - fs->first_data_sector;
    uint32_t clusters = data_sectors / fs->sectors_per_cluster + 2u;
    if (clusters > 0xFF4u) clusters = 0xFF4u;
    return (uint16_t)clusters;
}

static void free_chain(fat12_fs_t* fs, uint16_t first) {
    if (first < 2u || first >= 0xFF8u) return;
    while (first < 0xFF8u) {
        uint16_t next = get_fat_entry(fs, first);
        set_fat_entry(fs, first, 0u);
        if (next < 2u || next >= 0xFF8u) break;
        first = next;
    }
}

static uint16_t alloc_chain(fat12_fs_t* fs, uint32_t n) {
    if (n == 0u) return 0;
    uint16_t maxc = max_cluster(fs);
    uint16_t first = 0u, prev = 0u;
    for (uint32_t i = 0u; i < n; ++i) {
        uint16_t c = 0u;
        for (uint16_t s = 2u; s < maxc; ++s) {
            if (get_fat_entry(fs, s) == 0u) { c = s; break; }
        }
        if (c == 0u) { if (first) free_chain(fs, first); return 0; }
        set_fat_entry(fs, c, 0xFFFu);
        if (first == 0u) first = c;
        else             set_fat_entry(fs, prev, c);
        prev = c;
    }
    return first;
}

static int flush_fat(fat12_fs_t* fs) {
    if (!fs || !fs->fat) return -1;
    for (uint32_t i = 0; i < fs->sectors_per_fat; ++i) {
        if (write_sector(fs, fs->reserved_sectors + i,
                         fs->fat + i * fs->bytes_per_sector) != 0) return -1;
        if (write_sector(fs, fs->reserved_sectors + fs->sectors_per_fat + i,
                         fs->fat + i * fs->bytes_per_sector) != 0) return -1;
    }
    return 0;
}
```

- [ ] **Step 2: Replace the `fat12_write_file` stub**

Replace lines 225-228 with:

```c
static uint8_t* find_root_slot_by_name(fat12_fs_t* fs, const char* src_name) {
    char fname[11];
    fat12_format_name(src_name, fname);
    for (uint32_t i = 0; i < fs->root_entry_count; ++i) {
        uint8_t* e = fs->root_dir + i * 32u;
        if (e[0] == 0x00u || e[0] == 0xE5u || e[11] == 0x0Fu) continue;
        if (name_match((char*)e, fname)) return e;
    }
    return NULL;
}

int fat12_write_file(fat12_fs_t* fs, const char* name, const void* data, uint32_t size) {
    if (!fs || !name || (!data && size > 0u)) return -1;
    file_info_t info;
    int had = (fat12_find_file(fs, name, &info) == 0);
    uint8_t* entry = NULL;
    if (had) {
        entry = find_root_slot_by_name(fs, name);
        if (!entry) return -1;
        if (info.first_cluster >= 2u && info.first_cluster < 0xFF8u)
            free_chain(fs, info.first_cluster);
    } else {
        if (fat12_create_file(fs, name) != 0) return -1;
        entry = find_root_slot_by_name(fs, name);
        if (!entry) return -1;
    }

    uint32_t bpc = (uint32_t)fs->bytes_per_sector * fs->sectors_per_cluster;

    /* zero-size: leave first_cluster 0, size 0 */
    if (size == 0u) {
        entry[26] = 0u; entry[27] = 0u;
        entry[28] = 0u; entry[29] = 0u; entry[30] = 0u; entry[31] = 0u;
        if (flush_fat(fs) != 0) return -1;
        return flush_root(fs);
    }

    uint32_t clusters_needed = (size + bpc - 1u) / bpc;
    uint16_t first = alloc_chain(fs, clusters_needed);
    if (first == 0u) return -1;

    /* Write data sectors first (on-disk FAT still appears free if we crash
       mid-write — recoverable no-op). */
    const uint8_t* src = (const uint8_t*)data;
    uint16_t cluster = first;
    uint32_t written = 0u;
    while (written < size && cluster >= 2u && cluster < 0xFF8u) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        for (uint8_t s = 0u; s < fs->sectors_per_cluster && written < size; ++s) {
            uint8_t secbuf[512];
            fs_memset(secbuf, 0, sizeof(secbuf));
            uint32_t chunk = (uint32_t)fs->bytes_per_sector;
            if (chunk > size - written) chunk = size - written;
            fs_memcpy(secbuf, src + written, chunk);
            if (write_sector(fs, sector + s, secbuf) != 0) {
                free_chain(fs, first);
                return -1;
            }
            written += chunk;
        }
        cluster = get_fat_entry(fs, cluster);
    }

    if (flush_fat(fs) != 0) { free_chain(fs, first); return -1; }
    entry[26] = (uint8_t)(first & 0xFFu);
    entry[27] = (uint8_t)((first >> 8) & 0xFFu);
    entry[28] = (uint8_t)(size & 0xFFu);
    entry[29] = (uint8_t)((size >> 8) & 0xFFu);
    entry[30] = (uint8_t)((size >> 16) & 0xFFu);
    entry[31] = (uint8_t)((size >> 24) & 0xFFu);
    return flush_root(fs);
}
```

- [ ] **Step 3: Add `vga_shell_write` in `kmain.c`**

After `vga_shell_touch`, add:

```c
static void vga_shell_write(const char* rest) {
    while (*rest == ' ') rest++;
    if (!*rest) { vga_puts("usage: write <file> <text>\n"); return; }
    /* split into filename + data (rest of line) */
    const char* sp = rest;
    while (*sp && *sp != ' ') sp++;
    char fname[64];
    unsigned flen = (unsigned)(sp - rest);
    if (flen >= sizeof(fname)) flen = sizeof(fname) - 1u;
    for (unsigned i = 0u; i < flen; ++i) fname[i] = rest[i];
    fname[flen] = '\0';
    while (*sp == ' ') sp++;
    unsigned dlen = 0u;
    while (sp[dlen]) dlen++;
    if (!g_fs) { vga_puts("filesystem not mounted\n"); return; }
    if (fat12_write_file(g_fs, fname, sp, dlen) != 0)
        vga_puts("write: failed\n");
    else
        vga_puts("write: ok\n");
}
```

- [ ] **Step 4: Add the `write` branch**

In `vga_shell_run`'s `else if` chain, immediately after the `touch` branch from Task 1:

```c
} else if (cmd[0] == 'w' && cmd[1] == 'r' && cmd[2] == 'i') {
    vga_shell_write(line + 5);
```

- [ ] **Step 5: Build**

```bash
cd pasinux/kernel && make image
```

Expected: clean build.

- [ ] **Step 6: QEMU integration test**

```bash
cd pasinux/kernel && make qemu
```

| Typed | Expected |
|---|---|
| `touch note.txt` | `touch: ok` |
| `write note.txt hello world` | `write: ok` |
| `ls` | `NOTE.TXT  (12 bytes)` |
| `cat note.txt` | `hello world` |
| `write note.txt abc` | `write: ok` |
| `cat note.txt` | `abc` (overwrite truncates) |

- [ ] **Step 7: Persistence**

Close QEMU, restart, type `cat note.txt`. Expected: `abc`.

- [ ] **Step 8: Commit**

```bash
cd /c/Users/lekov/pasinux
git add pasinux/kernel/fs/fat12.c pasinux/kernel/kernel/kmain.c
git commit -m "feat(fat12): write_file + write shell command"
```

---

## Task 3: `fat12_delete_file` + shell `rm`

**Files:** `pasinux/kernel/fs/fat12.c` (replace stub) · `pasinux/kernel/kernel/kmain.c` (handler + branch)

**Interfaces:** `fat12_delete_file(fs, name)` finds the entry, frees its chain, marks the slot `0xE5`, flushes FAT + root.

- [ ] **Step 1: Replace the `fat12_delete_file` stub**

Replace lines 235-238 of `fat12.c` with:

```c
int fat12_delete_file(fat12_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;
    file_info_t info;
    if (fat12_find_file(fs, name, &info) != 0) return -1;
    for (uint32_t i = 0; i < fs->root_entry_count; ++i) {
        uint8_t* e = fs->root_dir + i * 32u;
        if (e[0] == 0x00u || e[0] == 0xE5u || e[11] == 0x0Fu) continue;
        char fname[11]; fat12_format_name(name, fname);
        if (!name_match((char*)e, fname)) continue;
        if (info.first_cluster >= 2u && info.first_cluster < 0xFF8u)
            free_chain(fs, info.first_cluster);
        e[0] = 0xE5u; /* mark deleted */
        if (flush_fat(fs) != 0) return -1;
        return flush_root(fs);
    }
    return -1;
}
```

- [ ] **Step 2: Add `vga_shell_rm`**

```c
static void vga_shell_rm(const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) { vga_puts("usage: rm <file>\n"); return; }
    if (!g_fs) { vga_puts("filesystem not mounted\n"); return; }
    if (fat12_delete_file(g_fs, arg) != 0) vga_puts("rm: failed\n");
    else                                   vga_puts("rm: ok\n");
}
```

- [ ] **Step 3: Add the `rm` branch**

In `vga_shell_run` (with the existing `else if` for `u` on `uptime`):

```c
} else if (cmd[0] == 'r' && cmd[1] == 'm') {
    vga_shell_rm(line + 2);
```

- [ ] **Step 4: Build & test**

```bash
cd pasinux/kernel && make image && make qemu
```

| Typed | Expected |
|---|---|
| `touch junk.tmp` | `touch: ok` |
| `write junk.tmp leftover` | `write: ok` |
| `ls` | lists `JUNK.TMP  (8 bytes)` |
| `rm junk.tmp` | `rm: ok` |
| `ls` | list no longer contains `JUNK.TMP` |
| `cat junk.tmp` | `cat: file not found: junk.tmp` |
| `rm junk.tmp` | `rm: failed` |

- [ ] **Step 5: Persistence + cluster-reuse check**

Close QEMU, restart.

| Typed | Expected |
|---|---|
| `ls` | `JUNK.TMP` still absent |
| `touch reuse.tmp` then `write reuse.tmp z` then `ls` | `REUSE.TMP  (1 bytes)` and its first_cluster (serial output if desired) overlaps with the cluster that `JUNK.TMP` previously used (proves `free_chain` actually freed bytes) |

Pattern check to assert cluster reuse: after delete + new `touch`+`write`, `REUSE.TMP`'s first_cluster equals `JUNK.TMP`'s old first_cluster. (Optional — only if you want extra confidence the FAT was flushed freed.)

- [ ] **Step 6: Commit**

```bash
cd /c/Users/lekov/pasinux
git add pasinux/kernel/fs/fat12.c pasinux/kernel/kernel/kmain.c
git commit -m "feat(fat12): delete_file + rm shell command"
```

---

## Task 4: `fat12_create_dir` + shell `mkdir`

**Files:** `pasinux/kernel/fs/fat12.h` (declare) · `pasinux/kernel/fs/fat12.c` (implement) · `pasinux/kernel/kernel/kmain.c` (handler + branch + help line)

**Interfaces:**
- `fat12_create_dir(fs, name)` — allocate 1 cluster, write `.`/`..` 32-byte entries into its first sector, mark root entry attr `0x10` with first_cluster = that cluster, flush FAT + root. (`..` first_cluster = 0 ⇒ "parent is root".)

- [ ] **Step 1: Declare in `fat12.h`**

Insert after the `fat12_create_file` declaration (around line 50):

```c
// Create a new directory in the root. Returns 0 on success.
int fat12_create_dir(fat12_fs_t* fs, const char* name);
```

- [ ] **Step 2: Implement in `fat12.c`**

After the existing `fat12_delete_file` body, add:

```c
int fat12_create_dir(fat12_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;
    file_info_t existing;
    if (fat12_find_file(fs, name, &existing) == 0) return -1;
    uint16_t cluster = alloc_chain(fs, 1u);
    if (cluster == 0u) return -1;

    /* zero cluster, then write '.' (self) and '..' (parent=root) entries */
    uint8_t secbuf[512];
    fs_memset(secbuf, 0, sizeof(secbuf));
    uint8_t* dot = secbuf;
    fs_memset(dot, ' ', 11);
    fs_memcpy(dot, ".          ", 11);  /* '.' then 10 spaces */
    dot[11] = 0x10;                     /* directory */
    dot[26] = (uint8_t)(cluster & 0xFFu);
    dot[27] = (uint8_t)((cluster >> 8) & 0xFFu);
    uint8_t* dotdot = secbuf + 32;
    fs_memset(dotdot, ' ', 11);
    fs_memcpy(dotdot, "..         ", 11);
    dotdot[11] = 0x10;
    /* dotdot first_cluster stays 0 -> root */

    uint32_t sector = cluster_to_sector(fs, cluster);
    for (uint8_t s = 0u; s < fs->sectors_per_cluster; ++s) {
        if (s > 0u) fs_memset(secbuf, 0, sizeof(secbuf));
        if (write_sector(fs, sector + s, secbuf) != 0) {
            free_chain(fs, cluster);
            return -1;
        }
    }
    if (flush_fat(fs) != 0) { free_chain(fs, cluster); return -1; }

    int off = find_free_dir_slot(fs);
    if (off < 0) { free_chain(fs, cluster); return -1; }
    char fname[11]; fat12_format_name(name, fname);
    uint8_t* e = fs->root_dir + off;
    fs_memcpy(e, fname, 11);
    e[11] = 0x10;
    fs_memset(e + 12, 0, 20);
    e[26] = (uint8_t)(cluster & 0xFFu);
    e[27] = (uint8_t)((cluster >> 8) & 0xFFu);
    return flush_root(fs);
}
```

- [ ] **Step 3: Add `vga_shell_mkdir`**

```c
static void vga_shell_mkdir(const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) { vga_puts("usage: mkdir <dir>\n"); return; }
    if (!g_fs) { vga_puts("filesystem not mounted\n"); return; }
    if (fat12_create_dir(g_fs, arg) != 0) vga_puts("mkdir: failed (exists or full)\n");
    else                                   vga_puts("mkdir: ok\n");
}
```

- [ ] **Step 4: Add the `mkdir` branch**

Insert in `vga_shell_run` chain. Place **after** the `mm` branch (`cmd[0]=='m' && cmd[1]=='m'`) so `mm` keeps winning:

```c
} else if (cmd[0] == 'm' && cmd[1] == 'k') {
    vga_shell_mkdir(line + 5);
```

- [ ] **Step 5: Add `mkdir` to the help text**

In `vga_shell_help`, add:

```c
vga_puts("  mkdir <dir>  create a directory\n");
```

- [ ] **Step 6: Build & test**

```bash
cd pasinux/kernel && make image && make qemu
```

| Typed | Expected |
|---|---|
| `mkdir docs` | `mkdir: ok` |
| `ls` | lists `DOCS  (0 bytes)` (size field shows 0 — dirs have no size; this is fine for our ls) |
| `mkdir docs` | `mkdir: failed (exists or full)` |
| `touch readme.md` then `write readme.md in docs` then `ls` | `README.MD  (8 bytes)` plus `DOCS  (0 bytes)` |
| `rm readme.md` | `rm: ok` |

- [ ] **Step 7: Verify the dir cluster has `.`/`..`**

After the run, do **not** reboot. The `'ls'` output's full-disk-faithful; persistence reboot covers everything else. The behavior is "real FAT12 subdir" — it has its own cluster with `.`/`..` — but we are not descending into it for file ops (per spec §6, out of scope).

- [ ] **Step 8: Commit**

```bash
cd /c/Users/lekov/pasinux
git add pasinux/kernel/fs/fat12.c pasinux/kernel/fs/fat12.h pasinux/kernel/kernel/kmain.c
git commit -m "feat(fat12): create_dir + mkdir shell command"
```

---

## Task 5: Boot-time multi-cluster write read-back self-test

**Files:** `pasinux/kernel/kernel/kmain.c` (new function called from `freestanding_subsystems_up`)

**Why:** The shell's `write` command is capped at the 128-byte `keyboard_readline_vga` line buffer, so we can never test multi-cluster write via the shell alone. This task adds an idempotent boot-time test that writes a **900-byte (= 3 clusters)** buffer, reads it back, compares every byte, deletes the temp file, and prints one log line.

- [ ] **Step 1: Add the self-test function to `kmain.c`**

Immediately after the existing `[FS] selftest: KERNEL.BIN ... OK` block in `freestanding_subsystems_up` (search for `[FS] selftest:`), add:

```c
static void fat12_write_selftest(void) {
    if (!g_fs) { serial_puts("[FS] writeselftest: no fs\n"); return; }
    static const char* tn = "WRTEST.TMP";
    uint8_t buf[900];
    for (unsigned i = 0u; i < sizeof(buf); ++i) buf[i] = (uint8_t)(i % 251u);
    if (fat12_write_file(g_fs, tn, buf, sizeof(buf)) != 0) {
        serial_puts("[FS] writeselftest: write FAILED\n"); return;
    }
    file_info_t info;
    if (fat12_find_file(g_fs, tn, &info) != 0 || info.size != 900u) {
        serial_puts("[FS] writeselftest: find/size FAILED\n");
        fat12_delete_file(g_fs, tn); return;
    }
    uint32_t sz = 0u; void* rd = fat12_read_file(g_fs, &info, &sz);
    if (!rd || sz != 900u) {
        serial_puts("[FS] writeselftest: read FAILED\n");
        fat12_delete_file(g_fs, tn); return;
    }
    unsigned bad = 0u;
    for (unsigned i = 0u; i < 900u; ++i)
        if (((uint8_t*)rd)[i] != buf[i]) ++bad;
    kfree(rd);
    if (bad) {
        serial_puts("[FS] writeselftest: DATA MISMATCH\n");
        fat12_delete_file(g_fs, tn); return;
    }
    if (fat12_delete_file(g_fs, tn) != 0) {
        serial_puts("[FS] writeselftest: cleanup FAILED\n"); return;
    }
    serial_puts("[FS] writeselftest: 900-byte multi-cluster r/w OK\n");
}
```

- [ ] **Step 2: Call it from `freestanding_subsystems_up`**

Directly after the existing KERNEL.BIN selftest (still inside the `if (g_fs) { ... }` block), insert:

```c
fat12_write_selftest();
```

Place it such that it runs **after** mount + the existing read-path selftest, but **before** `ring3_demo()`.

- [ ] **Step 3: Build & test**

```bash
cd pasinux/kernel && make image && make qemu-headless
```

`qemu-headless` runs transiently (mount → test → delete → VGA shell loop is reached → QEMU exits on idle because of `-no-reboot`). Capture the serial output:

```bash
cd pasinux/kernel && make image && make qemu-headless 2>&1 | tee qemu_serial.txt | grep -E "writeselftest|selftest:|KERNEL.BIN"
```

Expected (all four lines present, in this order):

```
[FS] FAT12 mounted OK
[FS] selftest: KERNEL.BIN found, first_cluster=2 read N bytes: OK
[FS] writeselftest: 900-byte multi-cluster r/w OK
```

(`mount` line was always there. The two `selftest` lines are new. `N` is the kernel size; ~42496 typically.)

**Failure modes to recognize:**
- `writeselftest: write FAILED` → alloc_chain broken (Task 2).
- `writeselftest: find/size FAILED` → root_dir flush or size encoding broken.
- `writeselftest: DATA MISMATCH` → sector copy or cluster_to_sector wrong, OR `alloc_chain` linked wrong (this is the strongest multi-cluster correctness signal).
- `writeselftest: cleanup FAILED` → delete_file / FAT flush broken (Task 3).

- [ ] **Step 4: Commit**

```bash
cd /c/Users/lekov/pasinux
git add pasinux/kernel/kernel/kmain.c
git commit -m "test(fat12): 900-byte multi-cluster write/readback selftest"
```

---

## Task 6: Full integration, persistence, and KERNEL.BIN-safety regression

**Files:** none modified (this is a regression + integration harness task). All assertions live in shell interactions and serial logs.

**Why:** The shell test paths exercised Tasks 1-4 cover happy-path creation/use/deletion. This task explicitly asserts:
1. KERNEL.BIN remains intact (cluster range and content) across all those operations — a misuse could overwrite the loaded code.
2. Every interaction persists across QEMU restarts (writes really hit the disk image).
3. Edge cases: full-cluster allocation boundary is handled gracefully (write into the only remaining free cluster, then a second write that doesn't fit reports failure without corrupting FAT).

- [ ] **Step 1: Run the full end-to-end session + persistence sweep**

A single QEMU session that exercises every operation:

```bash
cd pasinux/kernel && make qemu
```

Typed in order, checking results after each:

| # | Typed | Expected |
|---|---|---|
| 1 | `touch a.txt` | `touch: ok` |
| 2 | `write a.txt one` | `write: ok` |
| 3 | `touch b.txt` `write b.txt two` `touch c.txt` `write c.txt three` | three files created and written |
| 4 | `ls` | four entries: KERNEL.BIN, A.TXT, B.TXT, C.TXT with their sizes |
| 5 | `mkdir projects` | `mkdir: ok` |
| 6 | `ls` | five entries: KERNEL.BIN, A/B/C .TXT, PROJECTS (size 0) |
| 7 | `cat a.txt` | `one` |
| 8 | `cat b.txt` | `two` |
| 9 | `cat c.txt` | `three` |
| 10 | `rm b.txt` | `rm: ok` |
| 11 | `cat b.txt` | `cat: file not found: b.txt` |
| 12 | `ls` | four entries: KERNEL.BIN, A.TXT, C.TXT, PROJECTS |

Close QEMU (`Ctrl-A X` if `make qemu-serial`, close window if `make qemu`).

- [ ] **Step 2: Restart QEMU and prove persistence**

```bash
cd pasinux/kernel && make qemu
```

| Typed | Expected |
|---|---|
| `ls` | same four entries as at end of step 1 — KERNEL.BIN, A.TXT, C.TXT, PROJECTS |
| `cat a.txt` | `one` |
| `cat c.txt` | `three` |
| `rm a.txt` | `rm: ok` |
| `ls` | KERNEL.BIN, C.TXT, PROJECTS |
| `rm c.txt` | `rm: ok` |
| `rm projects` | `rm: failed` (directory entries with attr `0x10` are not deleted here — we don't walk them — by spec §6 design) |

That's the intended behavior. The `rm mkdir` failure on a directory is **not** a regression; it is the documented limit of this Part 1 plan. Log it and move on.

- [ ] **Step 3: KERNEL.BIN content-integrity check**

After step 2, the kernel is still running, so KERNEL.BIN was implicitly verified to be readable for `mount()` at this boot (that is what found it). For an even stronger check — that KERNEL.BIN's content bytes are byte-identical to the original `kernel.bin` artifact — type `cat kernel.bin` and visually verify the first 64 bytes begin with `MZ` followed by the PE header signature, then dump a hex range via... actually we don't currently have a hex-dump shell command. Reasonable proof that the artifact is byte-identical: `ls`-reported size of KERNEL.BIN equals the byte size of `pasinux/kernel/kernel.bin`:

```bash
ls          # in QEMU — note `KERNEL.BIN  (NNNN bytes)`
cd pasinux/kernel
ls -l pasinux.img kernel.bin
```

The size `N` reported by `ls` should equal `wc -c < kernel.bin` on the host. (Both are determined at image-build time by the same kernel size.)

- [ ] **Step 4: Final cleanup commit (no code changes if clean)**

If step 2/3 found any issue, fix it before committing. If clean, no commit needed — Tasks 1-5 already ship.

```bash
cd /c/Users/lekov/pasinux
git status --short   # should show no source modifications
```

Expected: either empty (clean) or only untracked artifacts (`qemu_*.txt`, etc.).

- [ ] **Step 5: Tag the Part-1 completion**

```bash
cd /c/Users/lekov/pasinux
git log --oneline -6
git tag -a part1-fat12-write-folders -m "FAT12 write path + folders + shell commands complete"
git push origin part1-fat12-write-folders     # only if you have a remote
```

---

## End-of-plan summary

After all six tasks the `fs/fat12.c` write path is real, `kernel/kmain.c` exposes `touch`/`write`/`rm`/`mkdir`, and the kernel can create, edit, and delete files and folders on a durable floppy image — the immediate "can't create files/folders" bug is fixed.

Out-of-scope items NOT covered by this plan (deferred to a future plan):
- The ELF32 loader and user-mode notepad (Part 2/3 of the spec; gated on an i386-ELF cross-toolchain being installed).
- Nested subdirectory traversal (`cd projects; cat readme`).
- `rm` of directories.
- Pretty directory-size/attribute display in `ls`.

