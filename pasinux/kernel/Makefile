CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -g
CFLAGS += -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui
LDFLAGS ?=
TARGET := kernel_sim
OBJS := kernel/kernel.o mm/mm.o sched/scheduler.o drivers/driver.o ipc/ipc.o

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
QEMU_NET ?= -nic user,model=rtl8139
KERNEL_SECTORS := 100
FREE_CFLAGS := -DFREESTANDING -m32 -std=c11 -ffreestanding -fno-pic -fno-pie \
               -fno-stack-protector -fno-asynchronous-unwind-tables \
               -Wall -Wextra -Wpedantic -Werror
FREE_CFLAGS += -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui -Ifs -Itui
FREE_OBJS := boot/entry.o arch/isr.o kernel/kmain.o drivers/vga.o arch/idt.o \
             arch/interrupt.o drivers/timer.o sched/sched_fs.o drivers/serial.o \
             drivers/keyboard.o mm/mm_fs.o drivers/driver_fs.o arch/paging.o \
             arch/tss.o arch/gdt.o arch/syscall.o user/user_start.o drivers/pci.o \
             drivers/rtl8139.o net/net_eth.o net/net_arp.o net/net_ip.o \
             net/net_tcp.o net/http.o net/json.o \
             drivers/ata.o fs/fat12.o \
             mm/pmm.o fs/elf.o \
             drivers/ps2mouse.o drivers/rtc.o \
             tui/tui_core.o tui/tui_wm.o tui/widgets.o tui/shell.o
IMAGE := pasinux.img

# i686-ELF cross toolchain for user-mode programs (ELF32).
# https://github.com/lordmilko/i686-elf-tools (Windows build)
ELF_TOOLCHAIN ?= C:/Users/lekov/AppData/Local/bin/i686-elf-tools/bin
ELFCC ?= $(ELF_TOOLCHAIN)/i686-elf-gcc.exe

NOTEPAD_ELF := NOTEPAD.elf
USER_CFLAGS := -m32 -ffreestanding -fno-pic -fno-pie -nostdlib \
               -fno-stack-protector -fno-asynchronous-unwind-tables \
               -O1 -Wall -Wextra -Werror

.PHONY: all clean run syntax gui run-gui sanitize image boot qemu qemu-sdl qemu-debug qemu-headless qemu-serial notepad

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
	$(CC) $(GUI_CFLAGS) -c kernel/kernel.c -o $@

gui/gui_main.o: gui/gui_main.c
	$(CC) $(GUI_CFLAGS) -c gui/gui_main.c -o $@

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

kernel.bin: kernel.pe boot/boot.bin boot/mkimage.py $(NOTEPAD_ELF)
	$(PYTHON) boot/mkimage.py --pe kernel.pe --boot boot/boot.bin \
		--kernel-bin kernel.bin --image $(IMAGE) \
		--kernel-sectors $(KERNEL_SECTORS) \
		--program NOTEPAD.BIN $(NOTEPAD_ELF)

$(NOTEPAD_ELF): user/notepad.c user/user.ld
	$(ELFCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/notepad.c

notepad: $(NOTEPAD_ELF)

$(IMAGE): kernel.bin

image boot: $(IMAGE)

qemu: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) -display sdl -serial stdio -no-reboot -no-shutdown

qemu-vga: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) -display sdl -vga std -no-reboot -no-shutdown

qemu-sdl: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) -display sdl -no-reboot -no-shutdown

qemu-debug: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) -display sdl -serial file:qemu_serial.txt -no-reboot -no-shutdown \
		-d cpu_reset,int,guest_errors 2>qemu_dbiginf.txt

qemu-headless: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) -display none -serial stdio -monitor none -no-reboot

qemu-serial: $(IMAGE)
	$(QEMU) -machine pc -drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) -display none -serial stdio -monitor none

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe $(GUI_OBJS) $(GUI_TARGET) $(GUI_TARGET).exe $(SAN_TARGET) $(SAN_TARGET).exe $(FREE_OBJS) kernel.pe kernel.bin $(IMAGE) $(NOTEPAD_ELF)
	rm -rf $(SAN_DIR)
