CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -g
LDFLAGS ?=
TARGET := kernel_sim
OBJS := kernel.o mm.o scheduler.o driver.o ipc.o

GUI_TARGET := kernel_gui
GUI_OBJS := kernel_gui.o mm.o scheduler.o driver.o ipc.o gui_main.o
GUI_CFLAGS := -mwindows $(CFLAGS) -DPASINUX_GUI_BUILD
GUI_LDFLAGS := -mwindows $(LDFLAGS)
GUI_LIBS := -lgdi32 -luser32 -lcomctl32 -lcomdlg32

SAN_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g \
              -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_LDFLAGS := -fsanitize=address,undefined
SAN_TARGET := kernel_sim_san
SAN_DIR := san
SAN_OBJS := $(SAN_DIR)/kernel.o $(SAN_DIR)/mm.o $(SAN_DIR)/scheduler.o \
            $(SAN_DIR)/driver.o $(SAN_DIR)/ipc.o


NASM ?= $(or $(shell command -v nasm 2>/dev/null), \
          C:/Users/lekov/AppData/Local/bin/NASM/NASM.exe, \
          /c/Users/lekov/AppData/Local/bin/NASM/NASM.exe, nasm)
PYTHON ?= $(or $(shell command -v python 2>/dev/null), \
            C:/Python314/python.exe, python)
QEMU ?= $(or $(shell command -v qemu-system-i386 2>/dev/null), \
           "C:/Program Files/qemu/qemu-system-i386.exe", \
           /c/Program\ Files/qemu/qemu-system-i386.exe, \
           qemu-system-i386)
KERNEL_SECTORS := 32
FREE_CFLAGS := -m32 -std=c11 -ffreestanding -fno-pic -fno-pie \
               -fno-stack-protector -fno-asynchronous-unwind-tables \
               -Wall -Wextra -Wpedantic -Werror
FREE_OBJS := entry.o isr.o kmain.o vga.o idt.o interrupt.o timer.o sched_fs.o serial.o keyboard.o ipc_fs.o
IMAGE := pasinux.img

.PHONY: all clean run syntax gui run-gui sanitize image boot qemu qemu-headless qemu-serial

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel_gui.o: kernel.c
	$(CC) $(GUI_CFLAGS) -c $< -o $@

syntax:
	$(CC) $(CFLAGS) -fsyntax-only kernel.c mm.c scheduler.c driver.c ipc.c

run: $(TARGET)
	./$(TARGET)

run-gui: gui
	./$(GUI_TARGET).exe

gui: $(GUI_TARGET)

$(GUI_TARGET): $(GUI_OBJS)
	$(CC) $(GUI_LDFLAGS) -o $@ $(GUI_OBJS) $(GUI_LIBS)
.
gui_main.o: gui_main.c
	$(CC) $(GUI_CFLAGS) -c $< -o $@

$(SAN_DIR):
	mkdir -p $(SAN_DIR)

$(SAN_DIR)/%.o: %.c | $(SAN_DIR)
	$(CC) $(SAN_CFLAGS) -c $< -o $@

$(SAN_TARGET): $(SAN_OBJS)
	$(CC) $(SAN_LDFLAGS) -o $@ $(SAN_OBJS)

sanitize: $(SAN_TARGET)


boot.bin: boot.asm
	$(NASM) -f bin $< -o $@

entry.o: entry.asm
	$(NASM) -f win32 $< -o $@

isr.o: isr.asm
	$(NASM) -f win32 $< -o $@

kmain.o vga.o idt.o interrupt.o timer.o sched_fs.o serial.o keyboard.o ipc_fs.o: %.o: %.c
	$(CC) $(FREE_CFLAGS) -c $< -o $@

kernel.pe: $(FREE_OBJS) linker.ld
	ld -m i386pe -T linker.ld --image-base 0x10000 \
		--section-alignment 16 --file-alignment 16 \
		-e _start -o $@ $(FREE_OBJS)

kernel.bin: kernel.pe boot.bin mkimage.py
	$(PYTHON) mkimage.py --pe kernel.pe --boot boot.bin \
		--kernel-bin kernel.bin --image $(IMAGE) \
		--kernel-sectors $(KERNEL_SECTORS)

$(IMAGE): kernel.bin

image boot: $(IMAGE)

qemu: $(IMAGE)
	$(QEMU) -drive file=$(IMAGE),format=raw,if=floppy -boot order=a

qemu-headless: $(IMAGE)
	$(QEMU) -drive file=$(IMAGE),format=raw,if=floppy -boot order=a \
		-display none -serial stdio -monitor none -no-reboot

qemu-serial: $(IMAGE)
	$(QEMU) -drive file=$(IMAGE),format=raw,if=floppy -boot order=a \
		-display none -serial stdio -monitor none

clean:
	$(PYTHON) -c "from pathlib import Path; import shutil; \
files='$(OBJS) $(TARGET) $(TARGET).exe $(GUI_OBJS) $(GUI_TARGET) $(GUI_TARGET).exe \
$(SAN_TARGET) $(SAN_TARGET).exe boot.bin entry.o isr.o kmain.o vga.o idt.o \
interrupt.o timer.o sched_fs.o serial.o keyboard.o ipc_fs.o kernel.pe kernel.bin $(IMAGE)'.split(); \
[Path(f).unlink(missing_ok=True) for f in files]; shutil.rmtree('$(SAN_DIR)', ignore_errors=True)"
