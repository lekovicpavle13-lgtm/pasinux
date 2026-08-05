# pasinux Codebase Reorganization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move 55+ flat kernel source files into 9 subsystem directories, updating only the Makefile and file paths — zero source code changes.

**Architecture:** Create subdirectories (`boot/`, `arch/`, `mm/`, `sched/`, `drivers/`, `net/`, `ipc/`, `kernel/`, `user/`, `gui/`), add `-I` flags for each so all bare `#include "foo.h"` statements resolve without changes, update Makefile targets and rules to use subdirectory paths.

**Tech Stack:** GNU Make, git mv, nasm, gcc, ld

## Global Constraints

- Zero source code changes to `.c`, `.h`, or `.asm` files — pure moves + Makefile updates only
- Both build paths must work: `make` (hosted) and `make image` (freestanding)
- All three hosted variants must work: `make run`, `make gui`, `make sanitize`
- Makefile must use `-I` flags for all subdirectories so includes resolve
- NASM file paths updated in Makefile rules
- Linker script and mkimage.py paths updated
- Committed after each task, but git mv preserves history

---

### Task 1: Create subdirectories and update Makefile infrastructure

**Files:**
- Modify: `pasinux/kernel/Makefile`

**Interfaces:**
- Consumes: Current Makefile structure
- Produces: Updated Makefile with subdirectory flags and paths

**Details:**
The Makefile needs four categories of changes:

1. **`-I` flags** added to both `CFLAGS` and `FREE_CFLAGS`:
   ```
   CFLAGS += -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui
   FREE_CFLAGS += -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui
   ```
   (Use `+=` so existing flags are preserved)

2. **Hosted OBJS paths** updated:
   ```
   OBJS := kernel/kernel.o mm/mm.o sched/scheduler.o drivers/driver.o ipc/ipc.o
   GUI_OBJS := kernel/kernel.o mm/mm.o sched/scheduler.o drivers/driver.o ipc/ipc.o gui/gui_main.o
   SAN_OBJS := $(SAN_DIR)/kernel/kernel.o $(SAN_DIR)/mm/mm.o $(SAN_DIR)/sched/scheduler.o $(SAN_DIR)/drivers/driver.o $(SAN_DIR)/ipc/ipc.o
   ```

3. **FREE_OBJS paths** updated:
   ```
   FREE_OBJS := boot/entry.o arch/isr.o kernel/kmain.o drivers/vga.o arch/idt.o \
                arch/interrupt.o drivers/timer.o sched/sched_fs.o drivers/serial.o \
                drivers/keyboard.o mm/mm_fs.o drivers/driver_fs.o arch/paging.o \
                arch/tss.o arch/gdt.o arch/syscall.o user/user_start.o drivers/pci.o \
                drivers/rtl8139.o net/net_eth.o net/net_arp.o net/net_ip.o \
                net/net_tcp.o net/http.o net/json.o
   ```

4. **NASM rules** updated with paths:
   ```makefile
   boot/boot.bin: boot/boot.asm
   	$(NASM) -f bin $< -o $@
   
   boot/entry.o: boot/entry.asm
   	$(NASM) -f win32 $< -o $@
   
   arch/isr.o: arch/isr.asm
   	$(NASM) -f win32 $< -o $@
   
   user/user_start.o: user/user_start.asm
   	$(NASM) -f win32 $< -o $@
   ```

5. **Linker + image paths** updated:
   ```makefile
   kernel.pe: $(FREE_OBJS) boot/linker.ld
   	ld -m i386pe -T boot/linker.ld --image-base 0x10000 \
   		--section-alignment 16 --file-alignment 16 \
   		-e _start -o $@ $(FREE_OBJS)
   
   kernel.bin: kernel.pe boot/boot.bin boot/mkimage.py
   	$(PYTHON) boot/mkimage.py --pe kernel.pe --boot boot/boot.bin \
   		--kernel-bin kernel.bin --image $(IMAGE) \
   		--kernel-sectors $(KERNEL_SECTORS)
   ```

6. **SAN directory creation** updated:
   ```makefile
   $(SAN_DIR):
   	mkdir -p $(SAN_DIR)/kernel $(SAN_DIR)/mm $(SAN_DIR)/sched $(SAN_DIR)/drivers $(SAN_DIR)/ipc
   ```

7. **C pattern rules** — use a single `%.o: %.c` rule with target-specific variable overrides:
   ```makefile
   # Generic pattern for all C objects
   %.o: %.c
   	$(CC) $(CFLAGS) -c $< -o $@
   
   # Freestanding objects use FREE_CFLAGS instead
   $(FREE_OBJS): CFLAGS = $(FREE_CFLAGS)
   ```
   This is simpler and handles all subdirectory paths. The hosted `OBJS` get default CFLAGS, `FREE_OBJS` get FREE_CFLAGS via target-specific override.

8. **Clean targets** updated to include subdirectory object paths:
   ```makefile
   clean:
   	$(PYTHON) -c "from pathlib import Path; import shutil; \
   files='$(OBJS) $(TARGET) $(TARGET).exe $(GUI_OBJS) $(GUI_TARGET) $(GUI_TARGET).exe \
   $(SAN_TARGET) $(SAN_TARGET).exe boot/boot.bin boot/entry.o arch/isr.o kernel/kmain.o drivers/vga.o arch/idt.o \
   arch/interrupt.o drivers/timer.o sched/sched_fs.o drivers/serial.o drivers/keyboard.o mm/mm_fs.o drivers/driver_fs.o \
   arch/paging.o arch/tss.o arch/gdt.o arch/syscall.o user/user_start.o kernel.pe kernel.bin $(IMAGE)'.split(); \
   [Path(f).unlink(missing_ok=True) for f in files]; shutil.rmtree('$(SAN_DIR)', ignore_errors=True)"
   ```

- [ ] **Step 1: Write the updated Makefile**

  Replace the current Makefile with the version containing all path updates above. Key changes:
  - Add `+=` -I lines to CFLAGS and FREE_CFLAGS
  - Update all OBJS/GUI_OBJS/SAN_OBJS/FREE_OBJS path lists
  - Replace explicit lists with target-specific CFLAGS override
  - Update NASM rules, linker rule, image rule, clean target

- [ ] **Step 2: Create all subdirectories**

  ```bash
  cd pasinux/pasinux/kernel
  mkdir -p boot arch mm sched drivers net ipc kernel user gui
  ```

- [ ] **Step 3: Commit the empty dirs + Makefile**

  ```bash
  git add boot/ arch/ mm/ sched/ drivers/ net/ ipc/ kernel/ user/ gui/ Makefile
  git commit -m "reorg: create subsystem directories and update Makefile paths"
  ```

---

### Task 2: Move all files to their new locations

**Files (all in `pasinux/kernel/`):**

| File → | New Location |
|--------|-------------|
| `boot.asm` | `boot/boot.asm` |
| `entry.asm` | `boot/entry.asm` |
| `linker.ld` | `boot/linker.ld` |
| `mkimage.py` | `boot/mkimage.py` |
| `_check_pe.py` | `boot/_check_pe.py` |
| `idt.c / idt.h` | `arch/idt.c / arch/idt.h` |
| `interrupt.c / interrupt.h` | `arch/interrupt.c / arch/interrupt.h` |
| `isr.asm` | `arch/isr.asm` |
| `gdt.c / gdt.h` | `arch/gdt.c / arch/gdt.h` |
| `tss.c / tss.h` | `arch/tss.c / arch/tss.h` |
| `paging.c / paging.h` | `arch/paging.c / arch/paging.h` |
| `syscall.c / syscall.h` | `arch/syscall.c / arch/syscall.h` |
| `io.h` | `arch/io.h` |
| `mm.c / mm.h` | `mm/mm.c / mm/mm.h` |
| `mm_fs.c / mm_fs.h` | `mm/mm_fs.c / mm/mm_fs.h` |
| `scheduler.c / scheduler.h` | `sched/scheduler.c / sched/scheduler.h` |
| `sched_fs.c / sched_fs.h` | `sched/sched_fs.c / sched/sched_fs.h` |
| `serial.c / serial.h` | `drivers/serial.c / drivers/serial.h` |
| `vga.c / vga.h` | `drivers/vga.c / drivers/vga.h` |
| `keyboard.c / keyboard.h` | `drivers/keyboard.c / drivers/keyboard.h` |
| `timer.c / timer.h` | `drivers/timer.c / drivers/timer.h` |
| `pci.c / pci.h` | `drivers/pci.c / drivers/pci.h` |
| `rtl8139.c / rtl8139.h` | `drivers/rtl8139.c / drivers/rtl8139.h` |
| `driver.c / driver.h` | `drivers/driver.c / drivers/driver.h` |
| `driver_fs.c / driver_fs.h` | `drivers/driver_fs.c / drivers/driver_fs.h` |
| `net_eth.c / net_eth.h` | `net/net_eth.c / net/net_eth.h` |
| `net_arp.c / net_arp.h` | `net/net_arp.c / net/net_arp.h` |
| `net_ip.c / net_ip.h` | `net/net_ip.c / net/net_ip.h` |
| `net_tcp.c / net_tcp.h` | `net/net_tcp.c / net/net_tcp.h` |
| `http.c / http.h` | `net/http.c / net/http.h` |
| `json.c / json.h` | `net/json.c / net/json.h` |
| `ipc.c / ipc.h` | `ipc/ipc.c / ipc/ipc.h` |
| `kmain.c` | `kernel/kmain.c` |
| `kernel.c / kernel.h` | `kernel/kernel.c / kernel/kernel.h` |
| `types.h` | `kernel/types.h` |
| `user_start.asm` | `user/user_start.asm` |
| `gui_main.c / gui_main.h` | `gui/gui_main.c / gui/gui_main.h` |

- [ ] **Step 1: git mv all files**

  ```bash
  cd pasinux/pasinux/kernel
  
  # boot/
  git mv boot.asm boot/boot.asm
  git mv entry.asm boot/entry.asm
  git mv linker.ld boot/linker.ld
  git mv mkimage.py boot/mkimage.py
  git mv _check_pe.py boot/_check_pe.py
  
  # arch/
  git mv idt.c idt.h arch/
  git mv interrupt.c interrupt.h arch/
  git mv isr.asm arch/
  git mv gdt.c gdt.h arch/
  git mv tss.c tss.h arch/
  git mv paging.c paging.h arch/
  git mv syscall.c syscall.h arch/
  git mv io.h arch/
  
  # mm/
  git mv mm.c mm.h mm/
  git mv mm_fs.c mm_fs.h mm/
  
  # sched/
  git mv scheduler.c scheduler.h sched/
  git mv sched_fs.c sched_fs.h sched/
  
  # drivers/
  git mv serial.c serial.h drivers/
  git mv vga.c vga.h drivers/
  git mv keyboard.c keyboard.h drivers/
  git mv timer.c timer.h drivers/
  git mv pci.c pci.h drivers/
  git mv rtl8139.c rtl8139.h drivers/
  git mv driver.c driver.h drivers/
  git mv driver_fs.c driver_fs.h drivers/
  
  # net/
  git mv net_eth.c net_eth.h net/
  git mv net_arp.c net_arp.h net/
  git mv net_ip.c net_ip.h net/
  git mv net_tcp.c net_tcp.h net/
  git mv http.c http.h net/
  git mv json.c json.h net/
  
  # ipc/
  git mv ipc.c ipc.h ipc/
  
  # kernel/
  git mv kmain.c kernel/
  git mv kernel.c kernel.h kernel/
  git mv types.h kernel/
  
  # user/
  git mv user_start.asm user/
  
  # gui/
  git mv gui_main.c gui_main.h gui/
  ```

- [ ] **Step 2: Verify all files moved correctly**

  ```bash
  cd pasinux/pasinux/kernel
  ls -la *.c *.h *.asm *.py *.ld 2>/dev/null || echo "Root kernel dir clean - good"
  find boot arch mm sched drivers net ipc kernel user gui -name "*.c" -o -name "*.h" -o -name "*.asm" | sort
  ```
  
  Expected: empty root (no .c/.h/.asm files left), all 55+ files in subdirectories.

- [ ] **Step 3: Commit**

  ```bash
  git add -A
  git commit -m "reorg: move kernel files into subsystem directories"
  ```

---

### Task 3: Build and verify all targets

- [ ] **Step 1: Build hosted simulator**

  ```bash
  cd pasinux/pasinux/kernel
  make clean && make
  ```
  
  Expected: compiles without errors, produces `kernel_sim`

- [ ] **Step 2: Run hosted simulator**

  ```bash
  make run
  ```
  
  Expected: runs and produces normal simulator output (MM, scheduler, IPC, chess protocol)

- [ ] **Step 3: Build freestanding image**

  ```bash
  make clean && make image
  ```
  
  Expected: compiles without errors, produces `pasinux.img`

- [ ] **Step 4: Verify in QEMU**

  ```bash
  make qemu-headless
  ```
  
  Expected: boots, shows `[PAGING]`, `[GDT]`, `[TSS]`, shell prompt `pasinux>`

- [ ] **Step 5: Build GUI variant**

  ```bash
  make clean && make gui
  ```
  
  Expected: compiles without errors, produces `kernel_gui`

- [ ] **Step 6: Verify git status is clean**

  ```bash
  cd pasinux
  git status
  ```
  
  Expected: clean working tree, no untracked files left in old locations

- [ ] **Step 7: Commit verification (or amend)**

  If any fixes were needed, commit them. Otherwise the previous commit stands.