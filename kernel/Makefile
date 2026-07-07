
SRC_DIR   := .
BUILD_DIR := build

CC      := gcc
LD      := ld
NASM    := nasm
QEMU    := qemu-system-i386

CFLAGS  := -std=c11 -m32 -ffreestanding -fno-pic -fno-stack-protector \
           -nostdlib -nostdinc -fno-builtin -Wall -Wextra -O2 -c
LDFLAGS := -m elf_i386 -T $(SRC_DIR)/linker.ld -nostdlib

C_SOURCES := kernel.c mm.c scheduler.c driver.c ipc.c
C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ENTRY_OBJ := $(BUILD_DIR)/entry.o

KERNEL_BIN := $(BUILD_DIR)/kernel.bin
BOOT_BIN   := $(BUILD_DIR)/boot.bin
DISK_IMG   := $(BUILD_DIR)/disk.img

.PHONY: all run clean patch-sectors

all: $(DISK_IMG)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)


$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(ENTRY_OBJ): $(SRC_DIR)/entry.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@


$(KERNEL_BIN): $(ENTRY_OBJ) $(C_OBJECTS) $(SRC_DIR)/linker.ld
	$(LD) $(LDFLAGS) -o $(BUILD_DIR)/kernel.elf $(ENTRY_OBJ) $(C_OBJECTS)
	objcopy -O binary $(BUILD_DIR)/kernel.elf $@

$(BOOT_BIN): $(SRC_DIR)/boot.asm $(KERNEL_BIN) | $(BUILD_DIR)
	@KSIZE=$$(stat -c%s $(KERNEL_BIN)); \
	SECTORS=$$(( (KSIZE + 511) / 512 )); \
	echo "kernel.bin is $$KSIZE bytes -> $$SECTORS sectors"; \
	sed "s/^KERNEL_SECTORS.*equ.*/KERNEL_SECTORS       equ $$SECTORS/" \
		$(SRC_DIR)/boot.asm > $(BUILD_DIR)/boot.patched.asm
	$(NASM) -f bin $(BUILD_DIR)/boot.patched.asm -o $@

$(DISK_IMG): $(BOOT_BIN) $(KERNEL_BIN)
	cat $(BOOT_BIN) $(KERNEL_BIN) > $@
	@# Pad to a whole number of sectors so tools/BIOS don't choke on it
	@SIZE=$$(stat -c%s $@); \
	PAD=$$(( (512 - SIZE % 512) % 512 )); \
	if [ $$PAD -gt 0 ]; then \
		dd if=/dev/zero bs=1 count=$$PAD >> $@ 2>/dev/null; \
	fi

# Additional sources for full kernel build
C_SOURCES := kernel.c mm.c scheduler.c driver.c ipc.c idt.c timer.c keyboard.c
C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
ISR_OBJ   := $(BUILD_DIR)/isr.o

$(ISR_OBJ): $(SRC_DIR)/isr.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

# Re-link kernel with ISR
$(KERNEL_BIN): $(ENTRY_OBJ) $(ISR_OBJ) $(C_OBJECTS) $(SRC_DIR)/linker.ld
	$(LD) $(LDFLAGS) -o $(BUILD_DIR)/kernel.elf $(ENTRY_OBJ) $(ISR_OBJ) $(C_OBJECTS)
	objcopy -O binary $(BUILD_DIR)/kernel.elf $@

run: $(DISK_IMG)
	$(QEMU) -drive format=raw,file=$(DISK_IMG)

clean:
	rm -rf $(BUILD_DIR)
