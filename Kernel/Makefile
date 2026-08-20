# ================= Toolchain =================
# On CI / Ubuntu this needs: sudo apt-get install -y nasm gcc-multilib
# Optional but nicer: a real cross compiler ->  make CC=i686-elf-gcc
CC   ?= cc
NASM ?= nasm
QEMU ?= qemu-system-i386

# ================= Flags =================
CFLAGS  = -m32 -std=c11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -Wall -Wextra -Wpedantic -Werror -g \
          -DFREESTANDING \
          -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui -Ifs \
          -MMD -MP
# add -O2 once the build is green, if you want optimizations

ASFLAGS = -f elf32 -g -F dwarf

# ================= Sources =================
C_SRCS = \
    kernel/kmain.c \
    arch/idt.c \
    arch/interrupt.c \
    arch/paging.c \
    arch/tss.c \
    arch/gdt.c \
    arch/syscall.c \
    mm/mm_fs.c \
    sched/sched_fs.c \
    drivers/vga.c \
    drivers/timer.c \
    drivers/serial.c \
    drivers/keyboard.c \
    drivers/driver_fs.c \
    drivers/pci.c \
    drivers/rtl8139.c \
    drivers/ata.c \
    net/net_eth.c \
    net/net_arp.c \
    net/net_ip.c \
    net/net_tcp.c \
    net/http.c \
    net/json.c \
    fs/fat12.c

ASM_SRCS = \
    boot/entry.asm \
    arch/isr.asm \
    user/user_start.asm

OBJS   = $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)
KERNEL = kernel.elf
IMAGE  = pasinux.img

.PHONY: all image run clean

all: $(KERNEL)

# ================= Build =================
 $(KERNEL): $(OBJS) boot/linker.ld
    $(CC) -m32 -nostdlib -Wl,-T,boot/linker.ld -Wl,-e,_start -o $@ $(OBJS) -lgcc
$(KERNEL_BIN): $(KERNEL)
  objcopy -O binary $(KERNEL) $@

%.o: %.c
    $(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
    $(NASM) $(ASFLAGS) $< -o $@

# auto-generated dependencies on headers
-include $(OBJS:.o=.d)

# ================= Disk image =================
image: $(IMAGE)

 $(IMAGE): $(KERNEL)
    @echo "ERROR: paste your image-building commands (python script / mkfs.vfat / mtools) here"
    @exit 1

# ================= QEMU =================
run: $(IMAGE)
    $(QEMU) -drive format=raw,if=floppy,file=$(IMAGE)

# ================= Clean =================
clean:
    rm -f $(OBJS) $(OBJS:.o=.d) $(KERNEL) $(IMAGE)
