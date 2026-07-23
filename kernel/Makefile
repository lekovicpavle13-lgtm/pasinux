CC      = gcc
LD      = ld
OBJCOPY = objcopy
NASM    = nasm

CFLAGS  = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -Wall -Wextra -c
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib
NASMFLAGS = -f elf32

# ONLY compiling kernel and serial for the first boot test
C_SOURCES = kernel.c serial.c
ASM_SOURCES = entry.asm isr.asm

C_OBJECTS = $(C_SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)

all: build/disk.img

build/disk.img: build/boot.bin build/kernel.bin
    cat build/boot.bin build/kernel.bin > build/disk.img

build/kernel.bin: build/kernel.elf
    $(OBJCOPY) -O binary $< $@

build/kernel.elf: $(C_OBJECTS) $(ASM_OBJECTS) linker.ld
    $(LD) $(LDFLAGS) -o $@ $(C_OBJECTS) $(ASM_OBJECTS)

build/boot.bin: boot.asm build/kernel.bin
    $(eval KERNEL_SIZE=$(shell wc -c < build/kernel.bin))
    $(eval KERNEL_SECTORS=$(shell echo $$(( ($(KERNEL_SIZE) / 512) + 1 ))))
    $(NASM) -f bin -dKERNEL_SECTORS=$(KERNEL_SECTORS) -o $@ $<

%.o: %.c
    $(CC) $(CFLAGS) -o $@ $<

%.o: %.asm
    $(NASM) $(NASMFLAGS) -o $@ $<

clean:
    rm -f build/*.o build/*.elf build/*.bin build/*.img
    mkdir -p build