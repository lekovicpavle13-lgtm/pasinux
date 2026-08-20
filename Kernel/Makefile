CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -g
CFLAGS += -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui
LDFLAGS ?=
TARGET := kernel_sim
OBJS := kernel/kernel.o mm/mm.o sched/scheduler.o drivers/driver.o ipc/ipc.o

nasm -f elf32 boot/entry.asm -o boot/entry.o
nasm -f elf32 arch/isr.asm -o arch/isr.o
nasm -f elf32 user/user_start.asm -o user/user_start.o

ld -m elf_i386 -T boot/linker.ld -e _start -o kernel.elf $(OBJS)

GUI_TARGET := kernel_gui
GUI_OBJS := kernel/kernel_gui.o mm/mm.o sched/scheduler.o drivers/driver.o ipc/ipc.o gui/gui_main.o
GUI_CFLAGS := -mwindows $(CFLAGS) -DPASINUX_GUI_BUILD
GUI_LDFLAGS := -mwindows $(LDFLAGS)
GUI_LIBS := -lgdi32 -luser32 -lcomctl32 -lcomdlg32

SAN_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g \
              -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_LDFLAGS := -fsanitize=address,undefined
SAN_TARGET := kernel_sim_san
SAN_DIR := san
SAN_OBJS := $(SAN_DIR)/kernel/kernel.o $(SAN_DIR)/mm/mm.o $(SAN_DIR)/sched/scheduler.o \
            $(SAN_DIR)/drivers/driver.o $(SAN_DIR)/ipc/ipc.o

NASM ?= $(or $(shell command -v nasm 2>/dev/null), \
          C:/Users/lekov/AppData/Local/bin/NASM/NASM.exe, \
          /c/Users/lekov/AppData/Local/bin/NASM/NASM.exe, nasm)
PYTHON ?= $(or $(shell command -v python 2>/dev/null), \
            C:/Python314/python.exe, python)
QEMU ?= $(or $(shell command -v qemu-system-i386 2>/dev/null), \
           "C:/Program Files/qemu/qemu-system-i386.exe", \
           /c/Program\ Files/qemu/qemu-system-i386.exe, \
           qemu-system-i386)
KERNEL_SECTORS := 100
FREE_CFLAGS := -DFREESTANDING -m32 -std=c11 -ffreestanding -fno-pic -fno-pie \
               -fno-stack-protector -fno-asynchronous-unwind-tables \
               -Wall -Wextra -Wpedantic -Werror
FREE_CFLAGS += -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui -Ifs
FREE_OBJS := boot/entry.o arch/isr.o kernel/kmain.o drivers/vga.o arch/idt.o \
             arch/interrupt.o drivers/timer.o sched/sched_fs.o drivers/serial.o \
             drivers/keyboard.o mm/mm_fs.o drivers/driver_fs.o arch/paging.o \
             arch/tss.o arch/gdt.o arch/syscall.o user/user_start.o drivers/pci.o \
             drivers/rtl8139.o net/net_eth.o net/net_arp.o net/net_ip.o \
             net/net_tcp.o net/http.o net/json.o \
             drivers/ata.o fs/fat12.o
IMAGE := pasinux.img

.PHONY: all clean run syntax gui run-gui sanitize image boot qemu qemu-sdl qemu-gtk qemu-debug qemu-headless qemu-serial

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

# Generic pattern for all C objects
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Freestanding objects use FREE_CFLAGS instead
$(FREE_OBJS): CFLAGS = $(FREE_CFLAGS)

syntax:
	$(CC) $(CFLAGS) -fsyntax-only kernel/kernel.c mm/mm.c sched/scheduler.c drivers/driver.c ipc/ipc.c

run: $(TARGET)
	./$(TARGET)

run-gui: gui
	./$(GUI_TARGET).exe

gui: $(GUI_TARGET)

$(GUI_TARGET): $(GUI_OBJS)
	$(CC) $(GUI_LDFLAGS) -o $@ $(GUI_OBJS) $(GUI_LIBS)

kernel/kernel_gui.o: kernel/kernel.c
	$(CC) $(GUI_CFLAGS) -c $< -o $@

gui/gui_main.o: gui/gui_main.c
	$(CC) $(GUI_CFLAGS) -c $< -o $@

$(SAN_DIR):
	mkdir -p $(SAN_DIR)/kernel $(SAN_DIR)/mm $(SAN_DIR)/sched $(SAN_DIR)/drivers $(SAN_DIR)/ipc

$(SAN_DIR)/%.o: %.c | $(SAN_DIR)
	$(CC) $(SAN_CFLAGS) -c $< -o $@

$(SAN_TARGET): $(SAN_OBJS)
	$(CC) $(SAN_LDFLAGS) -o $@ $(SAN_OBJS)

sanitize: $(SAN_TARGET)

boot/boot.bin: boot/boot.asm
	$(NASM) -f bin $< -o $@

boot/entry.o: boot/entry.asm
	$(NASM) -f win32 $< -o $@

arch/isr.o: arch/isr.asm
	$(NASM) -f win32 $< -o $@

user/user_start.o: user/user_start.asm
	$(NASM) -f win32 $< -o $@

kernel.pe: $(FREE_OBJS) boot/linker.ld
	ld -m i386pe -T boot/linker.ld --image-base 0x10000 \
		--section-alignment 16 --file-alignment 16 \
		-e _start -o $@ $(FREE_OBJS)

kernel.bin: kernel.pe boot/boot.bin boot/mkimage.py
	$(PYTHON) boot/mkimage.py --pe kernel.pe --boot boot/boot.bin \
		--kernel-bin kernel.bin --image $(IMAGE) \
		--kernel-sectors $(KERNEL_SECTORS)

$(IMAGE): kernel.bin

image boot: $(IMAGE)

# NOTE: `-display sdl,gl=off` forces SDL to software-render frames. On Windows
# hosts the default GL/D3D presenter can open a blank/black window even though
# the guest renders fine; gl=off avoids that. Use `qemu-gtk` as an alternative
# backend if SDL still shows a black window.
qemu: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		-display sdl,gl=off -vga std -serial stdio -no-reboot -no-shutdown

qemu-vga: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		-display sdl,gl=off -vga std -no-reboot -no-shutdown

qemu-sdl: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		-display sdl,gl=off -vga std -no-reboot -no-shutdown

qemu-debug: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		-display sdl,gl=off -vga std -serial file:qemu_serial.txt -no-reboot -no-shutdown \
		-d cpu_reset,int,guest_errors 2>qemu_dbiginf.txt

qemu-gtk: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		-display gtk -vga std -serial stdio -no-reboot -no-shutdown

qemu-headless: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		-display none -serial stdio -monitor none -no-reboot

qemu-serial: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		-display none -serial stdio -monitor none

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe $(GUI_OBJS) $(GUI_TARGET) $(GUI_TARGET).exe $(SAN_TARGET) $(SAN_TARGET).exe $(FREE_OBJS) kernel.pe kernel.bin $(IMAGE)
	rm -rf $(SAN_DIR)
